from tests.support.paths import ROOT as TEST_ROOT
import unittest
from pathlib import Path

from tests.support.source_contract_test_case import SourceContractTestCase


ROOT = TEST_ROOT
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

    def test_core_uses_a_unified_filesystem_namespace(self):
        files = (
            MQUICKJS / "include" / "esp32_mquickjs.h",
            MQUICKJS / "internal" / "esp32_mquickjs_core.h",
            MQUICKJS / "src" / "core" / "esp32_mquickjs.c",
            MQUICKJS / "src" / "core" / "mqjs_stdlib_esp32.c",
            MQUICKJS / "src" / "modules" / "fs" / "esp32_mquickjs_fs.c",
        )
        source = "\n".join(path.read_text(encoding="utf-8") for path in files)

        self.assertIn('JS_OBJECT_DEF("fs", js_fs)', source)
        self.assertIn('JS_CFUNC_DEF("mounts", 0, js_fs_mounts)', source)
        self.assertIn('JS_CFUNC_DEF("watch", 2, js_fs_watch)', source)
        self.assertIn("state->resource_key = mount", source)
        self.assertNotIn("js_fs_set_root", source)
        self.assertNotIn("char fs_root[ESP32_MQUICKJS_FS_ROOT_MAX];\n    char startup_fs_root", source)
        self.assertNotIn("/workspace", source.lower())
        self.assertNotIn("ESP32QJS_WORKSPACE", source)
        self.assertNotIn("ESP32QJS_APP_NATIVE", source)


if __name__ == "__main__":
    unittest.main()
