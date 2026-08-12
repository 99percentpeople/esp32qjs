import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MQUICKJS = ROOT / "components" / "esp32_mquickjs"
SYS_SOURCE = MQUICKJS / "src" / "core" / "esp32_mquickjs_sys.c"
CORE_SOURCE = MQUICKJS / "src" / "core" / "esp32_mquickjs.c"


class SecureRandomArchitectureTests(unittest.TestCase):
    def test_random_hex_uses_a_true_entropy_seeded_drbg(self):
        source = SYS_SOURCE.read_text(encoding="utf-8")
        cmake = (MQUICKJS / "CMakeLists.txt").read_text(encoding="utf-8")

        self.assertIn("bootloader_random_enable()", source)
        self.assertIn("mbedtls_ctr_drbg_seed", source)
        self.assertIn("bootloader_random_disable()", source)
        self.assertIn("mbedtls_ctr_drbg_random", source)
        self.assertIn("mbedtls_ctr_drbg_set_reseed_interval", source)
        self.assertIn("mbedtls_platform_zeroize", source)
        self.assertIn("mbedtls", cmake)
        self.assertIn("bootloader_support", cmake)

    def test_drbg_is_seeded_before_peripheral_modules_initialize(self):
        source = CORE_SOURCE.read_text(encoding="utf-8")
        secure_random = source.index("esp32_mquickjs_init_secure_random(ctx)")
        adc = source.index("esp32_mquickjs_init_adc_runtime()")
        wifi = source.index("esp32_mquickjs_init_wifi_runtime(ctx, runtime)")

        self.assertLess(secure_random, adc)
        self.assertLess(secure_random, wifi)


if __name__ == "__main__":
    unittest.main()
