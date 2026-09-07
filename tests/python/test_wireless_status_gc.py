"""Real VM failures and moving GC in Wi-Fi scan/status and ESP-NOW status."""
import pathlib
import re
import sys
import tempfile
import unittest
sys.path.insert(0,str(pathlib.Path(__file__).parent))
from wireless_vm_fixture import ROOT,CORE,build,extract,run
SDK=r'''
typedef int esp_err_t,wifi_mode_t,wifi_ps_type_t,wifi_second_chan_t;
#define ESP_OK 0
#define ESPNOW_ADDRESS_BYTES 6
#define WIFI_SCAN_BSSID_STR_LEN 18
#define WIFI_SCAN_MAX_RESULTS 64
#define MACSTR "%02x:%02x:%02x:%02x:%02x:%02x"
#define MAC2STR(a) a[0],a[1],a[2],a[3],a[4],a[5]
#define ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA 0
#define ESP32_MQUICKJS_WIFI_RADIO_CLIENT_ESPNOW 1
#define ESP32_MQUICKJS_WIFI_RADIO_CLIENT_CSI 2
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
typedef struct { bool initialized,connected,scanning;char ssid[33];int last_disconnect_reason; } esp32_mquickjs_wifi_status_t;
typedef struct {
    unsigned generation,channel_generation,primary_channel;unsigned clients[3];
    bool initialized,driver_owned,restart_required,starting,started,max_tx_power_available,power_save_available;
    const char *fault_stage;int fault_error,mode,power_save,max_tx_power_quarter_dbm;
} esp32_mquickjs_wifi_radio_status_t;
static struct { atomic_uint dropped_driver_events; } s_wifi_state;
static const char *wifi_radio_mode_to_string(int v) { (void)v;return "station"; }
static const char *wifi_power_save_to_string(int v) { (void)v;return "none"; }
static const char *wifi_reason_to_string(int v) { (void)v;return "fixture"; }
static const char *wifi_authmode_to_string(int v) { (void)v;return "wpa2-psk"; }
static const char *espnow_phy_mode_name(int v) { (void)v;return "ht"; }
static const char *esp_err_to_name(int v) { (void)v;return "ESP_FAIL"; }
static int esp32_mquickjs_wifi_get_status(esp32_mquickjs_wifi_status_t *s) { *s=(esp32_mquickjs_wifi_status_t){.initialized=true,.ssid="test-network"};return 0; }
static int esp32_mquickjs_wifi_radio_get_status(esp32_mquickjs_wifi_radio_status_t *s) {
    *s=(esp32_mquickjs_wifi_radio_status_t){.generation=UINT32_MAX,.driver_owned=true,.restart_required=true,.fault_stage="start",.fault_error=-1,.primary_channel=6,.max_tx_power_available=true,.max_tx_power_quarter_dbm=71,.power_save_available=true,.clients={1,1,1}};return 0;
}
static int esp32_mquickjs_wifi_radio_get_channel(uint8_t *p,wifi_second_chan_t *s,uint32_t *g) { *p=6;*s=0;*g=1;return 0; }
typedef struct { uint8_t bssid[6],ssid[33];int rssi,primary,authmode; } wifi_ap_record_t;
static int driver_failure;
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
        if(mode==1)*result=wifi_make_scan_results_array(ctx);
        if(mode==2)*result=espnow_peer_status_to_js(ctx,&peer);
        if(mode==3)*result=espnow_status_to_js(ctx,&session);
        inject=false;if(!nth)total=calls;
        if(nth || driver_failure) { assert(JS_IsException(*result) && JS_HasException(ctx));(void)JS_GetException(ctx); }
        else {
            assert(!JS_IsException(*result) && !JS_HasException(ctx));if(collect)assert(moved_roots>0);
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
        fields=set(re.findall(r'session->(\w+)',status))-{'broadcast_rate_config'}
        native='typedef struct { espnow_peer_rate_config_t broadcast_rate_config;'+''.join('atomic_uint '+n+';' for n in sorted(fields))+'} espnow_session_t;\n'
        native+='static void espnow_tx_queue_depth_locked(espnow_session_t *s,uint32_t *a,uint32_t *b) { (void)s;*a=2;*b=3; }\n'
        bodies='\n'.join(extract(wifi,n) for n in ['wifi_make_status_object','wifi_make_scan_entry_object','wifi_make_scan_results_array'])
        bodies+=extract((CORE/'esp32_mquickjs_wireless_core.c').read_text(),'esp32_mquickjs_wireless_format_address')
        bodies+='\n'.join(extract(espnow,n) for n in ['espnow_format_address','espnow_peer_rate_config_to_js','espnow_peer_status_to_js'])+status
        cls.binary=build(cls.temp.name,SDK+native+bodies,MAIN)

    def test_status_and_scan_allocation_failure_and_moving_gc(self):
        for mode in range(4):
            for gc in (0,1):
                with self.subTest(mode=mode,gc=gc):run([str(self.binary),str(mode),str(gc),'0'])

    def test_scan_driver_failure_cleanup(self):
        for failure in (1,2):run([str(self.binary),'1','1',str(failure)])
