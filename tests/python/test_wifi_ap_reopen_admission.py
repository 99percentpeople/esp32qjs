"""Production live-STA reopen admission and exact stored config policy; run deferred."""
import re
import unittest
from test_wifi_config_controls import PRELUDE, RADIO, HEADER, sdk_types, structure
from wireless_vm_fixture import ROOT, CORE, extract
from test_wireless_control_regression import compile_run


class WiFiAPReopenAdmission(unittest.TestCase):
    def code(self, enabled=False):
        radio, header=RADIO.read_text(),HEADER.read_text()
        client=re.search(r'typedef enum \{[^}]*\} esp32_mquickjs_wifi_radio_client_t;',header).group(0)
        broker=HEADER.with_name('esp32_mquickjs_wifi_promiscuous_broker.h').read_text()
        structs=structure(broker,'esp32_mquickjs_wifi_promiscuous_token_t')
        structs+=client+structure(header,'esp32_mquickjs_wifi_radio_lease_t')
        structs+=structure(header,'esp32_mquickjs_wifi_radio_lifecycle_t')
        structs+=structure(radio,'wifi_radio_live_lease_t')
        functions=''.join(extract(radio,n) for n in [
            'wifi_radio_lease_valid','wifi_radio_acquire_locked','wifi_radio_begin_lifecycle_with_dependents_locked', 'wifi_radio_begin_lifecycle_locked',
            'esp32_mquickjs_wifi_radio_5ghz_channel_bit','esp32_mquickjs_wifi_radio_validate_ap_config',
            'esp32_mquickjs_wifi_radio_accept_ap_config','wifi_radio_interfaces_live',
            'wifi_radio_ap_prestart_config_matches','wifi_radio_ap_reopen_config_matches','esp32_mquickjs_wifi_radio_begin_ap_reopen'])
        zero=extract((CORE/'esp32_mquickjs_wireless_core.c').read_text(),'esp32_mquickjs_wireless_secure_zero')
        gate = '#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1\n'
        gate += '#define ESP32_MQUICKJS_WIFI_AP_PRESTART_AVAILABLE '+str(int(enabled))+'\n'
        return gate+PRELUDE.replace('#define WIFI_RADIO_MAX_LEASES 2','#define WIFI_RADIO_MAX_LEASES 4')+sdk_types('esp32c5/representative')+structs+BOUNDARIES+zero+functions

    def test_different_configuration_capture_and_auto_channel_acceptance(self):
        compile_run(self,self.code(enabled=True)+r'''
int main(void) {
 for(int failure=0;failure<2;failure++) {
  memset(&s_radio,0,sizeof(s_radio));captures=0;capture_error=failure?-91:0;
  s_radio.generation=9;s_radio.next_lease_identity=3;s_radio.next_lifecycle_identity=41;
  s_radio.driver_owned=s_radio.storage_configured=s_radio.started=true;
  s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
  s_radio.effective_mode=s_radio.event_live=WIFI_MODE_STA;
  esp32_mquickjs_wifi_radio_lease_t app={9,1,ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION,true};
  esp32_mquickjs_wifi_radio_lease_t sta={9,2,ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA,true},ap={0};
  s_radio.clients[app.client]=s_radio.clients[sta.client]=1;
  s_radio.leases[0]=(wifi_radio_live_lease_t){.identity=1,.client=app.client,.required_mode=WIFI_MODE_STA};
  s_radio.leases[1]=(wifi_radio_live_lease_t){.identity=2,.client=sta.client,.required_mode=WIFI_MODE_STA};
  wifi_config_t requested={0};requested.ap.ssid_len=4;memcpy(requested.ap.ssid,"test",4);
  requested.ap.max_connection=4;requested.ap.beacon_interval=100;
  requested.ap.dtim_period=1;requested.ap.csa_count=3;
  requested.ap.pairwise_cipher=WIFI_CIPHER_TYPE_CCMP;requested.ap.sae_pwe_h2e=WPA3_SAE_PWE_UNSPECIFIED;
  stored=requested;stored.ap.ssid[0]='o';
  esp32_mquickjs_wifi_radio_lifecycle_t token={0};
  int error=esp32_mquickjs_wifi_radio_begin_ap_reopen(&app,&sta,&requested,&ap,&token);
  assert(captures==1&&!live_alloc&&!depth&&!critical&&error==capture_error);
  if(failure)assert(!token.identity&&!ap.acquired&&!s_radio.ap_reopen_config_pending);
  else assert(token.identity==41&&ap.identity==3&&s_radio.ap_reopen_config_pending);
  assert(app.identity==1&&sta.identity==2&&s_radio.effective_mode==WIFI_MODE_STA);
  wifi_config_t observed=requested;observed.ap.channel=6;
  assert(wifi_radio_ap_prestart_config_matches(&requested,&observed));
  assert(!wifi_radio_ap_reopen_config_matches(&requested,&observed));
  requested.ap.channel=1;assert(!wifi_radio_ap_prestart_config_matches(&requested,&observed));
  observed=requested;observed.ap.ssid[0]='x';assert(!wifi_radio_ap_prestart_config_matches(&requested,&observed));
 }
}
''')

    def test_no_mutation_on_config_mismatch_allocation_sdk_or_owner_failures(self):
        compile_run(self,self.code()+r'''
int main(void) {
    for(int scenario=0;scenario<10;scenario++) {
        memset(&s_radio,0,sizeof(s_radio));allocs=queries=0;
        s_radio.generation=9;s_radio.next_lease_identity=3;s_radio.next_lifecycle_identity=41;
        s_radio.driver_owned=s_radio.storage_configured=s_radio.started=true;
        s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
        s_radio.effective_mode=s_radio.event_live=WIFI_MODE_STA;
        s_radio.clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION]=1;
        s_radio.clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA]=1;
        esp32_mquickjs_wifi_radio_lease_t app={9,1,ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION,true};
        esp32_mquickjs_wifi_radio_lease_t sta={9,2,ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA,true},ap={0};
        s_radio.leases[0]=(wifi_radio_live_lease_t){.identity=1,.client=app.client,.required_mode=WIFI_MODE_STA};
        s_radio.leases[1]=(wifi_radio_live_lease_t){.identity=2,.client=sta.client,.required_mode=WIFI_MODE_STA};
        wifi_config_t requested={0};requested.ap.ssid_len=4;memcpy(requested.ap.ssid,"test",4);
        requested.ap.channel=1;requested.ap.max_connection=4;requested.ap.beacon_interval=100;
        requested.ap.dtim_period=1;requested.ap.csa_count=3;
        requested.ap.pairwise_cipher=WIFI_CIPHER_TYPE_CCMP;requested.ap.sae_pwe_h2e=WPA3_SAE_PWE_UNSPECIFIED;
        stored=requested;
        fail_alloc=scenario==2;getter_error=scenario==3;
        if(scenario==1)stored.ap.ssid_hidden=true;
        if(scenario==3)memset(stored.ap.password,0x61,sizeof(stored.ap.password));
        if(scenario==4)s_radio.wake_locks=1;
        if(scenario==5)sta.generation++;
        if(scenario==6)s_radio.leases[2]=(wifi_radio_live_lease_t){.identity=7,.client=ESP32_MQUICKJS_WIFI_RADIO_CLIENT_ESPNOW,.required_mode=WIFI_MODE_STA};
        if(scenario==7)s_radio.next_lease_identity=0;
        if(scenario==8)s_radio.operation.identity=7;
        if(scenario==9)requested.ap.channel=255;
        esp32_mquickjs_wifi_radio_lifecycle_t token={0};
        int err=esp32_mquickjs_wifi_radio_begin_ap_reopen(&app,&sta,&requested,&ap,&token);
        assert(!live_alloc && !depth && !critical && app.identity==1 && sta.identity==2);
        if(!scenario) {
            assert(err==ESP_OK && token.identity==41 && ap.identity==3 && ap.acquired);
            assert(s_radio.ap_reopen_pending && !s_radio.ap_reopen_attempted && s_radio.ap_transition_access_point==3);
            assert(s_radio.effective_mode==WIFI_MODE_STA && s_radio.next_lease_identity==4);
        } else {
            assert(err!=ESP_OK && !token.identity && !s_radio.lifecycle.identity && !ap.acquired);
            assert(!s_radio.ap_reopen_pending && !s_radio.fault_stage && !s_radio.cleanup_stage);
            assert(s_radio.clients[app.client]==1 && s_radio.clients[sta.client]==1);
            if(scenario>=4)assert(!allocs && !queries);
        }
    }
}
''')


