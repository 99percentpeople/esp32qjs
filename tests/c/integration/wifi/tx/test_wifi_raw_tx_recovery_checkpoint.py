"""Deferred real Raw TX predecessor capture, shared credential checkpoint/replay.

Radio owner admission is injected; production rate validation/capture/replay and
shared config checkpoint execute unchanged. No import/compile/run in API phase.
"""
from tests.support.fixtures import fixture_text
import unittest
from tests.c.integration.wifi.lifecycle.test_wifi_restart_configs import config_code, MAIN as CONFIG_MAIN
from tests.support.native_compile import compile_run


class WiFiRawTxRecoveryCheckpoint(unittest.TestCase):
    def test_predecessor_is_frozen_and_replayed_without_changing_live_temporary_rate(self):
        for profile in ('esp32c3/representative','esp32s3/representative-psram','esp32c5/representative'):
            for ap in (False,True):
                with self.subTest(profile=profile,ap=ap):
                    code=config_code(profile,ap,mutation_boundary=True)
                    code+=CONFIG_MAIN[:CONFIG_MAIN.index('int main(void)')]
                    compile_run(self,code+MAIN)


MAIN = fixture_text('wifi/tx/test_wifi_raw_tx_recovery_checkpoint/main.inc')
