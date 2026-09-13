"""Deferred tests of production antenna transactions; SDK/register boundaries injected.

No standalone transaction model. The actual GPIO ownership/write/rollback and
PHY callback code is extracted. Register helpers are the hardware boundary;
target compilation and later hardware tests cover those target-specific accesses.
"""
from tests.support.fixtures import fixture_text
import importlib.util
import re
import tempfile
import unittest

from tests.c.integration.wifi.config.test_wifi_config_controls import sdk_types
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import CORE, ROOT, build, extract, run

COMPONENT = ROOT / 'components/esp32_mquickjs'
SOURCE = COMPONENT / 'src/modules/wifi_driver/esp32_mquickjs_wifi_antenna.c'


def declaration(text, kind, name):
    return re.search(r'typedef ' + kind + r' \{[^}]*\} ' + name + ';', text).group(0) + '\n'


def types():
    source = SOURCE.read_text()
    header = (COMPONENT / 'internal/esp32_mquickjs_wifi_radio.h').read_text()
    result = sdk_types('esp32c5/representative', ('esp_phy_ant_config_t', 'esp_phy_ant_gpio_config_t'))
    result += declaration(header, 'struct', 'esp32_mquickjs_wifi_radio_config_result_t')
    result += declaration(header, 'union', 'esp32_mquickjs_wifi_antenna_snapshot_t')
    for name in ('antenna_route_t', 'antenna_pin_t', 'antenna_gpio_owner_t', 'antenna_transaction_t'):
        result += declaration(source, 'struct', name)
    return result


def native_source():
    source = SOURCE.read_text()
    prefix = '#include <assert.h>\n#include <stdbool.h>\n#include <stdint.h>\n#include <stdatomic.h>\n#include <stdlib.h>\n#include <string.h>\ntypedef int esp_err_t;\n'
    body = prefix + types() + BOUNDARIES
    for name in ('esp32_mquickjs_wifi_antenna_fault', 'esp32_mquickjs_wifi_antenna_valid',
                 'antenna_config_equal', 'antenna_gpio_equal', 'antenna_gpio_mask',
                 'antenna_route_equal', 'antenna_owned_pin', 'antenna_write_gpio',
                 'antenna_apply_idle', 'esp32_mquickjs_wifi_antenna_write'):
        body += extract(source, name)
    return body + RESET


