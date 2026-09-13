"""Real pump + Session + broker: progress with no JS/runtime service calls."""
import unittest
from tests.support.fixtures import fixture_text
from tests.support.native_compile import compile_run
from tests.c.integration.wifi.tx.test_wifi_raw_tx_session import production_session_code, MAIN, RAW
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit


class WiFiRawTxPump(unittest.TestCase):
    def test_native_progress_saturation_idle_and_runtime_replacement(self):
        code = production_session_code(identity=True)
        code = code.replace('void esp32_mquickjs_wifi_raw_tx_pump_wake(void) {}',
                            'void esp32_mquickjs_wifi_raw_tx_pump_wake(void);')
        code = code.replace('esp_err_t esp32_mquickjs_wifi_raw_tx_pump_init(void) {return ESP_OK;}',
                            'esp_err_t esp32_mquickjs_wifi_raw_tx_pump_init(void);')
        code += fixture_text('wifi/tx/test_wifi_raw_tx_pump/timer.inc')
        code += unit(RAW/'esp32_mquickjs_wifi_raw_tx_pump.c')
        code += MAIN[:MAIN.index('int main(void)')]
        compile_run(self, code + fixture_text('wifi/tx/test_wifi_raw_tx_pump/main.inc'))
