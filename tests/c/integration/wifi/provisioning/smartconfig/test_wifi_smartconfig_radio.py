"""Deferred production SmartConfig Radio admission, exact pins and restoration."""
from tests.support.fixtures import fixture_text
import re
import unittest
from tests.c.integration.wifi.config.test_wifi_vendor_ie import vendor_code, COMPONENT
from tests.c.integration.wifi.config.test_wifi_config_controls import sdk_types, structure
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import CORE, extract


class SmartConfigRadio(unittest.TestCase):
    def test_production_radio_binding(self):
        radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
        header = (COMPONENT / 'internal/esp32_mquickjs_wifi_radio.h').read_text()
        code = '#define CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API 1\n#define CONFIG_LWIP_IPV4 1\n' + vendor_code('esp32c5/representative')
        # Event-loop fence callbacks only take the critical lock; SDK mutation
        # stubs still require the operation mutex independently.
        code = code.replace('assert(locks && !critical);critical=1;', 'assert(!critical);critical=1;')
        code = code.replace('assert(locks && critical);critical=0;', 'assert(critical);critical=0;')
        enums = re.search(r'typedef enum \{[^}]*\} esp32_mquickjs_wifi_radio_operation_kind_t;', header).group(0)
        before = sdk_types('esp32c5/representative')
        more = sdk_types('esp32c5/representative', ('wifi_ap_record_t',))
        assert more.startswith(before)
        extra = more[len(before):] + enums + structure(header, 'esp32_mquickjs_wifi_radio_operation_t') + SMARTCONFIG_TYPES
        for name in ('events', 'decoder', 'radio'):
            extra += unit(COMPONENT / 'internal' / ('esp32_mquickjs_wifi_smartconfig_' + name + '.h'))
        extra += structure(radio, 'wifi_radio_smartconfig_t')
        extra += '\nstatic wifi_radio_smartconfig_t *s_sc_radio;\n#define WIFI_RADIO_SMARTCONFIG_PENDING (s_sc_radio != NULL)\n#define WIFI_RADIO_EAP_PENDING false\n'
        extra += '\nstatic struct {uint32_t owners[3];} *s_wps_radio;\n#define WIFI_RADIO_DPP_PENDING false\n#define WIFI_RADIO_WPS_PENDING (s_wps_radio != NULL)\n'
        extra += '\nstatic uint32_t s_sc_fence_posted,s_sc_fence_seen;\n#define WIFI_RADIO_SMARTCONFIG_FENCE_EVENT 7\n#define ESP32QJS_WIFI_RADIO_CONTROL_EVENT 1\n'
        code = code.replace('static struct {\n    int lock;', extra + '\nstatic struct {\n    int lock;', 1)
        code = code.replace('struct {unsigned identity,lease_identity;} operation;', 'esp32_mquickjs_wifi_radio_operation_t operation;')
        code = code.replace('generation,next_lease_identity,next_lifecycle_identity,wake_locks;', 'generation,next_operation_identity,next_lease_identity,next_lifecycle_identity,wake_locks;')
        code += BOUNDARIES
        code += extract((CORE / 'esp32_mquickjs_wireless_core.c').read_text(), 'esp32_mquickjs_wireless_secure_zero')
        for name in ('wifi_radio_smartconfig_observe_fence','wifi_radio_smartconfig_fence_locked',
                     'wifi_radio_connection_owner_locked','wifi_radio_smartconfig_exact_locked',
                     'wifi_radio_smartconfig_snapshot_locked','wifi_radio_smartconfig_restore_locked',
                     'esp32_mquickjs_wifi_radio_smartconfig_begin','esp32_mquickjs_wifi_radio_smartconfig_status',
                     'esp32_mquickjs_wifi_radio_smartconfig_finish_capture','esp32_mquickjs_wifi_radio_smartconfig_ack',
                     'esp32_mquickjs_wifi_radio_smartconfig_connection_begin','esp32_mquickjs_wifi_radio_smartconfig_connection_end',
                     'esp32_mquickjs_wifi_radio_smartconfig_credentials','esp32_mquickjs_wifi_radio_smartconfig_close',
                     'esp32_mquickjs_wifi_radio_end_operation'):
            code += extract(radio,name)
        compile_run(self, code + CASES)


SMARTCONFIG_TYPES = fixture_text('wifi/provisioning/smartconfig/test_wifi_smartconfig_radio/smartconfig_types.inc')

BOUNDARIES = fixture_text('wifi/provisioning/smartconfig/test_wifi_smartconfig_radio/boundaries.inc')

CASES = fixture_text('wifi/provisioning/smartconfig/test_wifi_smartconfig_radio/cases.inc')
