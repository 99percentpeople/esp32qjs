"""Deferred production state readback, channel refresh overlap, decoding and VM GC."""
from tests.support.fixtures import fixture_text
import re
import tempfile
import unittest
from tests.c.integration.wifi.config.test_wifi_config_controls import sdk_types, structure
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import CORE, build, extract, run


def state_code(profile, ap=True):
    code = '#include <stdbool.h>\n#include <stdint.h>\n#include <stddef.h>\n#include <string.h>\n#include <assert.h>\n'
    code += f'#define CONFIG_SOC_WIFI_SUPPORT_5G {int(profile.startswith("esp32c5/"))}\n#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT {int(ap)}\n'
    code += sdk_types(profile, ('wifi_second_chan_t',))
    header = (COMPONENT / 'internal/esp32_mquickjs_wifi_radio.h').read_text()
    for name in ('esp32_mquickjs_wifi_radio_driver_state_t', 'esp32_mquickjs_wifi_driver_state_query_t'):
        code += re.search(r'typedef enum \{[^}]*\} ' + name + ';', header).group(0)
    radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
    code += re.search(r'typedef enum \{[^}]*\} esp32_mquickjs_wifi_radio_client_t;', header).group(0)
    broker = (COMPONENT / 'internal/esp32_mquickjs_wifi_promiscuous_broker.h').read_text()
    code += structure(broker, 'esp32_mquickjs_wifi_promiscuous_token_t')
    code += structure(radio, 'wifi_radio_live_lease_t')
    code += structure(header, 'esp32_mquickjs_wifi_driver_state_readback_t') + BOUNDARIES
    for name in ('wifi_radio_country_valid', 'esp32_mquickjs_wifi_radio_5ghz_channel_bit', 'wifi_radio_refresh_channel',
                 'wifi_radio_get_channel_locked', 'esp32_mquickjs_wifi_radio_read_state'):
        code += extract(radio, name)
    return code + RESET


class WiFiDriverStateRead(unittest.TestCase):
    def test_real_native_state_readback_partial_errors_and_refresh_overlap(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            for ap in (False, True):
                with self.subTest(profile=profile, ap=ap):
                    compile_run(self, state_code(profile, ap) + MAIN)

    def test_public_arity_decoding_original_error_and_moving_gc(self):
        code = state_code('esp32c5/representative')
        wifi = (COMPONENT / 'src/modules/wifi/esp32_mquickjs_wifi.c').read_text()
        driver = (COMPONENT / 'src/modules/wifi_driver/esp32_mquickjs_wifi_driver.c').read_text()
        code += extract(wifi, 'esp32_mquickjs_wifi_country_to_js')
        code += extract((CORE / 'esp32_mquickjs.c').read_text(), 'esp32_mquickjs_throw_native_error')
        for name in ('driver_read_error', 'driver_state_to_js', 'driver_read_state',
                     'js_wifi_driver_get_mode', 'js_wifi_driver_get_country',
                     'js_wifi_driver_get_channel', 'js_wifi_driver_get_home_channel'):
            code += extract(driver, name)
        with tempfile.TemporaryDirectory() as tmp:
            binary = build(tmp, code, VM_MAIN)
            for query in range(4):
                for scenario in ('success', 'sdk-error', 'arity', 'decode'):
                    run([str(binary), str(query), scenario])


BOUNDARIES = fixture_text('wifi/driver/test_wifi_driver_state_read/boundaries.inc')

RESET = fixture_text('wifi/driver/test_wifi_driver_state_read/reset.inc')

MAIN = fixture_text('wifi/driver/test_wifi_driver_state_read/main.inc')

VM_MAIN = fixture_text('wifi/driver/test_wifi_driver_state_read/vm_main.inc')
