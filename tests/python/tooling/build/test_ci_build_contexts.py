from tests.support.paths import ROOT as TEST_ROOT
import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = TEST_ROOT
SCRIPT = ROOT / "scripts" / "prepare_ci_build_context.py"
SPEC = importlib.util.spec_from_file_location("prepare_ci_build_context_test", SCRIPT)
CI_CONTEXT = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = CI_CONTEXT
SPEC.loader.exec_module(CI_CONTEXT)


class CiBuildContextTests(unittest.TestCase):
    def test_wireless_limits_are_explicit_in_generated_contexts(self):
        for target, profile, psram in (
            ('esp32c3', 'representative', 0),
            ('esp32c5', 'representative', 0),
            ('esp32s3', 'representative-psram', 4194304),
        ):
            defaults, _, _, _ = CI_CONTEXT.sdkconfig_text(CI_CONTEXT.load_features(), target, profile)
            values = dict(line.split('=', 1) for line in defaults.splitlines() if line.startswith('CONFIG_') and '=' in line)
            self.assertEqual(int(values['CONFIG_ESP32_MQUICKJS_WIRELESS_INTERNAL_BUDGET_BYTES']), 131072)
            self.assertEqual(int(values['CONFIG_ESP32_MQUICKJS_WIRELESS_PSRAM_BUDGET_BYTES']), psram)
            reserve = int(values['CONFIG_ESP32_MQUICKJS_WIRELESS_CONTROL_RESERVE_BYTES'])
            self.assertEqual(reserve, 16384)
            self.assertLessEqual(reserve, int(values['CONFIG_ESP32_MQUICKJS_WIRELESS_INTERNAL_BUDGET_BYTES']))

    def test_nan_inventory_profile_uses_real_c5_kconfig_without_js_api(self):
        with tempfile.TemporaryDirectory() as name:
            output = CI_CONTEXT.generate_context("esp32c5", "wireless-inventory", Path(name))
            defaults = (output / "sdkconfig.defaults").read_text()
            manifest = json.loads((output / "manifest.json").read_text())
            self.assertIn("CONFIG_ESP_WIFI_NAN_SYNC_ENABLE=y", defaults)
            self.assertNotIn("nan", manifest["nativeFeatures"])
        for target in ("esp32c3", "esp32s3"):
            with self.assertRaisesRegex(SystemExit, "only supported for esp32c5"):
                CI_CONTEXT.generate_context(target, "wireless-inventory", Path("unused"))

    def test_ftm_profile_explicitly_enables_sdk_roles_without_changing_representative(self):
        for target in ("esp32c3", "esp32s3", "esp32c5"):
            with self.subTest(target=target), tempfile.TemporaryDirectory() as name:
                output = CI_CONTEXT.generate_context(target, "wireless-ftm", Path(name))
                defaults = (output / "sdkconfig.defaults").read_text()
                manifest = json.loads((output / "manifest.json").read_text())
                self.assertEqual(manifest["hardware"]["mcu"], target)
                self.assertEqual(manifest["contextId"], "firmware-ci-" + target + "-wireless-ftm")
                for field in ("ESP_WIFI_FTM_ENABLE", "ESP_WIFI_FTM_INITIATOR_SUPPORT", "ESP_WIFI_FTM_RESPONDER_SUPPORT"):
                    self.assertIn("CONFIG_" + field + "=y", defaults)
                representative, _, _, _ = CI_CONTEXT.sdkconfig_text(CI_CONTEXT.load_features(), target, "representative")
                self.assertNotIn("CONFIG_ESP_WIFI_FTM_ENABLE=y", representative)

    def test_roaming_profile_explicit_sdk_flags(self):
        for target in ("esp32c3", "esp32s3", "esp32c5"):
            with self.subTest(target=target), tempfile.TemporaryDirectory() as name:
                output = CI_CONTEXT.generate_context(target, "wireless-roaming", Path(name))
                defaults = (output / "sdkconfig.defaults").read_text()
                for field in ("11KV", "RRM", "WNM", "11R"):
                    self.assertIn("CONFIG_ESP_WIFI_" + field + "_SUPPORT=y", defaults)
                representative, _, _, _ = CI_CONTEXT.sdkconfig_text(CI_CONTEXT.load_features(), target, "representative")
                self.assertNotIn("CONFIG_ESP_WIFI_11KV_SUPPORT=y", representative)

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
            ("esp32s3", "bitmap"),
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
        self.assertIn("from build_tools.build import esptool_environment", source)
        self.assertIn("environment = esptool_environment()", source)
        self.assertIn("env=environment", source)

    def test_ci_build_recreates_generated_sdkconfig_from_current_defaults(self):
        source = (ROOT / "scripts" / "ci_build.py").read_text(encoding="utf-8")

        self.assertIn("sdkconfig.unlink(missing_ok=True)", source)
        self.assertIn("sdkconfig_old.unlink(missing_ok=True)", source)

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
            ("esp32s3", "bitmap"),
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

    def test_s3_no_psram_context_reserves_internal_heap_for_wifi(self):
        with tempfile.TemporaryDirectory() as name:
            output = CI_CONTEXT.generate_context(
                "esp32s3", "representative", Path(name)
            )
            defaults = (output / "sdkconfig.defaults").read_text(encoding="utf-8")

        self.assertIn("CONFIG_SPIRAM=n", defaults)
        self.assertIn("CONFIG_ESP32QJS_JS_HEAP_SIZE=106496", defaults)

    def test_bitmap_profile_excludes_optional_jpeg_decoder(self):
        with tempfile.TemporaryDirectory() as name:
            output = CI_CONTEXT.generate_context("esp32s3", "bitmap", Path(name))
            defaults = (output / "sdkconfig.defaults").read_text(encoding="utf-8")
            manifest = json.loads((output / "manifest.json").read_text(encoding="utf-8"))

        self.assertIn("CONFIG_ESP32_MQUICKJS_FEATURE_BITMAP=y", defaults)
        self.assertIn("CONFIG_ESP32_MQUICKJS_FEATURE_BITMAP_JPEG=n", defaults)
        self.assertIn("bitmap", manifest["nativeFeatures"])
        self.assertNotIn("bitmap_jpeg", manifest["nativeFeatures"])
