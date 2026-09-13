"""Deferred production registry and deadline-result regressions; AST only now."""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
import re
import unittest
from pathlib import Path
from tests.support.native_compile import compile_run


SOURCE = TEST_ROOT / 'components/esp32_mquickjs/src/core/esp32_mquickjs_future.c'


def extract(text, name):
    match = re.search(r'^[^\n;{}]*\b' + name + r'\s*\([^;{}]*\)\s*\{', text, re.M)
    if not match:
        raise AssertionError(name)
    depth, end = 1, match.end()
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[match.start():end] + '\n'


def registry_source():
    text = SOURCE.read_text()
    constants = '\n'.join(re.findall(r'^#define ESP32_MQUICKJS_FUTURE_(?:MAX_DRIVERS|DRIVER_CHUNK_SIZE) .*$', text, re.M))
    entry = re.search(r'typedef struct \{\s*JSGCRef function;.*?\} future_driver_entry_t;', text, re.S).group(0)
    chunk = re.search(r'typedef struct future_driver_chunk \{.*?\} future_driver_chunk_t;', text, re.S).group(0)
    return (COMMON + constants + '\n' + entry + chunk + REGISTRY_BOUNDARIES +
            ''.join(extract(text, name) for name in ('future_find_driver', 'future_clear_driver_registry',
                'esp32_mquickjs_future_register_driver')))


class FutureDriverRegistry(unittest.TestCase):
    def test_more_than_128_methods_keep_existing_roots_at_stable_addresses(self):
        compile_run(self, registry_source() + fixture_text('runtime/test_future_driver_registry/test_more_than_128_methods_keep_existing_roots_at_stable_addresses.inc'))

    def test_chunk_oom_preserves_registered_methods_and_retries_only_allocation(self):
        compile_run(self, registry_source() + fixture_text('runtime/test_future_driver_registry/test_chunk_oom_preserves_registered_methods_and_retries_only_allocation.inc'))

    def test_exact_capacity_and_shutdown_reject_new_entries_without_allocation(self):
        compile_run(self, registry_source() + fixture_text('runtime/test_future_driver_registry/test_exact_capacity_and_shutdown_reject_new_entries_without_allocation.inc'))


class FutureDeadlineResult(unittest.TestCase):
    def source(self):
        return COMMON + DEADLINE_BOUNDARIES + extract(SOURCE.read_text(), 'future_expire_deadlines')

    def test_empty_receive_timeout_fulfils_null_and_preserves_native_cancellation(self):
        compile_run(self, self.source() + fixture_text('runtime/test_future_driver_registry/test_empty_receive_timeout_fulfils_null_and_preserves_native_cancellation.inc'))

    def test_custom_success_and_error_are_rooted_during_cancellation(self):
        compile_run(self, self.source() + fixture_text('runtime/test_future_driver_registry/test_custom_success_and_error_are_rooted_during_cancellation.inc'))


COMMON = fixture_text('runtime/test_future_driver_registry/common.inc')

REGISTRY_BOUNDARIES = fixture_text('runtime/test_future_driver_registry/registry_boundaries.inc')

DEADLINE_BOUNDARIES = fixture_text('runtime/test_future_driver_registry/deadline_boundaries.inc')
