import unittest
from pathlib import Path

from source_contract_test_case import SourceContractTestCase


ROOT = Path(__file__).resolve().parents[2]
MQUICKJS = ROOT / "components" / "esp32_mquickjs"


class FilesystemRootArchitectureTests(SourceContractTestCase):
    def test_public_mount_api_has_disabled_feature_stubs(self):
        cmake = (MQUICKJS / "CMakeLists.txt").read_text(encoding="utf-8")
        stub = (
            MQUICKJS / "src/modules/fs/esp32_mquickjs_fs_stub.c"
        ).read_text(encoding="utf-8")

        self.assertIn("esp32_mquickjs_fs_stub.c", cmake)
        self.assertIn("#if !CONFIG_ESP32_MQUICKJS_FEATURE_FS", stub)
        self.assertIn("esp32_mquickjs_mount_littlefs", stub)
        self.assertIn("return false;", stub)

    def test_core_uses_immutable_generic_filesystem_volumes(self):
        files = (
            MQUICKJS / "include" / "esp32_mquickjs.h",
            MQUICKJS / "internal" / "esp32_mquickjs_core.h",
            MQUICKJS / "src" / "core" / "esp32_mquickjs.c",
            MQUICKJS / "src" / "core" / "mqjs_stdlib_esp32.c",
            MQUICKJS / "src" / "modules" / "fs" / "esp32_mquickjs_fs.c",
        )
        source = "\n".join(path.read_text(encoding="utf-8") for path in files)

        self.assertIn('JS_CLASS_DEF("FsVolume"', source)
        self.assertIn('JS_CFUNC_DEF("volume", 1, js_fs_volume)', source)
        self.assertIn("fs_volume_root(ctx, receiver, api_name)", source)
        self.assertNotIn("js_fs_set_root", source)
        self.assertNotIn("char fs_root[ESP32_MQUICKJS_FS_ROOT_MAX];\n    char startup_fs_root", source)
        self.assertNotIn("/workspace", source.lower())
        self.assertNotIn("ESP32QJS_WORKSPACE", source)
        self.assertNotIn("ESP32QJS_APP_NATIVE", source)


if __name__ == "__main__":
    unittest.main()
