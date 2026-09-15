"""Execute the production startup guard across simulated physical boots."""
import unittest

from tests.support.fixtures import fixture_text
from tests.support.native_compile import compile_run
from tests.support.paths import ROOT
from tests.support.wireless_vm_fixture import extract


class StartupGuardTests(unittest.TestCase):
    def test_every_two_fault_reboots_raise_level_up_to_hard(self):
        self.run_case(1)

    def test_external_reboots_clear_levels_and_ignore_interrupted_failure_marker(self):
        self.run_case(2)

    def test_healthy_scheduler_does_not_clear_the_fault_chain(self):
        self.run_case(3)

    def test_software_and_watchdog_faults_are_each_counted_once(self):
        self.run_case(4)

    def test_hard_mode_skips_the_framework_startup_loader(self):
        self.run_case(5)

    def run_case(self, case):
        source = (ROOT / "components/esp32qjs_runtime/src/esp32qjs_runtime.c").read_text()
        code = fixture_text("runtime/test_startup_guard/boundaries.inc")
        for name in (
            "runtime_reset_is_startup_failure", "runtime_reset_failure_reason",
            "runtime_copy_string", "runtime_boot_guard_commit",
            "runtime_boot_guard_load",
            "runtime_boot_guard_arm", "runtime_boot_guard_fail",
            "runtime_boot_guard_disarm_intentional",
            "runtime_boot_guard_note_returned", "runtime_boot_guard_poll_healthy",
            "runtime_startup_path_is_safe", "runtime_run_startup",
        ):
            code += extract(source, name)
        code += f"\n#define TEST_CASE {case}\n"
        code += fixture_text("runtime/test_startup_guard/main.inc")
        compile_run(self, code)
