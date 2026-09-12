"""Deferred actual AP rate Radio startup/cleanup; not imported/run in API phase.

Composes production registry, rate transaction, STOP/restore, lifecycle admission
and AP start/cleanup. SDK state, helper readiness, validation result and allocation
are injected boundaries. No substitute startup/cleanup state machine.
"""
import json
import unittest
from test_wifi_raw_tx_rate_lease import radio_rate_code
from test_wifi_tx_rate import COMPONENT, ROOT
from test_wifi_config_controls import sdk_types
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import extract


def ap_rate_code(profile):
    code = radio_rate_code(profile)
    extra = sdk_types(profile)
    symbols = json.loads((ROOT / 'docs/idf-wifi-api-inventory.json').read_text())['variants'][profile]['symbols']
    for value in symbols.values():
        decl = value.get('declaration', '')
        if decl.startswith('typedef ') and decl in code:
            extra = extra.replace(decl + ';', '')
    code = code.replace('#define WIFI_RADIO_MAX_LEASES 4',
                        extra + '\n#define WIFI_RADIO_MAX_LEASES 4')
    code = code.replace('unsigned generation,next_lease_identity,clients',
                        'unsigned generation,next_lease_identity,next_lifecycle_identity,clients')
    code = code.replace('struct {uint32_t identity,lease_identity;} operation,lifecycle;',
                        'struct {uint32_t identity,lease_identity;} operation;\n'
                        'esp32_mquickjs_wifi_radio_lifecycle_t lifecycle;')
    code = code.replace('s_radio.effective_mode=WIFI_MODE_STA;', 'assert(s_radio.leases[0].required_mode==s_radio.effective_mode);')
    code = code.replace('interface==WIFI_IF_STA && s_tx_rate_lease.identity',
                        'interface==WIFI_IF_AP && s_tx_rate_lease.identity')
    code += BOUNDARIES
    radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
    for name in ('wifi_radio_begin_lifecycle_with_dependents_locked', 'wifi_radio_begin_lifecycle_locked',
                 'wifi_radio_check_stopped_lifecycle_locked', 'esp32_mquickjs_wifi_radio_check_stopped_lifecycle',
                 'wifi_radio_config_equal', 'wifi_radio_validate_saved_ap_config',
                 'esp32_mquickjs_wifi_radio_copy_stopped_ap_configuration',
                 'esp32_mquickjs_wifi_radio_begin_raw_tx_ap_rate',
                 'esp32_mquickjs_wifi_radio_start_raw_tx_ap_rate',
                 'esp32_mquickjs_wifi_radio_quiesce_raw_tx_ap_rate'):
        code += extract(radio, name)
    return code


