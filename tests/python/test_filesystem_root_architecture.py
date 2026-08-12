import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MQUICKJS = ROOT / "components" / "esp32_mquickjs"


class FilesystemRootArchitectureTests(unittest.TestCase):
    def test_core_uses_a_generic_filesystem_root(self):
        files = (
            MQUICKJS / "include" / "esp32_mquickjs.h",
            MQUICKJS / "internal" / "esp32_mquickjs_core.h",
            MQUICKJS / "src" / "core" / "esp32_mquickjs.c",
            MQUICKJS / "src" / "core" / "mqjs_stdlib_esp32.c",
            MQUICKJS / "src" / "modules" / "fs" / "esp32_mquickjs_fs.c",
        )
        source = "\n".join(path.read_text(encoding="utf-8") for path in files)

        self.assertIn("fs.setRoot(path)", source)
        self.assertNotIn("workspace", source.lower())
        self.assertNotIn("ESP32QJS_APP_NATIVE", source)


if __name__ == "__main__":
    unittest.main()
