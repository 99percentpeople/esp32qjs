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

    def test_outer_javascript_watchdog_is_fed_only_during_native_wait(self):
        source = (
            RUNTIME / "src" / "esp32qjs_runtime.c"
        ).read_text(encoding="utf-8")
        future = (
            MQUICKJS / "src" / "core" / "esp32_mquickjs_future.c"
        ).read_text(encoding="utf-8")
        header = (
            MQUICKJS / "include" / "esp32_mquickjs.h"
        ).read_text(encoding="utf-8")
        cooperate_start = source.index("static bool runtime_cooperate(void *opaque)")
        cooperate_end = source.index("\nstatic void runtime_outer_heartbeat", cooperate_start)
        cooperate = source[cooperate_start:cooperate_end]

        self.assertIn("esp_task_wdt_reset();", cooperate)
        self.assertIn("runtime->engine.native_wait_depth > 0", cooperate)
        self.assertIn("runtime_feed_js_watchdog(runtime);", cooperate)
        self.assertIn("esp_task_wdt_reset_user", source)
        self.assertIn('esp_task_wdt_add_user("js_outer"', source)
        self.assertIn("ESP32QJS_OUTER_HEARTBEAT_WAIT_MS", source)
        self.assertIn("uint16_t native_wait_depth;", header)
        wait_start = future.index("JSValue js_future_wait")
        wait_end = future.index("\nJSValue js_future_cancel", wait_start)
        wait_source = future[wait_start:wait_end]
        self.assertIn("esp32_mquickjs_native_wait_begin", wait_source)
        self.assertIn("esp32_mquickjs_native_wait_end", wait_source)

    def test_startup_guard_latches_safe_mode_after_two_failures(self):
        source = (
            RUNTIME / "src" / "esp32qjs_runtime.c"
        ).read_text(encoding="utf-8")
        kconfig = (RUNTIME / "Kconfig.projbuild").read_text(encoding="utf-8")

        self.assertIn("runtime_boot_guard_arm(runtime);", source)
        self.assertIn('runtime_boot_guard_fail(runtime, "startup-exception")', source)
        self.assertIn("runtime_reset_is_startup_failure", source)
        self.assertIn("runtime->safe_mode_requested = true;", source)
        self.assertIn("runtime->safe_mode_active = true;", source)
        self.assertIn(
            'runtime_boot_guard_fail(runtime, "secondary-filesystem")', source
        )
        self.assertIn(
            'runtime_store_software_reason("secondary-filesystem")', source
        )
        self.assertIn(
            "required secondary filesystem is unavailable in safe mode", source
        )
        self.assertIn("config ESP32QJS_RUNTIME_STARTUP_FAILURE_LIMIT", kconfig)
        self.assertIn("default 2", kconfig)

    def test_agent_safe_mode_keeps_system_services_and_skips_workspace(self):
        agent_index = (
            ROOT.parent / "agent" / "device" / "flash_data" / "index.js"
        ).read_text(encoding="utf-8")

        service_attach = agent_index.index(
            "esp32AgentProtocol.attach(agentSerialTransport);"
        )
        startup_status = agent_index.index(
            "var startupState = runtimeState.startup;"
        )
        workspace_load = agent_index.index('load("index.js");')
        workspace_root = agent_index.index('fs.setRoot("/workspace");')
        self.assertLess(startup_status, workspace_root)
        self.assertLess(workspace_root, service_attach)
        self.assertLess(service_attach, workspace_load)
        self.assertIn("filesystem.secondaryMounted === true", agent_index)
        self.assertIn("var runtimeState = sys.status.runtime;", agent_index)
        self.assertNotIn("sys.status()", agent_index)
        self.assertIn(
            "if (workspaceMounted && !startupState.safeModeActive", agent_index
        )
        self.assertEqual(agent_index.count('fs.setRoot("/workspace");'), 1)
        self.assertNotIn("sys.safeMode", agent_index)

    def test_startup_guard_uses_private_nvs_and_ignores_intentional_control(self):
        source = (
            RUNTIME / "src" / "esp32qjs_runtime.c"
        ).read_text(encoding="utf-8")
        control_start = source.index(
            "static esp32_mquickjs_control_result_t runtime_request_control_hook("
        )
        control_end = source.index(
            "\nvoid esp32qjs_runtime_default_config(", control_start
        )
        control = source[control_start:control_end]

        self.assertIn('#define ESP32QJS_BOOT_GUARD_NAMESPACE "qjs_rt"', source)
        self.assertIn("nvs_open(ESP32QJS_BOOT_GUARD_NAMESPACE", source)
        self.assertNotIn('"agent_cfg"', source)
        self.assertIn("runtime_boot_guard_disarm_intentional(runtime);", control)


if __name__ == "__main__":
    unittest.main()
