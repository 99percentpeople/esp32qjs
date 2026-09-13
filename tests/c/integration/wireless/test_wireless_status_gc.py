"""Real VM failures and moving GC in Wi-Fi scan/status and ESP-NOW status."""
from tests.support.fixtures import fixture_text
import pathlib
import re
import sys
import tempfile
import unittest
from tests.support.wireless_vm_fixture import ROOT, CORE, build, extract, run
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit
from tests.support.wifi_connection_counter_fixture import connection_counter_code
from tests.c.integration.wifi.config.test_wifi_config_controls import HEADER, structure
SDK=fixture_text('wireless/test_wireless_status_gc/sdk.inc')
MAIN=fixture_text('wireless/test_wireless_status_gc/main.inc')
class WirelessStatusGc(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory();cls.addClassCleanup(cls.temp.cleanup)
        wifi=(ROOT/'components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi.c').read_text()
        espnow=(ROOT/'components/esp32_mquickjs/src/modules/espnow/esp32_mquickjs_espnow.c').read_text()
        status=extract(espnow,'espnow_status_to_js')
        # Only SDK/native snapshot fields are fixtures; all object creation,
        # rooting and exception propagation below is extracted production code.
        fields=set(re.findall(r'session->(\w+)',status))-{'broadcast_rate_config','interval_token'}
        native='typedef struct { struct {unsigned identity;} interval_token;espnow_peer_rate_config_t broadcast_rate_config;'+''.join('atomic_uint '+n+';' for n in sorted(fields))+'} espnow_session_t;\n'
        native+='static int espnow_channel_admit(espnow_session_t *s,bool refresh) { (void)s;(void)refresh;return 0; }\n'
        native+='static void espnow_tx_queue_depth_locked(espnow_session_t *s,uint32_t *a,uint32_t *b) { (void)s;*a=2;*b=3; }\n'
        radio=(ROOT/'components/esp32_mquickjs/src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
        interval_header=(ROOT/'components/esp32_mquickjs/internal/esp32_mquickjs_wifi_interval.h').read_text()
        native+=re.search(r'typedef struct \{[^}]*\} esp32_mquickjs_wifi_interval_token_t;',interval_header).group(0)
        native+=re.search(r'typedef struct \{[^}]*\} esp32_mquickjs_wifi_interval_state_t;',interval_header).group(0)
        native+='static void esp32_mquickjs_wifi_radio_interval_status(esp32_mquickjs_wifi_interval_state_t *s) {*s=(esp32_mquickjs_wifi_interval_state_t){.generation=7,.revision=9,.known=true,.value=300};}\n'
        native+='\n#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1\n'
        native+=unit(ROOT/'components/esp32_mquickjs/internal/esp32_mquickjs_wifi_policy.h')
        native+='static void esp32_mquickjs_wifi_radio_policy_status(esp32_mquickjs_wifi_policy_state_t *s) {*s=(esp32_mquickjs_wifi_policy_state_t){.revision=2};s->records[0]=(esp32_mquickjs_wifi_policy_record_t){.generation=7,.revision=2,.accepted_revision=1,.configured=true,.uncertain=true,.requested=true,.value=false,.error=77};}\n'
        driver=(ROOT/'components/esp32_mquickjs/src/modules/wifi_driver/esp32_mquickjs_wifi_driver.c').read_text()
        config=(ROOT/'components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi_config.c').read_text()
        bodies='#include "cutils.h"\n'+extract(config,'esp32_mquickjs_wifi_ssid_text')+extract(config,'esp32_mquickjs_wifi_set_ssid_properties')
        bodies+=extract(driver,'esp32_mquickjs_wifi_policies_to_js')
        bodies+=extract(driver,'esp32_mquickjs_wifi_interval_to_js')
        bodies+=extract(driver,'esp32_mquickjs_wifi_rssi_request_to_js')
        bodies+=extract(radio,'esp32_mquickjs_wifi_radio_5ghz_channel_bit')
        bodies+='\n'.join(extract(wifi,n) for n in ['wifi_negotiated_phy_name','wifi_link_snapshot_valid','wifi_set_link_properties','wifi_make_configuration_status','wifi_cipher_name','esp32_mquickjs_wifi_country_to_js','wifi_scan_protocols_to_js','wifi_scan_capabilities_to_js','wifi_make_radio_status','js_wifi_driver_status','wifi_make_status_object','wifi_make_scan_entry_object','wifi_make_scan_results_array'])
        bodies+=extract((CORE/'esp32_mquickjs_wireless_core.c').read_text(),'esp32_mquickjs_wireless_format_address')
        bodies+='\n'.join(extract(espnow,n) for n in ['espnow_format_address','espnow_peer_rate_config_to_js','espnow_peer_status_to_js'])+status
        counters='typedef int portMUX_TYPE;\n#define portMUX_INITIALIZER_UNLOCKED 0\n'+connection_counter_code(converter=True)
        core_types=HEADER.with_name('esp32_mquickjs_types.h').read_text()
        snapshot_types=''.join(structure(core_types,n) for n in (
            'esp32_mquickjs_wifi_link_snapshot_t','esp32_mquickjs_wifi_status_t'))
        snapshot_types+=''.join(structure(HEADER.read_text(),n) for n in (
            'esp32_mquickjs_wifi_radio_config_result_t','esp32_mquickjs_wifi_link_sample_t',
            'esp32_mquickjs_wifi_rssi_request_t'))
        cls.binary=build(cls.temp.name,SDK.replace('/* SNAPSHOT_TYPES */',snapshot_types)+native+counters+bodies,MAIN)

    def test_status_and_scan_allocation_failure_and_moving_gc(self):
        for mode in range(5):
            for gc in (0,1):
                with self.subTest(mode=mode,gc=gc):run([str(self.binary),str(mode),str(gc),'0'])

    def test_scan_driver_failure_cleanup(self):
        for failure in (1,2):run([str(self.binary),'1','1',str(failure)])
