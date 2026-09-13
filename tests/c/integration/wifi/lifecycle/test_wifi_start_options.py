"""Production start capture, atomic resolution and shared executor; phase run deferred."""
from tests.support.fixtures import fixture_text
import re
import tempfile
import unittest
import tests.c.integration.wifi.config.test_wifi_configuration_selection as selection_fixture
import tests.c.integration.wifi.config.test_wifi_configuration_executor as executor_fixture
from tests.c.integration.wifi.config.test_wifi_config_controls import PRELUDE, sdk_types, structure
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import ROOT, CORE, build, extract, run

RADIO=selection_fixture.RADIO
HEADER=selection_fixture.HEADER


class WiFiStartCapture(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory();cls.addClassCleanup(cls.temp.cleanup)
        options=(CORE/'esp32_mquickjs_options.c').read_text().replace(
            '#include "esp32_mquickjs_options.h"',
            (ROOT/'components/esp32_mquickjs/internal/esp32_mquickjs_options.h').read_text().replace(
                '#include "esp32_mquickjs_types.h"',''))
        config=(ROOT/'components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi_config.c').read_text()
        types=fixture_text('wifi/lifecycle/test_wifi_start_options/setupclass-types.inc')
        cls.binary=build(cls.temp.name,types+structure(HEADER.read_text(),'esp32_mquickjs_wifi_radio_configuration_selection_t')+
                         options+extract(config,'esp32_mquickjs_wifi_capture_start'),CAPTURE_MAIN)

    def test_capture_defaults_choices_strict_input_gc_and_allocation_failures(self):
        for expression,expected,mode,storage in [
            ('undefined',1,0,-1),('({})',1,0,-1),('({mode:"station"})',1,1,-1),
            ('({mode:"ap",storage:"flash"})',1,2,0),('({mode:"apsta",storage:"ram"})',1,3,1),
            ('null',0,0,-1),('[]',0,0,-1),('({mode:1})',0,0,-1),('({storage:true})',0,0,-1),
            ('({mode:"station\\u0000"})',0,0,-1),('({storage:"flash\\u0000"})',0,0,-1),
            ('({allowDisconnect:true})',0,0,-1),('({"mode\\u0000":"ap"})',0,0,-1),
            ('({start_only:true})',0,0,-1),
            ('({get mode(){throw 12345;}})',0,0,-1),
            ('({get storage(){throw 12345;}})',0,0,-1),
            ('({get mode(){gc();return "apsta";},get storage(){gc();return "ram";}})',1,3,1)]:
            run([str(self.binary),expression,str(expected),str(mode),str(storage)])


CAPTURE_MAIN = fixture_text('wifi/lifecycle/test_wifi_start_options/capture_main.inc')


class WiFiStartResolution(unittest.TestCase):
    def code(self,softap=True):
        header,radio=HEADER.read_text(),RADIO.read_text()
        enums=re.search(r'typedef enum \{[^}]*\} esp32_mquickjs_wifi_radio_client_t;',header).group(0)
        structs='typedef unsigned esp32_mquickjs_wifi_promiscuous_token_t;\n'+structure(radio,'wifi_radio_live_lease_t')+''.join(structure(header,n) for n in ['esp32_mquickjs_wifi_radio_lease_t',
            'esp32_mquickjs_wifi_radio_lifecycle_t','esp32_mquickjs_wifi_radio_configuration_selection_t'])
        functions=''.join(extract(radio,n) for n in ['wifi_radio_lease_valid','wifi_radio_acquire_locked','wifi_radio_begin_lifecycle_with_dependents_locked', 'wifi_radio_begin_lifecycle_locked',
                                                    'esp32_mquickjs_wifi_radio_begin_start_lifecycle'])
        boundaries=selection_fixture.BOUNDARIES.replace(
            'struct {unsigned identity;esp32_mquickjs_wifi_radio_client_t client;} leases[WIFI_RADIO_MAX_LEASES];',
            'wifi_radio_live_lease_t leases[WIFI_RADIO_MAX_LEASES];\n    unsigned clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_COUNT],next_lease_identity;int fault_error;')
        boundaries=boundaries.replace('s_radio.generation=7;', 's_radio.generation=7;s_radio.next_lease_identity=20;')
        return PRELUDE+f'#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT {int(softap)}\n'+sdk_types('esp32c5/representative')+enums+structs+boundaries+START_BOUNDARIES+functions

    def test_stopped_defaults_preserve_mode_storage_and_running_requests_do_not_reconfigure(self):
        compile_run(self,self.code()+fixture_text('wifi/lifecycle/test_wifi_start_options/test_stopped_defaults_preserve_mode_storage_and_running_requests_do_not_reconfigure.inc'))

    def test_running_station_gets_application_owner_before_mutex_release(self):
        compile_run(self,self.code()+fixture_text('wifi/lifecycle/test_wifi_start_options/test_running_station_gets_application_owner_before_mutex_release.inc'))

    def test_softap_gate_precedes_admission_and_driver_access(self):
        compile_run(self,self.code(False)+fixture_text('wifi/lifecycle/test_wifi_start_options/test_softap_gate_precedes_admission_and_driver_access.inc'))


START_BOUNDARIES = fixture_text('wifi/lifecycle/test_wifi_start_options/start_boundaries.inc')


class WiFiStartExecutor(unittest.TestCase):
    def test_configured_apsta_start_reuses_helpers_and_never_rewrites_ap_credentials(self):
        compile_run(self,executor_fixture.WiFiConfigurationExecutor().code()+fixture_text('wifi/lifecycle/test_wifi_start_options/test_configured_apsta_start_reuses_helpers_and_never_rewrites_ap_credentials.inc'))


class WiFiStartStoredConfig(unittest.TestCase):
    def test_exact_stopped_admission_and_failed_snapshot_are_zeroed(self):
        radio=RADIO.read_text()
        body=extract(radio,'wifi_radio_check_stopped_lifecycle_locked')
        body+=extract((CORE/'esp32_mquickjs_wireless_core.c').read_text(),'esp32_mquickjs_wireless_secure_zero')
        body+=extract(radio,'wifi_radio_validate_saved_ap_config')
        body+=extract(radio,'esp32_mquickjs_wifi_radio_copy_stopped_ap_configuration')
        compile_run(self,STORED_BOUNDARIES+body+fixture_text('wifi/lifecycle/test_wifi_start_options/test_exact_stopped_admission_and_failed_snapshot_are_zeroed.inc'))


STORED_BOUNDARIES = fixture_text('wifi/lifecycle/test_wifi_start_options/stored_boundaries.inc')
