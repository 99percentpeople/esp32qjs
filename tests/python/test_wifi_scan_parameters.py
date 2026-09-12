"""Deferred production scan transaction, SDK excess-copy boundary and restart intent.

Only SDK storage/failure, locks and physical activation are injected. These do
not prove actual RF scan timing, SDK normalization or device lifecycle behavior.
"""
import re
import shutil
import subprocess
import tempfile
from pathlib import Path
import unittest
from test_wifi_connection_controls import control_code
from test_wifi_driver_phy import COMPONENT
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import extract


def scan_support(radio):
    header = (COMPONENT / 'internal/esp32_mquickjs_wifi_scan_parameters.h').read_text()
    header = re.sub(r'^#(?:include|pragma).*$', '', header, flags=re.M)
    code = BOUNDARIES + header
    code += re.search(r'static struct \{[^}]*\} s_scan_parameters;', radio).group(0)
    code += extract(radio, 'wifi_radio_scan_parameters_observe_locked')
    code += extract(radio, 'wifi_radio_restore_scan_parameters_locked')
    return code


def scan_code(profile, ap=True):
    radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
    wifi = (COMPONENT / 'src/modules/wifi/esp32_mquickjs_wifi.c').read_text()
    code = control_code(profile, ap, ('wifi_scan_default_params_t',)) + scan_support(radio)
    code += 'static struct {bool unchanged;} s_stop_snapshot;\n'
    code += extract(radio, 'wifi_radio_invalidate_stop_snapshot_locked')
    for name in ('esp32_mquickjs_wifi_radio_read_scan_parameters', 'esp32_mquickjs_wifi_radio_write_scan_parameters'):
        code += extract(radio, name)
    code += extract(wifi, 'esp32_mquickjs_wifi_apply_scan_parameters')
    return code


