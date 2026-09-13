
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERR_TIMEOUT 2
#define ESP_WIFI_CONNECTIONLESS_INTERVAL_DEFAULT_MODE 0
#define ESPNOW_LIFECYCLE_CLOSED 0
#define ESPNOW_LIFECYCLE_ACTIVE 1
#define ESPNOW_LIFECYCLE_CLOSING 2
#define ESPNOW_OPERATION_CLOSE_SESSION 3
#define ESPNOW_OPERATION_CLOSE_PEER 4
#define ESPNOW_OPERATION_ADD_PEER 5
#define ESPNOW_OPERATION_UPDATE_PEER 6
#define CONFIG_ESP32_MQUICKJS_ESPNOW_MAX_PEERS 1
#define JS_CLASS_ESPNOW_SESSION 1
#define JS_CLASS_ESPNOW_PEER 2
#define ESP_LOGE(...) ((void)0)
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
typedef int esp_err_t,JSContext,JSValue,esp32_mquickjs_event_queue_t;
typedef void *TaskHandle_t;
typedef struct {JSValue val;} JSGCRef;
typedef struct {bool acquired;} radio_lease_t;
typedef struct {unsigned identity;} esp32_mquickjs_wifi_interval_token_t;
typedef struct {bool allocated,reserved;uint32_t generation;} espnow_peer_slot_t;
typedef struct esp32_mquickjs_future_driver_state {
    JSContext *ctx;void *runtime;int token,err,operation;uint32_t generation,peer_generation,peer_index;
    int64_t completed_at_us;_Atomic bool completed;bool result_bool,cancelled,peer_reserved,event_queue_rooted,owner_rooted;
    const char *failed_step;JSGCRef event_queue_ref,owner_ref;uint8_t lmk[16],address[6];
} esp32_mquickjs_future_driver_state_t;
typedef struct {
    int lock;_Atomic int lifecycle;void *runtime;uint32_t generation;uint8_t channel;
    radio_lease_t radio_lease;esp32_mquickjs_event_queue_t *event_queue;bool event_queue_retained;
    _Atomic(TaskHandle_t) tx_task;_Atomic bool tx_task_exited,tx_task_stop;
    bool receive_callback_registered,send_callback_registered,now_initialized,power_save_enabled,broadcast_peer_added;
    bool reaper_registered,power_save_cleanup_required;esp32_mquickjs_wifi_interval_token_t interval_token;uint8_t close_power_phase;_Atomic unsigned callbacks_active;_Atomic bool cleanup_scheduled;
    _Atomic esp_err_t cleanup_error;_Atomic(const char *) cleanup_stage;
    _Atomic(esp32_mquickjs_future_driver_state_t *) active_close,pending_tracked_send;
    unsigned tx_queue_capacity;int tx_queue;espnow_peer_slot_t peers[1];
} espnow_session_t;
static espnow_session_t s_espnow_session;
static int queue_value,s_espnow_closed_session_ref,s_espnow_closed_peer_ref;
static unsigned stages[7],failed_stage,resets,queue_releases,task_deletes,native_deinits,reaps,root_deletes,disposals,retires,frees;
static bool queue_closed,accept_worker,queued;
static const char *thrown_code;
static int sdk(unsigned stage) {assert(atomic_load(&s_espnow_session.cleanup_scheduled));++stages[stage];return failed_stage==stage ? 70+stage : ESP_OK;}
static int esp_now_unregister_recv_cb(void) {return sdk(1);}
static int esp_now_unregister_send_cb(void) {return sdk(2);}
static int esp_now_set_wake_window(uint16_t v) {assert(v==UINT16_MAX);return sdk(3);}
static int esp32_mquickjs_wifi_radio_interval_release(radio_lease_t *lease,esp32_mquickjs_wifi_interval_token_t *token) {assert(lease->acquired);if(!token->identity)return ESP_OK;int err=sdk(4);if(!err)token->identity=0;return err;}
static int esp_now_deinit(void) {return sdk(5);}
static void espnow_note_native_deinit(void) {++native_deinits;}
static void esp32_mquickjs_wifi_radio_release(radio_lease_t *lease) {if(!lease->acquired)return;if(sdk(6)==ESP_OK)lease->acquired=false;}
static void espnow_reset_session_storage(espnow_session_t *s) {
    assert(!s->radio_lease.acquired && !s->now_initialized && !s->receive_callback_registered && !s->send_callback_registered && !atomic_load(&s->callbacks_active));
    ++resets;s->event_queue=NULL;
}
static void esp32_mquickjs_event_queue_release(void *q) {assert(q==&queue_value);++queue_releases;}
static bool esp32_mquickjs_event_queue_is_closed(void *q) {(void)q;return queue_closed;}
static bool esp32_mquickjs_event_queue_close(void *q) {(void)q;queue_closed=true;return true;}
static bool esp32_mquickjs_wireless_close_can_release(unsigned n,bool done) {return !n && done;}
static void vTaskDelete(TaskHandle_t t) {assert(t);++task_deletes;}
static void vTaskDelay(int t) {(void)t;atomic_store(&s_espnow_session.tx_task_exited,true);atomic_store(&s_espnow_session.callbacks_active,0);}
static void espnow_notify_tx_worker(espnow_session_t *s) {(void)s;}
static int64_t esp_timer_get_time(void) {return 10;}
static void esp32_mquickjs_future_wake(void *r,int t) {(void)r;(void)t;}
static void esp32_mquickjs_espnow_tx_queue_discard_pending(int *q,uint32_t *n) {(void)q;*n=0;}
static void espnow_request_reap(espnow_session_t *s) {assert(s==&s_espnow_session);++reaps;}
static bool esp32_mquickjs_submit_background_worker(void (*worker)(void *),void *opaque) {(void)worker;assert(opaque==&s_espnow_session);queued=accept_worker;return accept_worker;}
static JSValue JS_ThrowInternalError(JSContext *ctx,const char *s) {(void)ctx;(void)s;return -1;}
static JSValue JS_NewBool(bool b) {return b;}
static JSValue espnow_throw_error(JSContext *ctx,const char *code,int err,const uint8_t *addr,int channel) {(void)ctx;(void)addr;(void)channel;thrown_code=code;return -err;}
static JSValue espnow_new_peer_handle(JSContext *ctx,unsigned i,void *p) {(void)ctx;(void)i;(void)p;assert(0);return 0;}
static JSValue espnow_peer_status_to_js(JSContext *ctx,void *p) {(void)ctx;(void)p;assert(0);return 0;}
static void espnow_clear_peer(espnow_peer_slot_t *p) {(void)p;assert(0);}
static bool esp32_mquickjs_event_queue_dispose(JSContext *ctx,JSValue v) {(void)ctx;(void)v;++disposals;return true;}
static void JS_DeleteGCRef(JSContext *ctx,JSGCRef *r) {(void)ctx;(void)r;++root_deletes;}
static void espnow_retire_handle(JSContext *ctx,JSValue v,int cls,void *ref) {(void)ctx;(void)v;(void)cls;(void)ref;++retires;}
static void esp32_mquickjs_wireless_secure_zero(void *p,size_t n) {memset(p,0,n);}
static void heap_caps_free(void *p) {++frees;free(p);}
