"""Deferred TWT SDK snapshot/admission using production wrappers and validators.

Native table bytes and forwarding calls are controlled boundaries. Host pointer
width is substituted only at ioctl transport/ETSTimer declarations; C5 archive
layout, static assertions and actual linker wrapping require target evidence.
No fake SDK retirement oracle: a snapshot has no quiescent/retired result.
"""
from tests.support.fixtures import fixture_text
from pathlib import Path
import re
import unittest
from tests.c.integration.wifi.tx.test_wifi_action_lane import PRELUDE
from tests.c.integration.wifi.twt.test_wifi_twt_broadcast_event import broadcast_types
from tests.c.integration.wifi.tx.test_wifi_action_sdk import BOUNDARIES as ACTION_BOUNDARIES
from tests.c.integration.wifi.config.test_wifi_config_controls import sdk_types, structure
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import extract


class WiFiTwtSdk(unittest.TestCase):
    def test_tables_pending_capacity_flow_selection_and_private_dispatch(self):
        sdk = Path('/home/zach/esp/esp-idf/components/esp_wifi/include/esp_wifi_he_types.h').read_text()
        action = (COMPONENT / 'src/modules/wifi_action/esp32_mquickjs_wifi_action_sdk.c').read_text()
        twt = COMPONENT / 'src/modules/wifi_twt'
        code = PRELUDE + fixture_text('wifi/twt/test_wifi_twt_sdk/test_tables_pending_capacity_flow_selection_and_private_dispatch-code.inc')
        code += re.search(r'typedef enum \{[^}]*\} wifi_twt_setup_cmds_t;', sdk).group(0)
        code += structure(sdk, 'wifi_twt_config_t')
        code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_twt_controls.h')
        code += structure(sdk, 'wifi_twt_setup_config_t')
        code += '\ntypedef wifi_twt_setup_config_t wifi_itwt_setup_config_t;\n'
        code += structure(sdk, 'wifi_btwt_setup_config_t')
        code += structure(sdk, 'esp_wifi_btwt_info_t')
        code += re.search(r'typedef enum \{[^}]*\} wifi_itwt_probe_status_t;', sdk).group(0)
        code += structure(sdk, 'wifi_event_sta_itwt_probe_t')
        code += structure(sdk, 'wifi_event_sta_itwt_setup_t')
        code += broadcast_types(commands=False)
        code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_twt_options.h')
        code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_twt_tx.h')
        code += structure(sdk, 'wifi_event_sta_itwt_suspend_t')
        code += '#define WIFI_EVENT_ITWT_SUSPEND 31\n'
        code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_twt_probe_timer.h')
        code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_twt_probe_result.h')
        code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_twt_probe_wake.h')
        code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_twt_setup_timer.h')
        code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_twt_setup_result.h')
        code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_twt_teardown_tx.h')
        code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_twt_information_timer.h')
        code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_twt_information.h')
        code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_twt_broadcast_timer.h')
        code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_twt_sdk.h')
        # Entire production native validators, excluding the unrelated JS parser.
        code += unit(twt / 'esp32_mquickjs_wifi_twt_options.c').split('#define READ(')[0] + '\n#endif\n'
        code += sdk_types('esp32c5/representative', ('wifi_action_tx_req_t', 'wifi_roc_req_t'))
        code += ACTION_BOUNDARIES
        code += structure(action, 'action_sdk_twt_request_t')
        code += structure(action, 'action_sdk_twt_broadcast_request_t')
        code += '#define ACTION_SDK_TWT_BROADCAST_SNAPSHOT_TYPE ((wifi_action_tx_t)(INT32_MAX - 8))\n'
        code += structure(action, 'action_sdk_twt_probe_cancel_request_t')
        code += structure(action, 'action_sdk_twt_probe_control_t')
        code += structure(action, 'action_sdk_twt_setup_control_t')
        code += structure(action, 'action_sdk_btwt_control_t')
        code += '#define ACTION_SDK_BTWT_CONTROL_TYPE ((wifi_action_tx_t)(INT32_MAX - 9))\nenum { BTWT_CANCEL, BTWT_QUIESCENT, BTWT_RELEASE, BTWT_TEARDOWN };\n'

        code += '#define ACTION_SDK_TWT_SETUP_CONTROL_TYPE ((wifi_action_tx_t)(INT32_MAX - 7))\n'
        code += 'enum { TWT_SETUP_QUIESCENT, TWT_SETUP_RELEASE, TWT_SETUP_TEARDOWN, TWT_TEARDOWN_TX_QUIESCENT, TWT_TEARDOWN_TX_RELEASE, TWT_INFORMATION_SUBMIT, TWT_INFORMATION_REAP, TWT_INFORMATION_RESUME };\n'
        code += '#define ACTION_SDK_TWT_PROBE_CONTROL_TYPE ((wifi_action_tx_t)(INT32_MAX - 5))\n'
        code += 'enum { TWT_PROBE_SUBMIT, TWT_PROBE_QUIESCENT, TWT_PROBE_RELEASE };\n'
        code += '#define ACTION_SDK_TWT_SNAPSHOT_TYPE ((wifi_action_tx_t)(INT32_MAX - 3))\n'
        code += '#define ACTION_SDK_TWT_PROBE_CANCEL_TYPE ((wifi_action_tx_t)(INT32_MAX - 4))\n'
        code += '#define ACTION_SDK_TWT_SETUP_CANCEL_TYPE ((wifi_action_tx_t)(INT32_MAX - 6))\n'
        code += BOUNDARIES
        code += unit(twt / 'esp32_mquickjs_wifi_twt_probe_result.c')
        code += unit(twt / 'esp32_mquickjs_wifi_twt_sdk.c')
        for name in ('esp32_mquickjs_wifi_action_receive', 'action_sdk_owner', 'action_sdk_guarded_callback',
                     '__wrap_wifi_action_tx_process', 'esp32_mquickjs_wifi_twt_sdk_broadcast_teardown', 'esp32_mquickjs_wifi_twt_sdk_broadcast_cancel', 'esp32_mquickjs_wifi_twt_sdk_broadcast_quiescent', 'esp32_mquickjs_wifi_twt_sdk_broadcast_release', 'esp32_mquickjs_wifi_twt_sdk_snapshot',
                     'esp32_mquickjs_wifi_twt_sdk_probe_cancel', 'esp32_mquickjs_wifi_twt_sdk_setup_cancel', 'action_sdk_probe_control',
                     'esp32_mquickjs_wifi_twt_sdk_probe_submit', 'esp32_mquickjs_wifi_twt_sdk_probe_quiescent',
                     'esp32_mquickjs_wifi_twt_sdk_probe_release', 'esp32_mquickjs_wifi_twt_sdk_setup_quiescent',
                     'esp32_mquickjs_wifi_twt_sdk_setup_release','esp32_mquickjs_wifi_twt_sdk_setup_teardown', 'esp32_mquickjs_wifi_twt_sdk_teardown_tx_quiescent',
                     'esp32_mquickjs_wifi_twt_sdk_teardown_tx_release', 'esp32_mquickjs_wifi_twt_sdk_information_submit',
                     'esp32_mquickjs_wifi_twt_sdk_information_reap', 'esp32_mquickjs_wifi_twt_sdk_broadcast_snapshot'):
            code += extract(action, name)
        compile_run(self, code + MAIN)


BOUNDARIES = fixture_text('wifi/twt/test_wifi_twt_sdk/boundaries.inc')
MAIN = fixture_text('wifi/twt/test_wifi_twt_sdk/main.inc')

# Result storage and frame transmission are SDK boundaries in this fixture;
# actual result/TX/timer scheduling have their own production-source fixtures.
BOUNDARIES += fixture_text('wifi/twt/test_wifi_twt_sdk/fragment.inc')
