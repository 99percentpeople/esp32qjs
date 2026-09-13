"""Deferred real EventQueue allocators/factory/receive retention + memory manager.

The existing manager fixture supplies heap/lock boundaries. JS and RTOS creation
are injected; production queue/resource/free/receive functions and the complete
manager execute together. This does not prove ESP32 RTOS ABI or full VM behavior.
"""
from tests.support.fixtures import fixture_text
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
from tests.support.wireless_vm_fixture import ROOT, CORE, extract


class EventQueueBudget(unittest.TestCase):
    def test_allocation_failure_control_reserve_native_retention_and_rtos_delete(self):
        compiler = shutil.which('cc')
        if compiler is None:
            self.skipTest('C compiler unavailable')
        source = (CORE / 'esp32_mquickjs_event_queue.c').read_text()
        heap = (ROOT / 'tests/c/unit/memory/test_memory_wireless.c').read_text().split('int main(void)')[0]
        start = source.index('struct esp32_mquickjs_event_queue {')
        end = source.index('static void *event_queue_allocate')
        structs = source[start:end]
        code = heap + SHIM + structs + BOUNDARIES
        for name in ['event_queue_allocate', 'event_queue_resource_allocate', 'event_queue_resource_release',
                     'event_queue_resource_create_lock', 'event_queue_resource_delete_lock',
                     'event_queue_resource_create_queue', 'event_queue_resource_delete_queue']:
            code += extract(source, name)
        start = source.index('static const esp32_mquickjs_event_queue_resource_ops_t')
        code += source[start:source.index('\n    };', start) + len('\n    };')]
        for name in ['event_queue_runtime', 'event_queue_register', 'event_queue_destroy_native',
                     'event_queue_take_destroy_ownership_locked', 'esp32_mquickjs_event_queue_retain',
                     'esp32_mquickjs_event_queue_release', 'event_queue_future_prepare',
                     'event_queue_future_destroy', 'event_queue_new', 'esp32_mquickjs_event_queue_new',
                     'esp32_mquickjs_event_queue_new_wireless']:
            code += extract(source, name)
        code += 'typedef struct {const char *memory_owner;} future_slot_t;\n'
        code += extract((CORE / 'esp32_mquickjs_future.c').read_text(), 'future_control_allocate')
        with tempfile.TemporaryDirectory() as temp:
            p, binary = Path(temp) / 'case.c', Path(temp) / 'case'
            p.write_text(code + MAIN)
            sources = [CORE / ('esp32_mquickjs_' + n + '.c') for n in
                       ['memory', 'memory_budget', 'memory_owner_accounting', 'memory_dma_accounting', 'event_queue_resources']]
            built = subprocess.run([compiler, '-std=c11', '-pthread', '-Wall', '-Wextra', '-Werror',
                                    '-Wno-unused-function', '-I' + str(ROOT / 'tests/c/support/memory_stubs'),
                                    '-I' + str(ROOT / 'components/esp32_mquickjs/internal'), str(p),
                                    *map(str, sources), '-o', str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(built.returncode, 0, built.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)


SHIM = fixture_text('runtime/test_event_queue_budget/shim.inc')
BOUNDARIES = fixture_text('runtime/test_event_queue_budget/boundaries.inc')
MAIN = fixture_text('runtime/test_event_queue_budget/main.inc')
