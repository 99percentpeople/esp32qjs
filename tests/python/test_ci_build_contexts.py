import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "prepare_ci_build_context.py"
SPEC = importlib.util.spec_from_file_location("prepare_ci_build_context_test", SCRIPT)
CI_CONTEXT = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = CI_CONTEXT
SPEC.loader.exec_module(CI_CONTEXT)


class CiBuildContextTests(unittest.TestCase):
    def test_ci_workflow_covers_required_target_profiles(self):
        workflow = (ROOT / ".github" / "workflows" / "ci.yml").read_text(
            encoding="utf-8"
        )
        for target, profile in (
            ("esp32c3", "minimal"),
            ("esp32c3", "representative"),
            ("esp32c5", "minimal"),
            ("esp32c5", "representative"),
            ("esp32s3", "minimal"),
            ("esp32s3", "representative"),
            ("esp32s3", "representative-psram"),
            ("esp32s3", "disabled"),
        ):
            self.assertIn(
                f"- target: {target}\n            profile: {profile}", workflow
            )
        self.assertIn("generate_api_manifest.py --check", workflow)
        self.assertIn("generate_feature_docs.py --check", workflow)
        self.assertIn("remote.py check-js", workflow)
        self.assertIn("unittest discover -s tests/python", workflow)
        self.assertIn("remote.py test --scope c", workflow)
        self.assertIn("repository: joltwallet/esp_littlefs", workflow)
        self.assertIn(
            "ref: 92ac3c2dce8c62c4b47bdb33e8461a4670411475", workflow
        )
        self.assertIn(
            "path: managed_components/joltwallet__littlefs", workflow
        )
        self.assertIn("submodules: recursive", workflow)

    def test_ci_build_ignores_user_global_esptool_configuration(self):
        source = (ROOT / "scripts" / "ci_build.py").read_text(encoding="utf-8")
        self.assertIn("from remote import write_esptool_config", source)
        self.assertIn("write_esptool_config()", source)

    def test_s3_camera_dependency_does_not_depend_on_late_kconfig_resolution(self):
        manifest = (
            ROOT / "components" / "esp32_mquickjs" / "idf_component.yml"
        ).read_text(encoding="utf-8")
        camera = manifest[manifest.index("espressif/esp32-camera:") :]

        self.assertIn('if: "target == esp32s3"', camera)
        self.assertNotIn("$CONFIG{ESP32_MQUICKJS_FEATURE_CAMERA}", camera)

    def test_every_profile_explicitly_selects_every_catalog_feature(self):
        catalog = json.loads(CI_CONTEXT.FEATURE_CATALOG.read_text(encoding="utf-8"))
        kconfigs = {feature["kconfig"] for feature in catalog["features"]}

        for target, profile in (
            ("esp32c3", "minimal"),
            ("esp32c3", "representative"),
            ("esp32c5", "minimal"),
            ("esp32c5", "representative"),
            ("esp32s3", "minimal"),
            ("esp32s3", "representative"),
            ("esp32s3", "representative-psram"),
            ("esp32s3", "disabled"),
        ):
            with self.subTest(target=target, profile=profile), tempfile.TemporaryDirectory() as name:
                output = CI_CONTEXT.generate_context(target, profile, Path(name))
                defaults = (output / "sdkconfig.defaults").read_text(encoding="utf-8")
                selected = {
                    line.split("=", 1)[0]
                    for line in defaults.splitlines()
                    if line.startswith("CONFIG_ESP32_MQUICKJS_")
                }
                self.assertTrue(kconfigs.issubset(selected))
                manifest = json.loads((output / "manifest.json").read_text(encoding="utf-8"))
                self.assertEqual(manifest["hardware"]["mcu"], target)

    def test_psram_profile_is_s3_only_and_uses_external_tls_memory(self):
        with tempfile.TemporaryDirectory() as name:
            output = CI_CONTEXT.generate_context(
                "esp32s3", "representative-psram", Path(name)
            )
            defaults = (output / "sdkconfig.defaults").read_text(encoding="utf-8")
            self.assertIn("CONFIG_SPIRAM=y", defaults)
            self.assertIn("CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y", defaults)
        with tempfile.TemporaryDirectory() as name:
            with self.assertRaisesRegex(SystemExit, "only supported for esp32s3"):
                CI_CONTEXT.generate_context(
                    "esp32c5", "representative-psram", Path(name)
                )
