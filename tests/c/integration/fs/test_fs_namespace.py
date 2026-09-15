"""Execute mount routing, merged listings, mutation guards and load context on the host."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tests.support.paths import ROOT
from tests.support.c_source import extract
from tests.support.fixtures import fixture_text


class FilesystemNamespaceTests(unittest.TestCase):
    def test_root_mount_overlay_mutations_and_nested_load_context(self):
        compiler = shutil.which("cc")
        if compiler is None:
            self.skipTest("C compiler unavailable")
        source = (ROOT / "components/esp32_mquickjs/src/modules/fs/esp32_mquickjs_fs.c").read_text()
        code = fixture_text("fs/test_fs_namespace/boundaries.inc")
        for name in ["find_littlefs_mount_by_root", "find_littlefs_mount_exact",
                     "fs_has_child_mount", "fs_mutation_path_error", "fs_namespace_stat",
                     "fs_list_add_entry", "fs_list_namespace", "load_script_file",
                     "fs_enter_load", "fs_leave_load", "load_from_fs", "fs_runtime", "esp32_mquickjs_fs_notify_change"]:
            code += extract(source, name)
        code += fixture_text("fs/test_fs_namespace/main.inc")
        with tempfile.TemporaryDirectory() as directory:
            test = Path(directory) / "test.c"
            binary = Path(directory) / "test"
            test.write_text(code)
            result = subprocess.run([
                compiler, "-std=c11", "-D_GNU_SOURCE", "-DCONFIG_ESP32_MQUICKJS_FEATURE_FS=1",
                "-Wall", "-Wextra", "-Werror",
                "-I" + str(ROOT / "components/esp32_mquickjs/internal"), str(test),
                str(ROOT / "components/esp32_mquickjs/src/modules/fs/esp32_mquickjs_fs_path.c"),
                "-o", str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