class WiFiAntennaWrite(unittest.TestCase):
    def test_target_mux_snapshot_and_restore_preserve_adjacent_pads(self):
        source = SOURCE.read_text()
        for c5 in (False, True):
            with self.subTest(target='esp32c5' if c5 else 'esp32c3'):
                body = '#include <assert.h>\n#include <stdbool.h>\n#include <stdint.h>\n'
                body += f'#define CONFIG_IDF_TARGET_ESP32C5 {int(c5)}\n'
                body += declaration(source, 'struct', 'antenna_route_t')
                body += fixture_text('wifi/config/test_wifi_antenna_write/test_target_mux_snapshot_and_restore_preserve_adjacent_pads.inc')
                for name in ('antenna_route_read', 'antenna_route_equal', 'antenna_route_restore'):
                    body += extract(source, name)
                body += fixture_text('wifi/config/test_wifi_antenna_write/test_target_mux_snapshot_and_restore_preserve_adjacent_pads-02.inc')
                compile_run(self, body)

    def test_native_phy_lock_modem_ownership_readback_and_failed_rollback(self):
        compile_run(self, native_source() + CONFIG_MAIN)

    def test_gpio_claim_reorder_release_and_original_route_restoration(self):
        compile_run(self, native_source() + GPIO_MAIN)

    def test_gpio_prevalidation_allocation_partial_write_and_rollback_fault(self):
        compile_run(self, native_source() + GPIO_FAILURE_MAIN)

    def test_radio_zero_owner_admission_and_boot_fault_diagnostics(self):
        header = (COMPONENT / 'internal/esp32_mquickjs_wifi_radio.h').read_text()
        radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
        body = native_source() + declaration(header, 'enum', 'esp32_mquickjs_wifi_radio_driver_state_t')
        body += RADIO_BOUNDARY + extract(radio, 'esp32_mquickjs_wifi_radio_write_antenna') + RADIO_MAIN
        compile_run(self, body)

    def test_sdk_idle_callback_uses_real_phy_access_lock(self):
        path = ROOT / 'scripts/patch_idf_phy_antenna.py'
        spec = importlib.util.spec_from_file_location('antenna_patch_idle', path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        compile_run(self, fixture_text('wifi/config/test_wifi_antenna_write/test_sdk_idle_callback_uses_real_phy_access_lock-02.inc') + module.IDLE_TRANSACTION + fixture_text('wifi/config/test_wifi_antenna_write/test_sdk_idle_callback_uses_real_phy_access_lock.inc'))

    def test_public_capture_and_success_allocation_finish_before_native_write(self):
        source = SOURCE.read_text()
        driver = (COMPONENT / 'src/modules/wifi_driver/esp32_mquickjs_wifi_driver.c').read_text()
        options = (CORE / 'esp32_mquickjs_options.c').read_text().replace(
            '#include "esp32_mquickjs_options.h"',
            (COMPONENT / 'internal/esp32_mquickjs_options.h').read_text().replace(
                '#include "esp32_mquickjs_types.h"', ''))
        body = 'typedef int esp_err_t;\n' + types() + options + VM_BOUNDARY
        body += extract(source, 'esp32_mquickjs_wifi_antenna_valid')
        body += extract((CORE / 'esp32_mquickjs.c').read_text(), 'esp32_mquickjs_throw_native_error')
        for name in ('driver_antenna_to_js', 'driver_phy_write_error', 'driver_antenna_capture',
                     'driver_write_antenna', 'js_wifi_driver_set_antenna', 'js_wifi_driver_set_antenna_gpio'):
            body += extract(driver, name)
        with tempfile.TemporaryDirectory() as directory:
            binary = build(directory, body, VM_MAIN)
            config = '{rxMode:"auto",txMode:"ant1",rxDefault:"ant0",enabledAnt0:15,enabledAnt1:14}'
            gpios = '{gpios:[{selected:true,gpio:4},{selected:false,gpio:127},{selected:false,gpio:126},{selected:false,gpio:125}]}'
            for gpio, value in ((False, config), (True, gpios)):
                for failure in (False, True):
                    run([str(binary), '(' + value + ')', str(int(gpio)), '1', str(int(failure))])
            for value in ('{}', config.replace('15,', '15.5,'), config.replace('15,', '4294967311,'),
                          config.replace('rxMode:"auto"', 'rxMode:"ant0"').replace('txMode:"ant1"', 'txMode:"auto"'),
                          config[:-1] + ',unknown:1}'):
                run([str(binary), '(' + value + ')', '0', '0', '0'])
            for value in (gpios.replace('true', '1'), gpios.replace('gpio:127', 'gpio:128'), '{gpios:[]}', '{gpios:[null,null,null,null]}'):
                run([str(binary), '(' + value + ')', '1', '0', '0'])
            for count in ('0', '2'):
                run([str(binary), '(' + config + ')', '0', '0', '0', count])


BOUNDARIES = fixture_text('wifi/config/test_wifi_antenna_write/boundaries.inc')

RESET = fixture_text('wifi/config/test_wifi_antenna_write/reset.inc')

CONFIG_MAIN = fixture_text('wifi/config/test_wifi_antenna_write/config_main.inc')

GPIO_MAIN = fixture_text('wifi/config/test_wifi_antenna_write/gpio_main.inc')

GPIO_FAILURE_MAIN = fixture_text('wifi/config/test_wifi_antenna_write/gpio_failure_main.inc')

VM_BOUNDARY = fixture_text('wifi/config/test_wifi_antenna_write/vm_boundary.inc')

VM_MAIN = fixture_text('wifi/config/test_wifi_antenna_write/vm_main.inc')

RADIO_BOUNDARY = fixture_text('wifi/config/test_wifi_antenna_write/radio_boundary.inc')

RADIO_MAIN = fixture_text('wifi/config/test_wifi_antenna_write/radio_main.inc')
