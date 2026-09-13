"""Deferred production RTOS storage, Wi-Fi/Future resources and worker bootstrap.

The complete memory manager and actual factories execute together. RTOS creation,
deletion and task admission are fault boundaries, not an ESP32 scheduler/ABI test.
"""
from tests.support.fixtures import fixture_text
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
from tests.support.wireless_vm_fixture import ROOT, CORE, extract


class WirelessRuntimeBudget(unittest.TestCase):
    def test_failed_creation_delete_order_retained_worker_suffix_and_restart(self):
        compiler = shutil.which('cc')
        if compiler is None:
            self.skipTest('C compiler unavailable')
        heap = (ROOT / 'tests/c/unit/memory/test_memory_wireless.c').read_text().split('int main(void)')[0]
        heap = heap.replace('void heap_caps_free(void *data)\n{',
                            'void heap_caps_free(void *data)\n{\n    before_heap_free(data);')
        future = (CORE / 'esp32_mquickjs_future.c').read_text()
        wifi_dir = ROOT / 'components/esp32_mquickjs/src/modules/wifi'
        wifi = (wifi_dir / 'esp32_mquickjs_wifi.c').read_text()
        code = 'static void before_heap_free(void *data);\n' + heap + SHIM
        code += (CORE / 'esp32_mquickjs_memory_rtos.c').read_text()
        for source, functions in [(future, ['future_runtime_resource_allocate',
                'future_runtime_resource_release', 'future_runtime_queue_create', 'future_runtime_queue_delete']),
                (wifi, ['wifi_create_lock', 'wifi_delete_lock', 'wifi_create_event_group',
                        'wifi_delete_event_group', 'wifi_create_queue', 'wifi_delete_queue'])]:
            for name in functions:
                code += extract(source, name)
        for source, tag in [(future, 'future'), (wifi, 'wifi')]:
            a = source.index('static const esp32_mquickjs_' + tag + '_runtime_resource_ops_t')
            code += source[a:source.index('};', a) + 2] + '\n'
        # Use the production worker item/storage declarations and initializer.
        code += re.search(r'typedef struct \{[^}]*\} future_worker_item_t;', future).group(0) + '\n'
        a = future.index('#define ESP32_MQUICKJS_FUTURE_WORKER_STACK_BYTES')
        code += future[a:future.index('static void future_scheduler_snapshot', a)]
        code += 'static QueueHandle_t s_future_worker_queue;\nstatic bool s_future_worker_pool_initialized;\n'
        code += extract(future, 'future_worker_task')
        code += extract(future, 'future_init_worker_pool')
        code += extract(future, 'esp32_mquickjs_submit_background_worker')
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            (directory / 'freertos').mkdir()
            for name in ('queue.h', 'semphr.h', 'event_groups.h'):
                (directory / 'freertos' / name).write_text('#pragma once\n')
            config = (ROOT / 'tests/c/support/memory_stubs/sdkconfig.h').read_text()
            config = config.replace('INTERNAL_BUDGET_BYTES 4096', 'INTERNAL_BUDGET_BYTES 32768')
            config += '\n#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1\n'
            (directory / 'sdkconfig.h').write_text(config)
            path, binary = directory / 'case.c', directory / 'case'
            path.write_text(code + MAIN)
            sources = [CORE / ('esp32_mquickjs_' + n + '.c') for n in
                       ['memory', 'memory_budget', 'memory_owner_accounting', 'memory_dma_accounting',
                        'future_runtime_resources']]
            sources.append(wifi_dir / 'esp32_mquickjs_wifi_runtime_resources.c')
            result = subprocess.run([compiler, '-std=c11', '-pthread', '-Wall', '-Wextra', '-Werror',
                                     '-Wno-unused-function', '-I' + temp,
                                     '-I' + str(ROOT / 'tests/c/support/memory_stubs'),
                                     '-I' + str(ROOT / 'components/esp32_mquickjs/internal'),
                                     str(path), *map(str, sources), '-o', str(binary)],
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)


SHIM = fixture_text('wireless/test_wireless_runtime_budget/shim.inc')
MAIN = fixture_text('wireless/test_wireless_runtime_budget/main.inc')
