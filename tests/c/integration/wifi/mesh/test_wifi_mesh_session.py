"""Deferred Mesh production Session scheduling and ownership regressions.

The real SDK/config and Session sources are included. Radio calls, time and
worker dispatch are injected boundaries; no independent Session state machine.
Run only in the consolidated Wi-Fi phase, not during API implementation.
"""
from tests.support.fixtures import fixture_text
import re
import unittest
from tests.c.integration.wifi.mesh.test_wifi_mesh_sdk import BASE, source as sdk_source
from tests.support.native_compile import compile_run


def source():
    clean = lambda text: re.sub(r'^\s*#(?:include[^\n]*|pragma once)\n', '', text, flags=re.M)
    return (sdk_source() + TYPES +
            clean((BASE / 'internal/esp32_mquickjs_wifi_mesh_radio.h').read_text()) +
            clean((BASE / 'internal/esp32_mquickjs_wifi_mesh_session.h').read_text()) +
            clean((BASE / 'src/modules/wifi_mesh/esp32_mquickjs_wifi_mesh_session.c').read_text()) + NATIVE)


class WiFiMeshSession(unittest.TestCase):
    def test_command_arriving_during_poll_is_retired_before_parent_close(self):
        compile_run(self, source() + fixture_text('wifi/mesh/test_wifi_mesh_session/test_command_arriving_during_poll_is_retired_before_parent_close.inc'))

    def test_late_native_return_preserves_original_error_and_marks_deadline(self):
        compile_run(self, source() + fixture_text('wifi/mesh/test_wifi_mesh_session/test_late_native_return_preserves_original_error_and_marks_deadline.inc'))

    def test_cancel_and_runtime_teardown_keep_dispatched_storage_until_return(self):
        compile_run(self, source() + fixture_text('wifi/mesh/test_wifi_mesh_session/test_cancel_and_runtime_teardown_keep_dispatched_storage_until_return.inc'))

    def test_close_before_first_worker_scrubs_configuration_without_radio_start(self):
        compile_run(self, source() + fixture_text('wifi/mesh/test_wifi_mesh_session/test_close_before_first_worker_scrubs_configuration_without_radio_start.inc'))

    def test_read_claims_require_exact_identity_and_do_not_wrap(self):
        compile_run(self, source() + fixture_text('wifi/mesh/test_wifi_mesh_session/test_read_claims_require_exact_identity_and_do_not_wrap.inc'))


TYPES = fixture_text('wifi/mesh/test_wifi_mesh_session/types.inc')

NATIVE = fixture_text('wifi/mesh/test_wifi_mesh_session/native.inc')

NATIVE += fixture_text('wifi/mesh/test_wifi_mesh_session/fragment.inc')
