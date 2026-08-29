import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SYS_SOURCE = (
    ROOT / "components" / "esp32_mquickjs" / "src" / "core" / "esp32_mquickjs_sys.c"
)


class SysArchitectureTests(unittest.TestCase):
    def test_option_validation_uses_native_own_property_enumeration(self):
        source = SYS_SOURCE.read_text(encoding="utf-8")
        start = source.index("static bool sys_validate_option_keys(")
        end = source.index("\n#if defined(CONFIG_ESP32_MQUICKJS_SYS_TASK_SNAPSHOT)", start)
        validator = source[start:end]

        self.assertIn('#include "mquickjs_priv.h"', source)
        self.assertIn("esp32_mquickjs_validate_plain_options", validator)
        self.assertNotIn("JS_GetGlobalObject", validator)
        self.assertNotIn("JS_GetGlobalObject", validator)
        self.assertNotIn('"Object"', validator)
        self.assertNotIn('"keys"', validator)
        self.assertNotIn("esp32_mquickjs_call", validator)


if __name__ == "__main__":
    unittest.main()
