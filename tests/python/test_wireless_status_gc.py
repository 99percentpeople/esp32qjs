"""Real VM failures and moving GC in Wi-Fi scan/status and ESP-NOW status."""
import pathlib
import re
import sys
import tempfile
import unittest
sys.path.insert(0,str(pathlib.Path(__file__).parent))
from wireless_vm_fixture import ROOT,CORE,build,extract,run
from test_wifi_rx_target import unit
from wifi_connection_counter_fixture import connection_counter_code
from test_wifi_config_controls import HEADER, structure
SDK=r'''
typedef int esp_err_t,wifi_mode_t,wifi_ps_type_t,wifi_second_chan_t;
#define ESP_OK 0
#define ESPNOW_ADDRESS_BYTES 6
#define WIFI_SCAN_BSSID_STR_LEN 18
#define WIFI_SCAN_MAX_RESULTS 32
#define ESP32_MQUICKJS_WIFI_MAX_SCAN_RECORDS 32
#define ESP32_MQUICKJS_WIFI_SSID_MAX_LEN 32
#define MACSTR "%02x:%02x:%02x:%02x:%02x:%02x"
#define MAC2STR(a) a[0],a[1],a[2],a[3],a[4],a[5]
#define ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA 0
#define ESP32_MQUICKJS_WIFI_RADIO_CLIENT_ESPNOW 1
#define ESP32_MQUICKJS_WIFI_RADIO_CLIENT_CSI 2
#define ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION 3
#define ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP 4
#define ESP32_MQUICKJS_WIFI_RADIO_CLIENT_MONITOR 5
#define ESP32_MQUICKJS_WIFI_RADIO_CLIENT_RAW_TX 6
#define ESP32_MQUICKJS_WIFI_RADIO_CLIENT_VENDOR_IE 7
#define ESP32_MQUICKJS_WIFI_RADIO_CLIENT_ACTION 8
#define ESP32_MQUICKJS_WIFI_RADIO_CLIENT_COUNT 9
#define WIFI_SECOND_CHAN_NONE 0
#define ESP_NOW_MAX_DATA_LEN 250
#define ESP32_MQUICKJS_WIRELESS_TX_RECOVERING 1
#define ESP32_MQUICKJS_WIRELESS_TX_FAILED 2
#define ESP32_MQUICKJS_ESPNOW_TX_DROP_OLDEST_BATCH 1
#define ESP32_MQUICKJS_ESPNOW_TX_SLOT_NONE 255
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
typedef struct { bool configured,short_guard_interval,ersu,dcm;unsigned phy_mode,mcs; } espnow_peer_rate_config_t;
typedef struct { uint8_t address[6];unsigned channel;bool encrypted;espnow_peer_rate_config_t rate_config; } espnow_peer_slot_t;
typedef enum {WIFI_PHY_MODE_LR,WIFI_PHY_MODE_11B,WIFI_PHY_MODE_11G,WIFI_PHY_MODE_11A,
    WIFI_PHY_MODE_HT20,WIFI_PHY_MODE_HT40,WIFI_PHY_MODE_HE20,WIFI_PHY_MODE_VHT20} wifi_phy_mode_t;
/* SNAPSHOT_TYPES */
typedef struct {
    esp32_mquickjs_wifi_radio_config_result_t configuration,activation;
    int driver_state,requested_mode,cleanup_error,channel_observation_error,wake_lock_error;unsigned wake_locks,active_operations,fixed_channel_owners,conflicted_channel_owners,promiscuous_owners;bool promiscuous_identity_exhausted;const char *cleanup_stage;
    unsigned generation,channel_generation,primary_channel;unsigned clients[9];
    unsigned event_phase,event_identity,event_expected,event_seen,event_stopped,event_live;bool event_fence_pending;
    bool lifecycle_active,initialized,driver_owned,restart_required,starting,started,max_tx_power_available,power_save_available;
    const char *fault_stage;int fault_error,mode,storage,power_save,max_tx_power_quarter_dbm;
} esp32_mquickjs_wifi_radio_status_t;
static struct { atomic_uint dropped_driver_events; const char *cleanup_stage; int cleanup_error,sta_detach_error,radio_lease; } s_wifi_state = {.cleanup_stage="timer-delete",.cleanup_error=-2};
static const char *s_wifi_setup_stage="driver-events";
static int s_wifi_setup_error=-3;
static const char *esp32_mquickjs_wifi_radio_driver_state_name(int v) { (void)v;return "cleanup-pending"; }
static const char *wifi_radio_mode_to_string(int v) { (void)v;return "station"; }
static const char *wifi_power_save_to_string(int v) { (void)v;return "none"; }
static const char *wifi_reason_to_string(int v) { (void)v;return "fixture"; }
static const char *wifi_authmode_to_string(int v) { (void)v;return "wpa2-psk"; }
static const char *espnow_phy_mode_name(int v) { (void)v;return "ht"; }
static const char *esp_err_to_name(int v) { (void)v;return "ESP_FAIL"; }
static int esp32_mquickjs_wifi_get_status(esp32_mquickjs_wifi_status_t *s) { *s=(esp32_mquickjs_wifi_status_t){.initialized=true,.scan_draining=true,.scan_cleanup_error=-1,.connect_timer_error=-2,.connect_draining=true,.disconnect_cleanup_error=-3,.ssid="test-network",.connected=true,.link={.valid=true,.ssid="test-network",.ssid_len=12,.bssid={2,1,2,3,4,5},.channel=6,.aid=41}};return 0; }
static JSValue esp32_mquickjs_wifi_ap_status(JSContext *ctx) { return JS_NewObject(ctx); }
static JSValue esp32_mquickjs_wifi_action_status(JSContext *ctx) { return JS_NewObject(ctx); }
static JSValue esp32_mquickjs_wifi_raw_tx_status(JSContext *ctx) { return JS_NewObject(ctx); }
static JSValue esp32_mquickjs_wifi_watch_status(JSContext *ctx) { return JS_NewObject(ctx); }
static bool esp32_mquickjs_wifi_radio_rssi_request_status(esp32_mquickjs_wifi_rssi_request_t *s) {
    *s=(esp32_mquickjs_wifi_rssi_request_t){.generation=7,.revision=1,.requested_dbm=-70};return true;
}
static int esp32_mquickjs_wifi_radio_sample_station_link(const void *lease,const uint8_t bssid[6],
    uint8_t channel,esp32_mquickjs_wifi_link_sample_t *sample) {
    (void)lease;(void)bssid;(void)channel;
    *sample=(esp32_mquickjs_wifi_link_sample_t){.rssi_valid=true,.rssi=-50,.phy_valid=true,.phy=WIFI_PHY_MODE_HT20};return ESP_OK;
}
static int esp32_mquickjs_wifi_ap_netif_cleanup_error(void) { return -31; }
static int esp32_mquickjs_wifi_radio_get_status(esp32_mquickjs_wifi_radio_status_t *s) {
    *s=(esp32_mquickjs_wifi_radio_status_t){.configuration={.stage="mode",.error=-1,.mutation_attempted=true,.rollback_attempted=true,.rollback_complete=true},.activation={.stage="verify",.error=-2},.event_phase=5,.event_expected=1,.event_seen=0,.event_stopped=1,.event_fence_pending=true,.channel_observation_error=-3,.cleanup_stage="deinit",.cleanup_error=-2,.active_operations=1,.fixed_channel_owners=2,.conflicted_channel_owners=1,.generation=UINT32_MAX,.driver_owned=true,.restart_required=true,.fault_stage="start",.fault_error=-1,.primary_channel=6,.max_tx_power_available=true,.max_tx_power_quarter_dbm=71,.power_save_available=true,.clients={1,1,1}};return 0;
}
static int esp32_mquickjs_wifi_radio_get_channel(uint8_t *p,wifi_second_chan_t *s,uint32_t *g) { *p=6;*s=0;*g=1;return 0; }
typedef enum { WIFI_CIPHER_TYPE_NONE, WIFI_CIPHER_TYPE_WEP40, WIFI_CIPHER_TYPE_WEP104, WIFI_CIPHER_TYPE_TKIP, WIFI_CIPHER_TYPE_CCMP, WIFI_CIPHER_TYPE_TKIP_CCMP, WIFI_CIPHER_TYPE_AES_CMAC128, WIFI_CIPHER_TYPE_SMS4, WIFI_CIPHER_TYPE_GCMP, WIFI_CIPHER_TYPE_GCMP256, WIFI_CIPHER_TYPE_AES_GMAC128, WIFI_CIPHER_TYPE_AES_GMAC256 } wifi_cipher_type_t;
#define WIFI_STORAGE_RAM 1
#define WIFI_SECOND_CHAN_ABOVE 1
#define WIFI_SECOND_CHAN_BELOW 2
#define WIFI_ANT_ANT0 0
#define WIFI_ANT_ANT1 1
#define WIFI_COUNTRY_POLICY_AUTO 0
#define WIFI_COUNTRY_POLICY_MANUAL 1
#define CONFIG_SOC_WIFI_SUPPORT_5G 1
typedef struct { char cc[3];uint8_t schan,nchan;int8_t max_tx_power;int policy;uint32_t wifi_5g_channel_mask; } wifi_country_t;
typedef struct { uint8_t bssid[6],ssid[33];int rssi,primary,authmode,second,ant;wifi_cipher_type_t pairwise_cipher,group_cipher;wifi_country_t country;bool phy_11b,phy_11g,phy_11n,phy_11a,phy_11ac,phy_11ax,phy_lr,wps,ftm_responder,ftm_initiator; } wifi_ap_record_t;
static uint32_t esp32_mquickjs_wifi_radio_restart_snapshot_bytes(void) {return 0;}
static int driver_failure;
static void esp32_mquickjs_wifi_scan_results_consumed(void) {}
static int esp_wifi_scan_get_ap_num(uint16_t *n) { *n=2;return driver_failure==1 ? -1 : 0; }
static int esp_wifi_scan_get_ap_records(uint16_t *n,wifi_ap_record_t *r) {
    for(int i=0;i<*n;i++)r[i]=(wifi_ap_record_t){.ssid="test-network",.primary=6,.rssi=-50};
    return driver_failure==2 ? -1 : 0;
}
'''
MAIN=r'''
int main(int argc,char **argv) {
    (void)argc;int mode=atoi(argv[1]);collect=atoi(argv[2]);driver_failure=atoi(argv[3]);int total=1;
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(128*1024);JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        calls=0;fail_at=nth;moved_roots=0;
        espnow_peer_slot_t peer={.rate_config={.configured=true,.mcs=7},.channel=0,.encrypted=true};
        espnow_session_t session={.broadcast_rate_config=peer.rate_config,.tx_queue_capacity=4,.channel=6,.channel_generation=1};
        JSGCRef ref;JSValue *result=JS_PushGCRef(ctx,&ref);inject=true;
        if(mode==0)*result=wifi_make_status_object(ctx);
        if(mode==4)*result=js_wifi_driver_status(ctx,NULL,0,NULL);
        if(mode==1)*result=wifi_make_scan_results_array(ctx,32);
        if(mode==2)*result=espnow_peer_status_to_js(ctx,&peer);
        if(mode==3)*result=espnow_status_to_js(ctx,&session);
        inject=false;if(!nth)total=calls;
        if(nth || driver_failure) { assert(JS_IsException(*result) && JS_HasException(ctx));(void)JS_GetException(ctx); }
        else {
            assert(!JS_IsException(*result) && !JS_HasException(ctx));if(collect)assert(moved_roots>0);
            if(mode==0 || mode==4) {
                if(mode==0) {
                    int32_t channel;
                    assert(!JS_ToInt32(ctx,&channel,JS_GetPropertyStr(ctx,*result,"channel")) && channel==6);
                    assert(!JS_ToInt32(ctx,&channel,JS_GetPropertyStr(ctx,*result,"aid")) && channel==41);
                    assert(!JS_ToInt32(ctx,&channel,JS_GetPropertyStr(ctx,*result,"rssi")) && channel==-50);
                    *result=JS_GetPropertyStr(ctx,*result,"radio");
                }
                JSValue configuration=JS_GetPropertyStr(ctx,*result,"configuration");
                assert(JS_GetPropertyStr(ctx,configuration,"rollbackComplete")==JS_TRUE);
                int32_t mask;assert(!JS_ToInt32(ctx,&mask,JS_GetPropertyStr(ctx,*result,"eventStoppedMask")) && mask==1);
                assert(!JS_ToInt32(ctx,&mask,JS_GetPropertyStr(ctx,*result,"eventSeenMask")) && mask==0);
                JSCStringBuf phase_buf;const char *phase=JS_ToCString(ctx,JS_GetPropertyStr(ctx,*result,"eventPhase"),&phase_buf);
                assert(phase && !strcmp(phase,"restart"));
                *result=JS_GetPropertyStr(ctx,*result,"policies");
                *result=JS_GetPropertyStr(ctx,*result,"dynamicCarrierSense");
                assert(JS_IsNull(JS_GetPropertyStr(ctx,*result,"value")));
                assert(JS_GetPropertyStr(ctx,*result,"lastAcceptedValue")==JS_FALSE);
                assert(JS_GetPropertyStr(ctx,*result,"uncertain")==JS_TRUE);
                int32_t n;assert(!JS_ToInt32(ctx,&n,JS_GetPropertyStr(ctx,*result,"revision")) && n==2);
            }
            if(mode==1) { int32_t n;assert(!JS_ToInt32(ctx,&n,JS_GetPropertyStr(ctx,*result,"length")) && n==2); }
        }
        JS_PopGCRef(ctx,&ref);JS_GC(ctx);assert(root_count==0 && native_live==0);JS_FreeContext(ctx);free(heap);
    }
    printf("status mode=%d gc=%d boundaries=%d\n",mode,collect,total);
}
'''
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
