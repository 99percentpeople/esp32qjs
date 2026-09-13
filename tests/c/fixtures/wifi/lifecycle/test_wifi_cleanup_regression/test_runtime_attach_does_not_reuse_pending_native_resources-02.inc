
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define ESP_OK 0
typedef int JSContext,esp32_mquickjs_runtime_t;
static struct { const char *cleanup_stage;int cleanup_error;bool runtime_cleanup_pending,connect_in_progress,connect_draining;struct { bool connected; } status;unsigned scan_generation,connect_generation; } s_wifi_state;
static esp32_mquickjs_runtime_t *s_wifi_runtime;
static void wifi_lock(void) {}
static void wifi_unlock(void) {}
static bool esp32_mquickjs_init_wifi_ap_runtime(JSContext *ctx) { (void)ctx;return true; }
static bool esp32_mquickjs_init_wifi_wake_runtime(JSContext *ctx) { (void)ctx;return true; }
static bool cleanup_fails;
static int cleanup_calls,attached,pollers,errors,connect_cancels,scan_cancels;
static int esp32_mquickjs_wifi_cancel_connect(unsigned generation) { (void)generation;connect_cancels++;return 0; }
static int esp32_mquickjs_wifi_cancel_scan(unsigned generation) { (void)generation;scan_cancels++;return 0; }
static int wifi_finish_runtime_cleanup(bool wait) {
    (void)wait;
    cleanup_calls++;
    if(cleanup_fails) return s_wifi_state.cleanup_error;
    s_wifi_state.runtime_cleanup_pending=false;s_wifi_state.cleanup_stage=NULL;s_wifi_state.cleanup_error=0;return 0;
}
static const char *esp_err_to_name(int err) { (void)err;return "fixture"; }
static int JS_ThrowInternalError(JSContext *c,const char *fmt,...) { (void)c;(void)fmt;errors++;return 0; }
static bool esp32_mquickjs_init_wifi_future_runtime(JSContext *c,esp32_mquickjs_runtime_t *r) { (void)c;(void)r;attached++;return true; }
static bool esp32_mquickjs_init_wifi_raw_tx_runtime(JSContext *c,esp32_mquickjs_runtime_t *r) { (void)c;(void)r;return true; }
static bool esp32_mquickjs_init_wifi_action_runtime(JSContext *c,esp32_mquickjs_runtime_t *r) { (void)c;(void)r;return true; }
static int wifi_driver_event_poller;
static bool esp32_mquickjs_register_async_poller(esp32_mquickjs_runtime_t *r,int p,void *o) { (void)r;(void)p;(void)o;pollers++;return true; }