BOUNDARIES = r'''
#define WIFI_RADIO_NAN_PENDING false
#define WIFI_RADIO_MESH_PENDING false
/* No competing wake-interval or TX-rate transaction is active in these AP
 * admission cases; production owner admission remains in the extracted code. */
static struct {struct {unsigned identity;} owner;bool uncertain;} s_interval;
static struct {unsigned identity;} s_tx_rate_lease;
#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT 1
#define ESP32_MQUICKJS_WIFI_MAX_AP_CLIENTS 4
#define ESP32_MQUICKJS_WIFI_AP_BEACON_QUANTUM_TU 100
#define ESP32_MQUICKJS_WIFI_AP_BEACON_MAX_TU 60000
#define ESP32_MQUICKJS_WIFI_AP_DTIM_MAX 10
#define ESP32_MQUICKJS_WIFI_RADIO_STARTED 3
#define AP_STOP_IDLE 0
static struct {
    int lock,driver_state,ap_stop_phase;bool started,driver_owned,storage_configured,restart_required;
    bool promiscuous_claimed,ap_reopen_pending,ap_reopen_attempted,ap_reopen_config_pending;
    bool ap_reopen_quiesced,ap_reopen_restore_complete;
    const char *fault_stage,*cleanup_stage;int fault_error;
    wifi_mode_t effective_mode,event_live;
    unsigned generation,next_lease_identity,next_lifecycle_identity,wake_locks;
    unsigned ap_transition_application,ap_transition_station,ap_transition_access_point;
    unsigned clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_COUNT];
    struct { unsigned identity; } operation;
    esp32_mquickjs_wifi_radio_lifecycle_t lifecycle;
    wifi_radio_live_lease_t leases[WIFI_RADIO_MAX_LEASES];
} s_radio;
static int depth,critical,queries,allocs,live_alloc;
static bool fail_alloc,getter_error;
static wifi_config_t stored;
static int captures,capture_error;
static int esp32_mquickjs_wifi_ap_prestart_prepare(uint32_t generation,uint32_t identity,
    const wifi_config_t*requested,const wifi_config_t*previous,
    bool(*accept)(const wifi_config_t*,const wifi_config_t*)){
    assert(depth&&!critical&&generation==9&&identity==41&&s_radio.lifecycle.identity==41);
    assert(requested->ap.ssid[0]=='t'&&previous->ap.ssid[0]=='o'&&accept);++captures;return capture_error;
}
static int esp32_mquickjs_wifi_ap_prestart_release(uint32_t generation,uint32_t identity){
    (void)generation;(void)identity;assert(!"unexpected capture release");return 0;
}
static int wifi_radio_cleanup_fault(const char*stage,int error){(void)stage;assert(!"unexpected cleanup fault");return error;}
static void wifi_radio_operation_lock(void) { assert(!depth && !critical);depth=1; }
static void wifi_radio_operation_unlock(void) { assert(depth && !critical);depth=0; }
#define taskENTER_CRITICAL(p) do { (void)(p);assert(!critical);critical=1; } while(0)
#define taskEXIT_CRITICAL(p) do { (void)(p);assert(critical);critical=0; } while(0)
static void *checked_calloc(size_t n,size_t size) {
    assert(depth && !critical && n==1 && size==sizeof(wifi_config_t));allocs++;
    if(fail_alloc)return NULL;live_alloc++;return calloc(n,size);
}
static void checked_free(void *p) {
    assert(depth && !critical && live_alloc==1);
    for(size_t i=0;i<sizeof(wifi_config_t);i++)assert(!((unsigned char *)p)[i]);
    live_alloc--;free(p);
}
#define calloc checked_calloc
#define free checked_free
static int esp_wifi_get_mode(wifi_mode_t *m) { assert(depth && !critical);queries++;*m=WIFI_MODE_STA;return 0; }
static int wifi_radio_validate_regulatory_channel(uint8_t channel) { assert(depth&&!critical&&channel);return 0; }
static int esp_wifi_get_config(wifi_interface_t interface,wifi_config_t *c) {
    assert(depth && !critical && interface==WIFI_IF_AP);queries++;*c=stored;return getter_error?-7:0;
}
'''
