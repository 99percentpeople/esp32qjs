"""Deferred production ByteView/Source ownership with the complete memory manager.

Only JS/heap/lock boundaries are replaced. Moving GC and setters use the real VM
in test_wireless_byte_storage_gc; this fixture checks actual budget lifetime.
"""
from tests.support.fixtures import fixture_text
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
from tests.support.wireless_vm_fixture import ROOT, CORE, extract


class ByteSourceBudget(unittest.TestCase):
    def test_control_reserve_failed_transfer_read_retention_and_final_release(self):
        compiler = shutil.which('cc')
        if compiler is None:
            self.skipTest('C compiler unavailable')
        source = (CORE / 'esp32_mquickjs_byte_source.c').read_text()
        heap = (ROOT / 'tests/c/unit/memory/test_memory_wireless.c').read_text().split('int main(void)')[0]
        structs = source[source.index('#define ESP32_MQUICKJS_BYTE_SPAN_SOURCE_OWNER_KEY'):
                         source.index('static void *byte_source_allocate')]
        code = heap + SHIM + structs
        code += (CORE / 'esp32_mquickjs_byte_span.c').read_text()
        for name in [
            'byte_source_allocate', 'leased_byte_span_source_next', 'leased_byte_span_source_close',
            'byte_view_from_value', 'byte_span_source_from_value', 'byte_view_release', 'byte_view_make',
            'esp32_mquickjs_open_byte_span_source', 'esp32_mquickjs_new_wireless_byte_span_source',
            'esp32_mquickjs_new_wireless_owned_byte_view', 'esp32_mquickjs_new_wireless_retained_byte_view',
            'esp32_mquickjs_new_byte_span_source', 'esp32_mquickjs_new_owned_byte_view',
            'esp32_mquickjs_byte_view_acquire_read', 'esp32_mquickjs_byte_view_release_read',
            'js_byte_view_close', 'js_byte_span_source_close',
            'js_byte_view_finalizer', 'js_byte_span_source_finalizer',
        ]:
            code += extract(source, name)
        with tempfile.TemporaryDirectory() as temp:
            path, binary = Path(temp) / 'case.c', Path(temp) / 'case'
            for name in ('mquickjs.h', 'esp32_mquickjs_types.h'):
                (Path(temp) / name).write_text('#pragma once\n')
            path.write_text(code + MAIN)
            sources = [CORE / ('esp32_mquickjs_' + n + '.c') for n in
                       ['memory', 'memory_budget', 'memory_owner_accounting', 'memory_dma_accounting']]
            result = subprocess.run([compiler, '-std=c11', '-pthread', '-Wall', '-Wextra', '-Werror',
                                     '-Wno-unused-function', '-I' + temp,
                                     '-I' + str(ROOT / 'tests/c/support/memory_stubs'),
                                     '-I' + str(ROOT / 'components/esp32_mquickjs/internal'),
                                     str(path), *map(str, sources), '-o', str(binary)],
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)


SHIM = fixture_text('memory/test_byte_source_budget/shim.inc')
MAIN = fixture_text('memory/test_byte_source_budget/main.inc')
