"""Actual sys budget conversion with movable GC and Nth allocation failure.

Only VM execution is scheduled here; native admission has its production-manager
fixture in tests/c/unit/memory/test_memory_wireless.c. Execution is deferred to Wi-Fi review.
"""
from tests.support.fixtures import fixture_text
import tempfile
import unittest
from tests.support.wireless_vm_fixture import CORE, build, extract, run


class WirelessBudgetGC(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        source = (CORE / 'esp32_mquickjs_sys.c').read_text()
        code = (CORE / 'esp32_mquickjs_memory_budget.c').read_text()
        code += extract(source, 'sys_wireless_budget_region')
        code += extract(source, 'sys_wireless_budget')
        cls.binary = build(cls.temp.name, code, MAIN)

    def test_every_conversion_allocation_and_movable_gc(self):
        for collect in (0, 1):
            run([str(self.binary), str(collect)])


MAIN = fixture_text('wireless/test_wireless_budget_gc/main.inc')