class WiFiScanParameters(unittest.TestCase):
    def test_production_sdk_failures_exact_owners_padding_and_activation(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            with self.subTest(profile=profile):
                compile_run(self, scan_code(profile) + MAIN)


    def test_unadapted_sdk_copy_fails_asan_and_production_padding_survives(self):
        compiler = shutil.which('cc')
        if compiler is None:
            self.skipTest('C compiler unavailable')
        production = scan_code('esp32c5/representative')
        unadapted = production.replace(extract(production, 'wifi_scan_parameters_set_native'),
            'static int wifi_scan_parameters_set_native(const wifi_scan_default_params_t *p) { return esp_wifi_set_scan_parameters(p); }\n')
        main = r'''
int main(void) {
    reset();wifi_scan_default_params_t value=WIFI_SCAN_PARAMS_DEFAULT_CONFIG(),actual;
    assert(esp32_mquickjs_wifi_apply_scan_parameters(&value,&actual,&result)==ESP_OK);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            for adapted, body in ((False, unadapted), (True, production)):
                source = Path(tmp) / ('adapted.c' if adapted else 'unadapted.c')
                binary = source.with_suffix('')
                source.write_text(body + main)
                # Force the full native-sized read through ASAN's memcpy interceptor;
                # GCC's inline copy can miss the poisoned middle of this span.
                compiled = subprocess.run([compiler, '-std=c11', '-g', '-fsanitize=address',
                    '-fno-omit-frame-pointer', '-fno-builtin-memcpy', str(source), '-o', str(binary)], capture_output=True, text=True)
                self.assertEqual(compiled.returncode, 0, compiled.stderr)
                executed = subprocess.run([str(binary)], capture_output=True, text=True)
                if adapted:
                    self.assertEqual(executed.returncode, 0, executed.stderr)
                else:
                    self.assertNotEqual(executed.returncode, 0)
                    self.assertIn('stack-buffer-overflow', executed.stderr)


BOUNDARIES = r'''
#define WIFI_SCAN_PARAMS_DEFAULT_CONFIG() {.scan_time={.active={.min=0,.max=120},.passive=360},.home_chan_dwell_time=30}
static wifi_scan_default_params_t native_scan=WIFI_SCAN_PARAMS_DEFAULT_CONFIG();
static bool scan_partial_failure,scan_corrupt;
static unsigned scan_writes,scan_resets;
static int esp_wifi_get_scan_parameters(wifi_scan_default_params_t *out) {
    assert(s_radio.started && (s_radio.effective_mode&WIFI_MODE_STA));
    int error=sdk_step(false);
    if(!error){*out=native_scan;if(scan_corrupt)out->home_chan_dwell_time=1;}
    return error;
}
static int esp_wifi_set_scan_parameters(const wifi_scan_default_params_t *input) {
    assert(s_radio.started && (s_radio.effective_mode&WIFI_MODE_STA));
    ++scan_writes;scan_resets+=input==NULL;
    if(input) {
        /* Exactly the fixed SDK's 44-byte read, so ASAN catches an unpadded
         * public 16-byte argument. Extra command bytes must be deterministic. */
        unsigned char command[44];memcpy(command,input,sizeof(command));
        for(unsigned i=sizeof(*input);i<sizeof(command);++i)assert(command[i]==0);
    }
    int error=sdk_step(true);
    if(!error || scan_partial_failure) {
        native_scan=(wifi_scan_default_params_t)WIFI_SCAN_PARAMS_DEFAULT_CONFIG();
        if(input){native_scan=*input;
            if(!native_scan.scan_time.active.max)native_scan.scan_time.active.max=120;
            if(!native_scan.scan_time.passive)native_scan.scan_time.passive=360;
            if(!native_scan.home_chan_dwell_time)native_scan.home_chan_dwell_time=30;}
    }
    return error;
}
'''

MAIN = r'''
static wifi_scan_default_params_t wanted={.scan_time={.active={.min=4,.max=90},.passive=222},.home_chan_dwell_time=50},actual;
static void source(void){reset();memset(&s_scan_parameters,0,sizeof(s_scan_parameters));
    native_scan=(wifi_scan_default_params_t)WIFI_SCAN_PARAMS_DEFAULT_CONFIG();
    scan_partial_failure=scan_corrupt=false;scan_writes=scan_resets=0;}
static int change(const wifi_scan_default_params_t *p){return esp32_mquickjs_wifi_apply_scan_parameters(p,&actual,&result);}
int main(void){
    source();assert(change(&wanted)==ESP_OK && wifi_scan_parameters_equal(&actual,&wanted));
    assert(s_scan_parameters.known && !result.persistent_mutation_possible);
    assert(change(NULL)==ESP_OK && scan_resets==1 && actual.scan_time.active.max==120);
    wifi_scan_default_params_t zero={0};assert(change(&zero)==ESP_OK && actual.home_chan_dwell_time==30);
    unsigned before=calls;zero.scan_time.active.min=121;
    assert(change(&zero)==ESP_ERR_INVALID_ARG && calls==before);
    source();fail_at=2;assert(change(&wanted)==77 && result.rollback_complete && !result.rollback_attempted);
    assert(scan_writes==1 && !s_radio.fault_stage);
    source();fail_at=2;scan_partial_failure=true;
    assert(change(&wanted)==77 && result.rollback_attempted && result.rollback_complete && scan_writes==2);
    assert(native_scan.scan_time.active.max==120);
    source();fail_at=3;rollback_fail=1;
    assert(change(&wanted)==77 && s_radio.fault_stage && s_radio.cleanup_stage);
    before=calls;assert(change(&wanted)==ESP_ERR_INVALID_STATE && calls==before);
    source();s_wifi_state.scan_draining=true;assert(change(&wanted)==ESP_ERR_INVALID_STATE && !calls);
    source();s_wifi_state.radio_lease.identity++;assert(change(&wanted)==ESP_ERR_INVALID_STATE && !calls);
    source();s_radio.leases[3]=(wifi_radio_live_lease_t){90,ESP32_MQUICKJS_WIFI_RADIO_CLIENT_CSI};
    assert(change(&wanted)==ESP_ERR_INVALID_STATE && !calls);
    source();s_wifi_state.status.connected=true;assert(change(&wanted)==ESP_OK);
    source();assert(change(&wanted)==ESP_OK);wifi_radio_operation_lock();
    native_scan=(wifi_scan_default_params_t)WIFI_SCAN_PARAMS_DEFAULT_CONFIG();s_scan_parameters.pending=true;
    before=calls;assert(wifi_radio_restore_scan_parameters_locked(WIFI_MODE_AP)==ESP_OK && calls==before);
    assert(wifi_radio_restore_scan_parameters_locked(WIFI_MODE_STA)==ESP_OK && wifi_scan_parameters_equal(&native_scan,&wanted));
    before=scan_writes;assert(wifi_radio_restore_scan_parameters_locked(WIFI_MODE_STA)==ESP_OK && scan_writes==before);
    native_scan=(wifi_scan_default_params_t)WIFI_SCAN_PARAMS_DEFAULT_CONFIG();s_scan_parameters.pending=true;fail_at=calls+2;
    assert(wifi_radio_restore_scan_parameters_locked(WIFI_MODE_STA)==77 && s_radio.fault_stage);
    before=calls;assert(wifi_radio_restore_scan_parameters_locked(WIFI_MODE_STA)==ESP_ERR_INVALID_STATE && calls==before);
    wifi_radio_operation_unlock();
    source();const char *stage=NULL;s_radio.effective_mode=WIFI_MODE_AP;
    assert(esp32_mquickjs_wifi_radio_read_scan_parameters(&actual,&stage)==ESP_ERR_INVALID_STATE && !calls);
    return 0;
}
'''
