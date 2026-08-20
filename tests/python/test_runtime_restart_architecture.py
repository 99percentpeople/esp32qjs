import unittest
from pathlib import Path

from source_contract_test_case import SourceContractTestCase


ROOT = Path(__file__).resolve().parents[2]
MQUICKJS = ROOT / "components" / "esp32_mquickjs"
RUNTIME = ROOT / "components" / "esp32qjs_runtime"


class RuntimeRestartArchitectureTests(SourceContractTestCase):
    def test_startup_script_uses_a_boot_scoped_root(self):
        source = (
            MQUICKJS / "src" / "modules" / "fs" / "esp32_mquickjs_fs.c"
        ).read_text(encoding="utf-8")
        header = (
            MQUICKJS / "include" / "esp32_mquickjs.h"
        ).read_text(encoding="utf-8")
        core = (
            MQUICKJS / "src" / "core" / "esp32_mquickjs.c"
        ).read_text(encoding="utf-8")

        self.assertIn("startup_fs_base_path(runtime)", source)
        self.assertIn("char startup_fs_root[ESP32_MQUICKJS_FS_ROOT_MAX];", header)
        self.assertIn("runtime->startup_fs_root[0] = '\\0';", core)
        self.assertIn("runtime->startup_fs_root,", core)

    def test_usb_serial_driver_is_boot_scoped(self):
        source = (
            MQUICKJS / "src" / "modules" / "usb_serial" / "esp32_mquickjs_usb_serial.c"
        ).read_text(encoding="utf-8")

        self.assertIn("s_usb_serial_driver_ready", source)
        self.assertIn("Close generation-owned JS state only", source)
        self.assertNotIn("usb_serial_jtag_vfs_use_nonblocking();", source)
        self.assertNotIn("usb_serial_jtag_driver_uninstall();", source)

    def test_supervisor_publishes_generation_and_restart_count(self):
        source = (
            RUNTIME / "src" / "esp32qjs_runtime.c"
        ).read_text(encoding="utf-8")

        self.assertIn("runtime->generation++;", source)
        self.assertIn("runtime->restart_count++;", source)
        self.assertIn("JavaScript runtime generation", source)


if __name__ == "__main__":
    unittest.main()
