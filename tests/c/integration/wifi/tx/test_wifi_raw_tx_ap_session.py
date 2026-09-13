"""Deferred actual Session runtime scheduling/AP cancellation and physical handoff."""
from tests.support.fixtures import fixture_text
import unittest
from tests.c.integration.wifi.tx.test_wifi_raw_tx_session import production_session_code, MAIN as SESSION_MAIN
from tests.support.native_compile import compile_run


class WiFiRawTxApSession(unittest.TestCase):
    def test_runtime_only_open_close_retry_and_physical_termination(self):
        code = production_session_code()
        code = code.replace('interface==WIFI_IF_STA && length==24',
                            '(interface==WIFI_IF_STA || interface==WIFI_IF_AP) && length==24')
        code += SESSION_MAIN[:SESSION_MAIN.index('int main(void)')]
        compile_run(self, code + MAIN)


MAIN = fixture_text('wifi/tx/test_wifi_raw_tx_ap_session/main.inc')
