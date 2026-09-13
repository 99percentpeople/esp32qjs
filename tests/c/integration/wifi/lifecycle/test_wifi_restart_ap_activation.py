"""Deferred production AP-only CSA then STA activation; SDK/scheduler boundaries.

The production event barrier is exercised separately by band_cycle_events.
Native CSA/association behavior still needs RF verification on each target.
"""
from tests.support.fixtures import fixture_text
import re
import unittest
from tests.support.wireless_vm_fixture import extract
from tests.c.integration.wifi.lifecycle.test_wifi_restart_configs import config_code, MAIN as CONFIG_MAIN
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.support.native_compile import compile_run

CLOCKS = fixture_text('wifi/lifecycle/test_wifi_restart_ap_activation/clocks.inc')

BOUNDARIES = fixture_text('wifi/lifecycle/test_wifi_restart_ap_activation/boundaries.inc')

MAIN = fixture_text('wifi/lifecycle/test_wifi_restart_ap_activation/main.inc')


def activation_code(profile):
    code = config_code(profile, True)
    # SDK adapters preserve their faults while modeling asynchronous channel
    # completion and the AP-only -> APSTA mode transition, without AP restart.
    code = re.sub(r'static int wifi_radio_begin_events\([^)]*\) \{[\s\S]*?\n\}', fixture_text('wifi/lifecycle/test_wifi_restart_ap_activation/activation_code-code-03.inc').replace('native_mode==WIFI_MODE_AP', 's_radio.effective_mode==WIFI_MODE_AP'), code, count=1)
    code = code.replace('for(unsigned i=0;i<WIFI_RADIO_MAX_LEASES;++i)assert(!s_radio.leases[i].identity);\n    native_running=true;',
                        'if(native_mode==WIFI_MODE_AP){native_channel=native_home=native_config[1].ap.channel;native_band=WIFI_BAND_2G;}\n    native_running=true;')
    code = code.replace('static int esp_wifi_set_mode(wifi_mode_t mode) {assert(!s_radio.started);native_mode=mode;return sdk_step(true);}', fixture_text('wifi/lifecycle/test_wifi_restart_ap_activation/activation_code-code.inc'))
    code = re.sub(r'static int esp_wifi_set_channel\([^)]*\) \{[\s\S]*?\n\}', fixture_text('wifi/lifecycle/test_wifi_restart_ap_activation/activation_code-code-02.inc'), code, count=1)
    radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
    code = CLOCKS + code + BOUNDARIES
    for name in ('wifi_radio_restart_ap_channel_matches', 'wifi_radio_restart_ap_channel_wait_inner',
                 'wifi_radio_restart_ap_channel_wait', 'wifi_radio_restart_configs_start_locked'):
        code += extract(radio, name)
    return code


class WiFiRestartApActivation(unittest.TestCase):
    def test_ap_csa_then_station_start_faults_deadline_and_stale_token(self):
        helpers = CONFIG_MAIN[:CONFIG_MAIN.index('static void disabled_pmf_replay')]
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            with self.subTest(profile=profile):
                compile_run(self, activation_code(profile) + helpers + MAIN)
