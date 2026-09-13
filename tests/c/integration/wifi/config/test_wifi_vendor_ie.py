"""Deferred production Vendor IE ownership/configuration; no fixture run this wave.

SDK bytes, lock and Radio storage are injected. Exact production registry acquire,
release, lifecycle admission and all Vendor IE operations are extracted unchanged.
No RF, SDK allocation or runtime consumer teardown proof is inferred.
"""
from tests.support.fixtures import fixture_text
import re
import tempfile
import unittest
from tests.c.integration.wifi.config.test_wifi_config_controls import sdk_types, structure
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import CORE, build, extract, run
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit


def vendor_code(profile, ap=True, enterprise=False):
    radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
    header = (COMPONENT / 'internal/esp32_mquickjs_wifi_radio.h').read_text()
    vendor = (COMPONENT / 'internal/esp32_mquickjs_wifi_vendor_ie.h').read_text()
    code = PRELUDE + f'#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT {int(ap)}\n'
    if enterprise:
        code += '#define CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT 1\n'
        start = radio.index('#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT\n/* Operation mutex only.')
        code += radio[start:radio.index('\n#endif', start) + len('\n#endif')] + '\n'
    code += sdk_types(profile, ('wifi_vendor_ie_type_t', 'wifi_vendor_ie_id_t'))
    for name in ('esp32_mquickjs_wifi_radio_client_t', 'esp32_mquickjs_wifi_radio_driver_state_t'):
        code += re.search(r'typedef enum \{[^}]*\} ' + name + ';', header).group(0)
    for name in ('esp32_mquickjs_wifi_radio_lease_t', 'esp32_mquickjs_wifi_radio_lifecycle_t',
                 'esp32_mquickjs_wifi_radio_promiscuous_lease_t', 'esp32_mquickjs_wifi_radio_configuration_selection_t'):
        code += structure(header, name)
    # Unused broker storage is an injected boundary, never exercised here.
    code += 'typedef unsigned esp32_mquickjs_wifi_promiscuous_token_t;\n'
    code += structure(radio, 'wifi_radio_live_lease_t')
    code += '\n' + '\n'.join(re.findall(r'^#define ESP32_MQUICKJS_WIFI_VENDOR_IE_.*$', vendor, re.M)) + '\n'
    for name in ('esp32_mquickjs_wifi_vendor_ie_slot_t', 'esp32_mquickjs_wifi_vendor_ie_status_t'):
        code += structure(vendor, name)
    code += BOUNDARIES
    code += re.search(r'static struct \{[^}]*\} s_vendor_ie;', radio).group(0)
    code += (COMPONENT / 'internal/esp32_mquickjs_wifi_radio_mutation.h').read_text()
    for name in ('wifi_radio_lease_valid', 'wifi_radio_promiscuous_owner', 'wifi_radio_acquire_locked',
                 'wifi_radio_release_locked', 'wifi_radio_begin_lifecycle_with_dependents_locked', 'wifi_radio_begin_lifecycle_locked',
                 'wifi_radio_vendor_ie_interface', 'wifi_radio_vendor_ie_release_empty',
                 'wifi_radio_vendor_ie_start_matches', 'wifi_radio_vendor_ie_remove',
                 'wifi_radio_vendor_ie_start_count', 'wifi_radio_vendor_ie_start_park',
                 'wifi_radio_vendor_ie_begin_start_locked', 'wifi_radio_vendor_ie_start_attach',
                 'wifi_radio_vendor_ie_start_commit', 'esp32_mquickjs_wifi_radio_vendor_ie_clear_lifecycle',
                 'esp32_mquickjs_wifi_radio_vendor_ie_set',
                 'esp32_mquickjs_wifi_radio_vendor_ie_clear', 'esp32_mquickjs_wifi_radio_vendor_ie_status'):
        code += extract(radio, name)
    return code + RESET


class WiFiVendorIe(unittest.TestCase):
    def test_slots_failed_enable_cleanup_suffix_and_exact_lifecycle_owners(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            for ap in (False, True):
                with self.subTest(target=profile, softap=ap):
                    compile_run(self, vendor_code(profile, ap) + MAIN)

    def test_public_input_capture_result_oom_and_rooted_status(self):
        code = vendor_code('esp32c5/representative')
        code += unit(CORE / 'esp32_mquickjs_options.c')
        code += extract((CORE / 'esp32_mquickjs.c').read_text(), 'esp32_mquickjs_throw_native_error')
        code += 'static const char *esp_err_to_name(int err) {(void)err;return "injected";}\n'
        code += 'static JSValue esp32_mquickjs_wifi_vendor_ie_watch_status(JSContext *ctx) {return JS_NewObject(ctx); }\n'
        code += unit(COMPONENT / 'src/modules/wifi_vendor_ie/esp32_mquickjs_wifi_vendor_ie.c')
        base = '{interface:"station",frame:"probe-request",index:0,enabled:true,data:[221,4,1,2,3,4]}'
        cases = [(base, True), ('null', False), (base.replace('index:0','index:1.5'), False),
                 (base.replace('index:0','index:"0"'), False), (base.replace('enabled:true','enabled:1'), False),
                 (base.replace('probe-request','beacon'), False), (base.replace('[221,4,1,2,3,4]','[221,5,1,2,3,4]'), False),
                 (base.replace('[221,4,1,2,3,4]','[221,4,1,2,3,256]'), False),
                 (base.replace('interface:', 'extra:1,interface:'), False),
                 (base.replace('enabled:true','enabled:false'), False)]
        with tempfile.TemporaryDirectory() as tmp:
            binary = build(tmp, code, VM_MAIN)
            for expression, valid in cases:
                run([str(binary), "(" + expression + ")", str(int(valid)), 'set'])
            for scenario in ('arity', 'sdk-error', 'status', 'clear'):
                run([str(binary), "(" + base + ")", '0' if scenario in ('arity', 'sdk-error') else '1', scenario])


PRELUDE = fixture_text('wifi/config/test_wifi_vendor_ie/prelude.inc')

BOUNDARIES = fixture_text('wifi/config/test_wifi_vendor_ie/boundaries.inc')

RESET = fixture_text('wifi/config/test_wifi_vendor_ie/reset.inc')

MAIN = fixture_text('wifi/config/test_wifi_vendor_ie/main.inc')


VM_MAIN = fixture_text('wifi/config/test_wifi_vendor_ie/vm_main.inc')
