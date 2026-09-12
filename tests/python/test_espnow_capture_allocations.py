"""Production open capture, pools and cleanup with deterministic SDK failures."""
import pathlib
import re
import sys
import tempfile
import unittest
sys.path.insert(0,str(pathlib.Path(__file__).parent))
from wireless_vm_fixture import ROOT,CORE,build,extract,run
SDK=r'''
#include "esp32_mquickjs_native_pool.h"
#include "esp32_mquickjs_espnow_tx_queue.h"
#define MALLOC_CAP_INTERNAL 2
#define ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL 0
#define ESP32_MQUICKJS_MEMORY_EXTERNAL 1
#define ESP32_MQUICKJS_WIRELESS_TX_READY 0
#define ESPNOW_TX_TASK_STACK_BYTES 1024
#define CONFIG_ESP32_MQUICKJS_ESPNOW_MAX_PEERS 1
#define ESPNOW_LIFECYCLE_CLOSED 0
#define ESPNOW_LIFECYCLE_OPENING 1
#define ESPNOW_LIFECYCLE_CLOSING 2
#define ESP32_MQUICKJS_EVENT_QUEUE_DROP_NEWEST 0
#define ESP32_MQUICKJS_WIFI_RADIO_CLIENT_ESPNOW 1
#define WIFI_MODE_STA 1
#define ESP_NOW_KEY_LEN 16
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERR_TIMEOUT 2
typedef int esp_err_t;
typedef struct {unsigned identity;} esp32_mquickjs_wifi_interval_token_t;
typedef struct { bool acquired; } esp32_mquickjs_wifi_radio_lease_t;
#define ESP_WIFI_CONNECTIONLESS_INTERVAL_DEFAULT_MODE 0
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
#define ESP_LOGE(...) ((void)0)
typedef int portMUX_TYPE,esp32_mquickjs_event_queue_t;
typedef void *TaskHandle_t;
typedef struct { int unused; } espnow_receive_event_t;
typedef struct { uint8_t *payload; } espnow_rx_slot_t;
typedef struct { uint8_t address[6];unsigned length; } espnow_tx_packet_t;
typedef struct { uint8_t lmk[16]; } espnow_peer_slot_t;
typedef struct { bool configured; } espnow_peer_rate_config_t;
typedef struct {
    JSContext *ctx;void *runtime;int token,err;int64_t completed_at_us;
    atomic_bool completed;bool transferred,event_queue_rooted,has_pmk,power_save_enabled,channel_fixed;
    uint8_t pmk[16];JSGCRef event_queue_ref;unsigned generation,send_timeout_ms,channel,wake_window_ms,wake_interval_ms,receive_capacity,max_payload_bytes,tx_queue_capacity;
    espnow_peer_rate_config_t broadcast_rate_config;esp32_mquickjs_espnow_tx_overflow_t tx_overflow;
} esp32_mquickjs_future_driver_state_t;
'''
BOUNDARIES=r'''
static espnow_session_t s_espnow_session;static atomic_uint s_espnow_next_generation=1;
static unsigned queue_capacity,radio_live,queue_retains;static int queue_value;static bool queue_closed,parse_busy;
static bool boundary_failure(void) {return inject && ++calls==fail_at;}
static void *esp32_mquickjs_memory_payload_alloc(const char *name,size_t n,int policy) { (void)name;return heap_caps_malloc(n,policy); }
static void *esp32_mquickjs_memory_payload_calloc(const char *name,size_t n,size_t size,int policy) { (void)name;return heap_caps_calloc(n,size,policy); }
static void *esp32_mquickjs_get_active_runtime(void) {return (void *)1;}
/* Parsing has its own validation tests; this fixture fixes bounded options so
 * every resource creation/registration boundary in production capture runs. */
static bool espnow_parse_open_options(JSContext *ctx,int argc,JSGCRef *argv,esp32_mquickjs_future_driver_state_t *s) {
    (void)ctx;(void)argc;(void)argv;s->receive_capacity=2;s->max_payload_bytes=64;s->tx_queue_capacity=queue_capacity;s->has_pmk=true;memset(s->pmk,0x5a,16);if(parse_busy)atomic_store(&s_espnow_session.cleanup_scheduled,true);return true;
}
static JSValue espnow_receive_event_to_js(JSContext *ctx,const void *e,void *p) { (void)ctx;(void)e;(void)p;return JS_UNDEFINED; }
static void espnow_receive_event_drop(void *e,void *p) { (void)e;(void)p; }
static void espnow_event_queue_close(void *p) { (void)p; }
static JSValue esp32_mquickjs_event_queue_new(JSContext *ctx,void *r,size_t size,unsigned cap,int policy,void *convert,void *drop,void *close,void *opaque) {
    (void)r;(void)size;(void)cap;(void)policy;(void)convert;(void)drop;(void)close;(void)opaque;return JS_NewObject(ctx);
}
static void *esp32_mquickjs_event_queue_from_value(JSContext *ctx,JSValue v) { (void)ctx;(void)v;return &queue_value; }
static bool esp32_mquickjs_event_queue_retain(void *q) {assert(q);if(boundary_failure())return false;queue_retains++;return true;}
static void esp32_mquickjs_event_queue_release(void *q) {assert(q && queue_retains);queue_retains--;}
static bool esp32_mquickjs_event_queue_dispose(JSContext *ctx,JSValue v) { (void)ctx;(void)v;return true; }
static bool esp32_mquickjs_event_queue_discard_all(void *p) { (void)p;return true; }
static bool esp32_mquickjs_event_queue_is_closed(void *p) { (void)p;return queue_closed; }
static bool esp32_mquickjs_event_queue_close(void *p) { (void)p;queue_closed=true;return true; }
static int esp32_mquickjs_wifi_radio_acquire(int client,int mode,esp32_mquickjs_wifi_radio_lease_t *lease) { (void)client;(void)mode;if(boundary_failure())return 1;lease->acquired=true;radio_live++;return 0; }
static void esp32_mquickjs_wifi_radio_release(esp32_mquickjs_wifi_radio_lease_t *lease) { if(lease->acquired){assert(radio_live);radio_live--;lease->acquired=false;} }
static int64_t esp_timer_get_time(void) {return 1;}
static bool esp32_mquickjs_future_wake(void *r,int t) { (void)r;(void)t;return true; }
static void espnow_notify_tx_worker(espnow_session_t *s) { (void)s; }
static void espnow_request_reap(espnow_session_t *s) { (void)s;assert(0); }
static bool espnow_schedule_background_close(espnow_session_t *s) { (void)s;assert(!"capture cleanup should be synchronous before driver start");return false; }
static bool esp32_mquickjs_wireless_close_can_release(unsigned n,bool quiescent) {return n==0 && quiescent;}
static void vTaskDelete(void *p) { (void)p;assert(0); }
static int esp_now_unregister_recv_cb(void) {assert(0);return 0;}
static int esp_now_unregister_send_cb(void) {assert(0);return 0;}
static int esp_now_set_wake_window(int n) { (void)n;assert(0);return 0;}
static int esp32_mquickjs_wifi_radio_interval_release(esp32_mquickjs_wifi_radio_lease_t *lease,esp32_mquickjs_wifi_interval_token_t *token) {(void)lease;assert(!token->identity);return ESP_OK;}
static int esp_now_deinit(void) {assert(0);return 0;}
static void espnow_note_native_deinit(void) {assert(0);}
'''
MAIN=r'''
int main(int argc,char **argv) {
    queue_capacity=atoi(argv[1]);collect=atoi(argv[2]);int total=1;
    if(argc>3) {
        void *heap=malloc(128*1024);JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        parse_busy=true;esp32_mquickjs_future_driver_state_t *state=NULL;
        assert(!espnow_open_capture(ctx,NULL,0,NULL,&state) && !state && JS_HasException(ctx));
        assert(s_espnow_session.cleanup_scheduled && !radio_live && !queue_retains && !native_live);
        (void)JS_GetException(ctx);JS_FreeContext(ctx);free(heap);return 0;
    }
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(128*1024);JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        memset(&s_espnow_session,0,sizeof(s_espnow_session));queue_closed=false;
        calls=0;fail_at=nth;inject=true;esp32_mquickjs_future_driver_state_t *state=NULL;
        bool ok=espnow_open_capture(ctx,NULL,0,NULL,&state);inject=false;
        if(!nth)total=calls;
        if(nth) {assert(!ok && !state && JS_HasException(ctx));(void)JS_GetException(ctx);}
        else {assert(ok && state);espnow_open_release(state);for(unsigned i=0;i<16;i++)assert(state->pmk[i]==0);heap_caps_free(state);}
        assert(s_espnow_session.lifecycle==ESPNOW_LIFECYCLE_CLOSED);
        for(unsigned i=0;i<16;i++)assert(s_espnow_session.pmk[i]==0);
        assert(!queue_retains && !radio_live);JS_GC(ctx);assert(!root_count && !native_live);JS_FreeContext(ctx);free(heap);
    }
    printf("ESP-NOW capture queue=%u gc=%d boundaries=%d\n",queue_capacity,collect,total);
}
'''
class EspnowCaptureAllocations(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory();cls.addClassCleanup(cls.temp.cleanup)
        now=(ROOT/'components/esp32_mquickjs/src/modules/espnow/esp32_mquickjs_espnow.c').read_text()
        names=['espnow_clear_peer','espnow_reset_session_storage','espnow_begin_close','espnow_close_failure','espnow_restore_power_save','espnow_finish_close','espnow_close_native','espnow_allocate_receive_pool','espnow_allocate_tx_queue','espnow_open_release','espnow_closed_and_quiescent','espnow_open_capture']
        bodies='\n'.join(extract(now,n) for n in names)
        fields=set(re.findall(r'session->(\w+)',bodies))
        special={'interval_token':'esp32_mquickjs_wifi_interval_token_t ','rx_slots':'espnow_rx_slot_t *','rx_payloads':'uint8_t *','tx_payloads':'uint8_t *','tx_staging':'uint8_t *','tx_task_stack':'uint8_t *','tx_packets':'espnow_tx_packet_t *','tx_links':'esp32_mquickjs_espnow_tx_slot_link_t *','tx_queue':'esp32_mquickjs_espnow_tx_queue_t ','broadcast_rate_config':'espnow_peer_rate_config_t ','event_queue':'esp32_mquickjs_event_queue_t *','runtime':'void *','tx_task':'_Atomic(void *) ','active_send':'_Atomic(esp32_mquickjs_future_driver_state_t *) ','pending_tracked_send':'_Atomic(esp32_mquickjs_future_driver_state_t *) ','active_close':'_Atomic(esp32_mquickjs_future_driver_state_t *) ','rx_free':'esp32_mquickjs_native_pool_t ','radio_lease':'esp32_mquickjs_wifi_radio_lease_t ','cleanup_stage':'_Atomic(const char *) ','cleanup_scheduled':'atomic_bool '}
        native='typedef struct { uint8_t pmk[16];espnow_peer_slot_t peers[1];'+''.join(special.get(n,'atomic_uint ')+n+';' for n in sorted(fields-{'pmk','peers'}))+'} espnow_session_t;\n'
        secure=extract((CORE/'esp32_mquickjs_wireless_core.c').read_text(),'esp32_mquickjs_wireless_secure_zero')
        cls.binary=build(cls.temp.name,SDK+native+BOUNDARIES+(CORE/'esp32_mquickjs_native_pool.c').read_text()+(ROOT/'components/esp32_mquickjs/src/modules/espnow/esp32_mquickjs_espnow_tx_queue.c').read_text()+secure+bodies,MAIN)

    def test_all_open_allocations_queue_retain_radio_reservation_and_secret_cleanup(self):
        for capacity in (0,2):
            for gc in (0,1):
                with self.subTest(capacity=capacity,gc=gc):run([str(self.binary),str(capacity),str(gc)])

    def test_capture_rechecks_native_cleanup_reservation_after_option_getters(self):
        run([str(self.binary), '0', '0', 'parse-busy'])
