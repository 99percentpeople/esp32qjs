import unittest
from pathlib import Path

from source_contract_test_case import SourceContractTestCase


ROOT = Path(__file__).resolve().parents[2]
MQUICKJS = ROOT / "components" / "esp32_mquickjs"
NVS_SOURCE = MQUICKJS / "src" / "modules" / "nvs" / "esp32_mquickjs_nvs.c"


class NvsArchitectureTests(SourceContractTestCase):
    def test_nvs_is_an_optional_feature_with_explicit_security_policy(self):
        kconfig = (MQUICKJS / "Kconfig.projbuild").read_text(encoding="utf-8")
        cmake = (MQUICKJS / "CMakeLists.txt").read_text(encoding="utf-8")

        self.assertIn("config ESP32_MQUICKJS_FEATURE_NVS", kconfig)
        self.assertIn("default n", kconfig)
        self.assertIn("does not enable NVS encryption", kconfig)
        self.assertIn("CONFIG_ESP32_MQUICKJS_FEATURE_NVS", cmake)
        self.assertIn("src/modules/nvs/esp32_mquickjs_nvs.c", cmake)
        self.assertIn("nvs_flash", cmake)

    def test_nvs_values_are_bounded_and_mutations_use_purge_capable_handles(self):
        source = NVS_SOURCE.read_text(encoding="utf-8")

        self.assertIn("ESP32_MQUICKJS_NVS_MAX_VALUE_BYTES 2048U", source)
        self.assertIn("ESP32_MQUICKJS_NVS_MAX_NAME_BYTES 15U", source)
        self.assertIn("NVS_READWRITE_PURGE", source)
        self.assertIn("nvs_commit(handle)", source)
        self.assertNotIn("nvs_flash_erase", source)
        self.assertNotIn("nvs_flash_generate_keys", source)

    def test_nvs_reports_encryption_without_provisioning_keys(self):
        source = NVS_SOURCE.read_text(encoding="utf-8")
        stdlib = (
            MQUICKJS / "src" / "core" / "mqjs_stdlib_esp32.c"
        ).read_text(encoding="utf-8")
        info = (
            MQUICKJS / "src" / "core" / "esp32_mquickjs_sys.c"
        ).read_text(encoding="utf-8")

        self.assertIn("CONFIG_NVS_ENCRYPTION", source)
        self.assertIn('"encrypted"', source)
        self.assertIn('JS_OBJECT_DEF("nvs"', stdlib)
        self.assertIn('JS_CGETSET_MAGIC_DEF("nvs", js_sys_feature_get', stdlib)
        self.assertIn("CONFIG_ESP32_MQUICKJS_FEATURE_NVS", info)


if __name__ == "__main__":
    unittest.main()
