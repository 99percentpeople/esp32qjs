"""Deferred production Mesh native owner tests; SDK/allocator boundaries injected.

Do not run during API implementation. The real owner and event-capture source
are compiled together when the consolidated Wi-Fi test phase begins.
"""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
import re
import unittest
from pathlib import Path
from tests.support.native_compile import compile_run

BASE = TEST_ROOT / 'components/esp32_mquickjs'


def source():
    clean = lambda text: re.sub(r'^\s*#(?:include[^\n]*|pragma once)\n', '', text, flags=re.M)
    main = (BASE / 'src/modules/wifi_mesh/esp32_mquickjs_wifi_mesh_sdk.c').read_text()
    events = (BASE / 'src/modules/wifi_mesh/esp32_mquickjs_wifi_mesh_events.inc').read_text()
    main = main.replace('#include "esp32_mquickjs_wifi_mesh_events.inc"', events)
    main = main.replace('#include "esp32_mquickjs_wifi_mesh_scan.inc"',
                        (BASE / 'src/modules/wifi_mesh/esp32_mquickjs_wifi_mesh_scan.inc').read_text())
    main = main.replace('#include "esp32_mquickjs_wifi_mesh_controls.inc"',
                        (BASE / 'src/modules/wifi_mesh/esp32_mquickjs_wifi_mesh_controls.inc').read_text())
    return PREFIX + clean((BASE / 'internal/esp32_mquickjs_wifi_mesh_feature.h').read_text()) + clean((BASE / 'internal/esp32_mquickjs_wifi_mesh_sdk.h').read_text()) + clean(main) + NATIVE


class WiFiMeshSdk(unittest.TestCase):
    def test_seal_during_send_defers_deinit_to_the_worker_cleanup_suffix(self):
        code = source().replace(
            'assert(esp32_mquickjs_wifi_mesh_sdk_close(&active)==ESP_ERR_TIMEOUT);',
            'assert(!esp32_mquickjs_wifi_mesh_sdk_seal(&active));assert(!deinits);')
        compile_run(self, code + fixture_text('wifi/mesh/test_wifi_mesh_sdk/test_seal_during_send_defers_deinit_to_the_worker_cleanup_suffix.inc'))

    def test_allocation_and_parameter_admission_before_native_mutation(self):
        compile_run(self, source() + fixture_text('wifi/mesh/test_wifi_mesh_sdk/test_allocation_and_parameter_admission_before_native_mutation.inc'))

    def test_native_failure_retains_identity_and_does_not_replay_cleanup(self):
        compile_run(self, source() + fixture_text('wifi/mesh/test_wifi_mesh_sdk/test_native_failure_retains_identity_and_does_not_replay_cleanup.inc'))

    def test_partial_init_failure_is_not_hidden_by_noop_sdk_deinit(self):
        compile_run(self, source() + fixture_text('wifi/mesh/test_wifi_mesh_sdk/test_partial_init_failure_is_not_hidden_by_noop_sdk_deinit.inc'))

    def test_full_observation_queue_keeps_native_control_and_exact_commit(self):
        compile_run(self, source() + fixture_text('wifi/mesh/test_wifi_mesh_sdk/test_full_observation_queue_keeps_native_control_and_exact_commit.inc'))

    def test_receive_retains_two_lanes_and_conversion_failure_does_not_consume(self):
        compile_run(self, source() + fixture_text('wifi/mesh/test_wifi_mesh_sdk/test_receive_retains_two_lanes_and_conversion_failure_does_not_consume.inc'))

    def test_close_during_native_send_keeps_span_until_native_return(self):
        compile_run(self, source() + fixture_text('wifi/mesh/test_wifi_mesh_sdk/test_close_during_native_send_keeps_span_until_native_return.inc'))


PREFIX = fixture_text('wifi/mesh/test_wifi_mesh_sdk/prefix.inc')

NATIVE = fixture_text('wifi/mesh/test_wifi_mesh_sdk/native.inc')

PREFIX += fixture_text('wifi/mesh/test_wifi_mesh_sdk/fragment.inc')

NATIVE += fixture_text('wifi/mesh/test_wifi_mesh_sdk/fragment-02.inc')

PREFIX += fixture_text('wifi/mesh/test_wifi_mesh_sdk/fragment-03.inc')
NATIVE += fixture_text('wifi/mesh/test_wifi_mesh_sdk/fragment-04.inc')