class WiFiRawTxApRate(unittest.TestCase):
    def test_saved_wpa2_sdk_sae_default_is_preserved_without_weakening_input(self):
        radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
        for profile in ('esp32c3/representative','esp32s3/representative-psram','esp32c5/representative'):
            with self.subTest(profile=profile):
                code = ap_rate_code(profile)
                original = extract(code,'esp32_mquickjs_wifi_radio_validate_ap_config')
                code = code.replace(original, '''
#define ESP32_MQUICKJS_WIFI_MAX_AP_CLIENTS 4
#define ESP32_MQUICKJS_WIFI_AP_BEACON_QUANTUM_TU 100
#define ESP32_MQUICKJS_WIFI_AP_BEACON_MAX_TU 60000
#define ESP32_MQUICKJS_WIFI_AP_DTIM_MAX 10
''' + extract(radio,'esp32_mquickjs_wifi_radio_validate_ap_config'))
                compile_run(self,code+MAIN[:MAIN.index('int main(void)')]+r'''
int main(void) {
    setup();
    stored.ap.authmode=WIFI_AUTH_WPA2_PSK;memcpy(stored.ap.password,"12345678",9);
    stored.ap.max_connection=1;stored.ap.beacon_interval=100;stored.ap.dtim_period=1;
    stored.ap.csa_count=3;stored.ap.pairwise_cipher=WIFI_CIPHER_TYPE_CCMP;
    stored.ap.pmf_cfg.capable=true;stored.ap.sae_pwe_h2e=WPA3_SAE_PWE_BOTH;
    /* Explicit dormant SAE options remain invalid input; the SDK nonetheless
     * returns this default for a normal WPA2 AP. Replay must preserve it. */
    assert(esp32_mquickjs_wifi_radio_validate_ap_config(&stored)==ESP_ERR_INVALID_ARG);
    assert(begin()==ESP_OK && !memcmp(&snapshot,&stored,sizeof(stored)));
    assert(start()==ESP_OK && !memcmp(&snapshot,&stored,sizeof(stored)));
    assert(close_rate()==ESP_OK);
    for (int i=0;i<4;i++) {
        wifi_config_t invalid=stored;
        if(i==0)invalid.ap.password[0]=0;
        if(i==1){invalid.ap.pmf_cfg.required=true;invalid.ap.pmf_cfg.capable=false;}
        if(i==2)invalid.ap.authmode=WIFI_AUTH_WPA3_PSK;
        if(i==3)invalid.ap.sae_pwe_h2e=(wifi_sae_pwe_method_t)255;
        wifi_config_t before=invalid;
        assert(wifi_radio_validate_saved_ap_config(&invalid)!=ESP_OK);
        assert(!memcmp(&before,&invalid,sizeof(invalid)));
    }
    return 0;
}
''')

    def test_start_snapshot_failure_handoff_and_atomic_cleanup(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            with self.subTest(profile=profile):
                compile_run(self, ap_rate_code(profile) + MAIN)


BOUNDARIES = r'''
#include <stdlib.h>
#define ESP_ERR_INVALID_RESPONSE 80
static struct {struct {uint32_t identity;} start_owner;} s_vendor_ie;
static struct {void *snapshot;} s_config_restart;
static struct {struct {uint32_t identity;} owner;} s_policy_restart;
static wifi_config_t stored;
static unsigned gets,allocations,frees,wipes;
static int config_error,mode_error,validation_error;
static bool allocation_fail;
static void *ap_calloc(size_t count,size_t size) {
    if(allocation_fail)return NULL;
    ++allocations;return calloc(count,size);
}
static void ap_free(void *p) {
    assert(p);const unsigned char *bytes=p;
    for(size_t i=0;i<sizeof(wifi_config_t);++i)assert(!bytes[i]);
    ++frees;free(p);
}
#define calloc ap_calloc
#define free ap_free
static void esp32_mquickjs_wireless_secure_zero(void *p,size_t size) {
    assert(size==sizeof(wifi_config_t));memset(p,0,size);++wipes;
}
static esp_err_t esp_wifi_get_mode(wifi_mode_t *mode) {
    assert(locks && !critical);*mode=s_radio.effective_mode;return mode_error;
}
static esp_err_t esp_wifi_get_config(wifi_interface_t interface,wifi_config_t *config) {
    assert(locks && !critical && interface==WIFI_IF_AP);++gets;*config=stored;return config_error;
}
/* Complete AP credential validation has its own production fixture. */
static esp_err_t esp32_mquickjs_wifi_radio_validate_ap_config(const wifi_config_t *config) {
    assert(config);return validation_error;
}
'''

MAIN = r'''
#define radio(n) esp32_mquickjs_wifi_radio_##n
static wifi_tx_rate_config_t requested={.phymode=WIFI_PHY_MODE_11G,.rate=WIFI_PHY_RATE_24M};
static esp32_mquickjs_wifi_radio_lifecycle_t token;
static esp32_mquickjs_wifi_radio_lease_t lease;
static wifi_config_t snapshot;
static uint8_t channel;
static wifi_mode_t selected_mode;
static void setup(void) {
    memset(&s_radio,0,sizeof(s_radio));s_radio.generation=7;s_radio.next_lease_identity=1;s_radio.next_lifecycle_identity=1;
    s_radio.driver_owned=s_radio.storage_configured=true;s_radio.effective_mode=WIFI_MODE_AP;
    s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
    s_tx_rates=(esp32_mquickjs_wifi_tx_rate_state_t){.next_identity=2};
    native_rate=(wifi_tx_rate_config_t){.phymode=WIFI_PHY_MODE_11G,.rate=WIFI_PHY_RATE_6M};
    s_tx_rates.records[1]=(esp32_mquickjs_wifi_tx_rate_record_t){.known=true,.generation=7,.write_identity=1,.config=native_rate};
    memset(&s_tx_rate_lease,0,sizeof(s_tx_rate_lease));memset(&token,0,sizeof(token));memset(&lease,0,sizeof(lease));
    memset(&stored,0,sizeof(stored));stored.ap.channel=6;stored.ap.ssid_len=4;memcpy(stored.ap.ssid,"test",4);
    writes=starts=stops=waits=gets=allocations=frees=wipes=0;config_error=mode_error=validation_error=0;
    next_write[0]=next_write[1]=stop_error=wait_error=start_error=0;allocation_fail=stop_observed=false;channel=99;selected_mode=WIFI_MODE_NULL;
}
static int begin(void) {
    int err=radio(begin_raw_tx_ap_rate)(&requested,&token,&selected_mode);
    if(!err)err=radio(copy_stopped_ap_configuration)(&token,&snapshot);
    return err;
}
static int start(void) {return radio(start_raw_tx_ap_rate)(&token,selected_mode,&snapshot,&requested,&lease,&channel);}
static int close_rate(void) {return radio(quiesce_raw_tx_ap_rate)(&lease,&token);}
int main(void) {
    (void)esp32_mquickjs_wifi_radio_5ghz_channel_bit;
    setup();assert(!begin() && token.identity && !writes && !starts);
    assert(!start() && !token.identity && lease.acquired && channel==6 && starts==1 && writes==1);
    assert(allocations==frees && wipes==1);
    s_radio.leases[0].raw_tx_identity=55;
    assert(close_rate()==ESP_ERR_INVALID_STATE && lease.acquired && !token.identity && !stops);
    s_radio.leases[0].raw_tx_identity=0;
    s_radio.next_lifecycle_identity=0;
    assert(close_rate()==ESP_ERR_NO_MEM && !stops && lease.acquired);s_radio.next_lifecycle_identity=2;
    wait_error=51;assert(close_rate()==51 && stops==1 && writes==1 && !token.identity && lease.acquired);
    wait_error=0;next_write[0]=52;
    assert(close_rate()==52 && stops==1 && writes==3 && !token.identity && lease.acquired);
    assert(!close_rate() && token.identity && !lease.acquired && stops==1 && native_rate.rate==WIFI_PHY_RATE_6M);
    assert(!radio(check_stopped_lifecycle)(&token,true));
    unsigned before=writes;assert(!close_rate() && writes==before && stops==1);
    esp32_mquickjs_wifi_radio_lifecycle_t stale=token;stale.generation++;
    assert(radio(quiesce_raw_tx_ap_rate)(&lease,&stale)==ESP_ERR_INVALID_STATE);
    setup();assert(!begin());stored.ap.password[0]='x';
    assert(start()==ESP_ERR_INVALID_RESPONSE && token.identity && !lease.acquired && !writes && !starts);
    assert(allocations==frees && !close_rate());
    setup();assert(!begin());allocation_fail=true;
    assert(start()==ESP_ERR_NO_MEM && token.identity && !lease.acquired && !writes && !starts);
    allocation_fail=false;assert(!close_rate());
    setup();assert(!begin());start_error=53;
    assert(start()==53 && !token.identity && lease.acquired && s_tx_rate_lease.identity);
    assert(!close_rate() && token.identity && !lease.acquired && s_radio.fault_error==53);
    assert(!radio(check_stopped_lifecycle)(&token,true));
    setup();assert(!begin());next_write[0]=54;next_write[1]=0;
    assert(start()==54 && token.identity && lease.acquired && !s_tx_rate_lease.identity && !starts);
    assert(!close_rate() && !lease.acquired && token.identity);
    setup();assert(!begin());next_write[0]=54;next_write[1]=55;
    assert(start()==54 && !token.identity && lease.acquired && s_tx_rate_lease.restore_pending);
    assert(!close_rate() && token.identity && !lease.acquired);
    setup();s_radio.leases[0].identity=77;
    assert(begin()==ESP_ERR_INVALID_STATE && !token.identity && !writes && !gets);
    setup();s_radio.next_lifecycle_identity=0;
    assert(begin()==ESP_ERR_NO_MEM && !writes && !gets);
    setup();s_radio.effective_mode=WIFI_MODE_APSTA;
    assert(!begin() && selected_mode==WIFI_MODE_APSTA && !writes && !starts);
    assert(radio(start_raw_tx_ap_rate)(&token,WIFI_MODE_AP,&snapshot,&requested,&lease,&channel)==ESP_ERR_INVALID_STATE);
    assert(!lease.acquired && !writes && token.identity);
    s_tx_rates.records[0]=(esp32_mquickjs_wifi_tx_rate_record_t){.known=true,.generation=7,.write_identity=2,
        .config={.phymode=WIFI_PHY_MODE_11G,.rate=WIFI_PHY_RATE_9M}};s_tx_rates.next_identity=3;
    esp32_mquickjs_wifi_tx_rate_record_t station_rate=s_tx_rates.records[0];
    assert(!start() && s_radio.effective_mode==WIFI_MODE_APSTA && s_radio.leases[0].required_mode==WIFI_MODE_APSTA);
    assert(!close_rate() && s_radio.effective_mode==WIFI_MODE_APSTA);
    assert(!memcmp(&station_rate,&s_tx_rates.records[0],sizeof(station_rate)));
    setup();s_radio.effective_mode=WIFI_MODE_STA;
    assert(begin()==ESP_ERR_INVALID_STATE && selected_mode==WIFI_MODE_NULL && !token.identity && !gets);
    return 0;
}
'''
