"""Deferred USD production lifecycle/timer fixtures; no execution on import."""
from tests.support.fixtures import fixture_text
import re
import unittest

from tests.c.integration.wifi.nan.test_idf_nan_control import BASE, NanNativeControl


class NanUsdSdkLifecycle(unittest.TestCase):
    compile_run = NanNativeControl.compile_run

    def production(self):
        header = (BASE / 'internal/esp32_mquickjs_wifi_nan_usd_sdk.h').read_text()
        header = re.sub(r'^#(?:include[^\n]*|pragma once)\n', '', header, flags=re.M)
        module = BASE / 'src/modules/wifi_nan'
        tx = (BASE / 'internal/esp32_mquickjs_wifi_nan_tx.h').read_text()
        tx = re.sub(r'^#(?:include[^\n]*|pragma once)\n', '', tx, flags=re.M)
        return (PREFIX + tx + header + BOUNDARY +
                (module / 'esp32_mquickjs_wifi_nan_usd_state.inc').read_text() +
                (module / 'esp32_mquickjs_wifi_nan_usd_lifecycle.inc').read_text())

    def test_start_failures_preserve_owner_and_close_retries_only_the_suffix(self):
        self.compile_run(self.production() + fixture_text('wifi/nan/test_wifi_nan_usd_sdk/test_start_failures_preserve_owner_and_close_retries_only_the_suffix.inc'))

    def test_native_buffer_blocks_engine_free_and_handler_retirement(self):
        self.compile_run(self.production() + fixture_text('wifi/nan/test_wifi_nan_usd_sdk/test_native_buffer_blocks_engine_free_and_handler_retirement.inc'))

    def test_dispatched_timer_cannot_enter_reused_engine_and_exhaustion_does_not_wrap(self):
        self.compile_run(self.production() + fixture_text('wifi/nan/test_wifi_nan_usd_sdk/test_dispatched_timer_cannot_enter_reused_engine_and_exhaustion_does_not_wrap.inc'))

    def test_full_observation_queue_never_blocks_or_changes_engine_lifecycle(self):
        self.compile_run(self.production() + fixture_text('wifi/nan/test_wifi_nan_usd_sdk/test_full_observation_queue_never_blocks_or_changes_engine_lifecycle.inc'))

    def test_command_wait_never_holds_engine_mutex_and_stale_ticket_cannot_mutate(self):
        source = (BASE / 'src/modules/wifi_nan/esp32_mquickjs_wifi_nan_usd_commands.inc').read_text()
        self.compile_run(self.production() + COMMAND_BOUNDARY + source + fixture_text('wifi/nan/test_wifi_nan_usd_sdk/test_command_wait_never_holds_engine_mutex_and_stale_ticket_cannot_mutate.inc'))


PREFIX = fixture_text('wifi/nan/test_wifi_nan_usd_sdk/prefix.inc')

BOUNDARY = fixture_text('wifi/nan/test_wifi_nan_usd_sdk/boundary.inc')

COMMAND_BOUNDARY = fixture_text('wifi/nan/test_wifi_nan_usd_sdk/command_boundary.inc')
