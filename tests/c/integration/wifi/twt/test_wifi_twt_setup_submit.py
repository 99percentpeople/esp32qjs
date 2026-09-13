"""Deferred production setup submit/registry/native-wrapper handoff.

The public SDK preflight/dispatch and original native mutation are controlled
boundaries. This does not claim SDK semaphore, RF or retirement execution.
Only AST is allowed before the Wi-Fi API stage.
"""
from tests.support.fixtures import fixture_text
from pathlib import Path
import re
import unittest
from tests.c.integration.wifi.config.test_wifi_config_controls import structure
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit
from tests.c.integration.wifi.twt.test_wifi_twt_setup_result import PRELUDE
from tests.c.integration.wifi.twt.test_wifi_twt_broadcast_event import broadcast_types
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import extract


class WiFiTwtSetupSubmit(unittest.TestCase):
    def test_public_sdk_preservation_exact_identity_early_event_and_uncertain_return(self):
        sdk = Path('/home/zach/esp/esp-idf/components/esp_wifi/include/esp_wifi_he_types.h').read_text()
        twt = COMPONENT / 'src/modules/wifi_twt'
        code = PRELUDE + fixture_text('wifi/twt/test_wifi_twt_setup_submit/test_public_sdk_preservation_exact_identity_early_event_and_uncertain_return-code.inc')
        for name in ('wifi_twt_setup_cmds_t', 'wifi_itwt_probe_status_t'):
            code += re.search(r'typedef enum \{[^}]*\} ' + name + ';', sdk).group(0)
        code += structure(sdk, 'wifi_twt_setup_config_t')
        code += '\ntypedef wifi_twt_setup_config_t wifi_itwt_setup_config_t;\n'
        for name in ('wifi_btwt_setup_config_t', 'esp_wifi_btwt_info_t', 'wifi_event_sta_itwt_setup_t', 'wifi_event_sta_itwt_probe_t'):
            code += structure(sdk, name)
        code += broadcast_types(commands=False)
        for name in ('options', 'tx', 'probe_timer', 'probe_result', 'probe_wake', 'setup_timer',
                     'setup_result', 'setup_submit', 'teardown_tx', 'information_timer', 'broadcast_timer', 'sdk'):
            code += unit(COMPONENT / f'internal/esp32_mquickjs_wifi_twt_{name}.h')
        code += BOUNDARIES
        code += re.search(r'typedef enum \{[^}]*\} wifi_itwt_teardown_status_t;', sdk).group(0)
        code += structure(sdk, 'wifi_event_sta_itwt_teardown_t')
        code += unit(twt / 'esp32_mquickjs_wifi_twt_options.c').split('#define READ(')[0] + '\n#endif\n'
        for name in ('setup_result', 'setup_submit'):
            code += unit(twt / f'esp32_mquickjs_wifi_twt_{name}.c')
        native = (twt / 'esp32_mquickjs_wifi_twt_sdk.c').read_text()
        for name in ('twt_individual_setup_process', '__wrap_wifi_sta_itwt_setup_process'):
            code += extract(native, name)
        compile_run(self, code + MAIN)


BOUNDARIES = fixture_text('wifi/twt/test_wifi_twt_setup_submit/boundaries.inc')
MAIN = fixture_text('wifi/twt/test_wifi_twt_setup_submit/main.inc')
