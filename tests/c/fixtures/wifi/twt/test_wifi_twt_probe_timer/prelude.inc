
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define CONFIG_SOC_WIFI_HE_SUPPORT 1
#define CONFIG_IDF_TARGET_ESP32C5 1
#define IRAM_ATTR
#define DRAM_ATTR
#define _Static_assert(...)
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
typedef int esp_err_t;
typedef unsigned portMUX_TYPE;
typedef struct {uint32_t timer_expire;void *timer_arg;} ETSTimer;
typedef void *esp_timer_handle_t;
typedef struct {void (*callback)(void *);void *arg;int dispatch_method;const char *name;} esp_timer_create_args_t;
#define ESP_TIMER_TASK 0
esp_err_t esp_timer_create(const esp_timer_create_args_t *,esp_timer_handle_t *);
esp_err_t esp_timer_stop(esp_timer_handle_t);
esp_err_t esp_timer_delete(esp_timer_handle_t);
esp_err_t esp_timer_start_once(esp_timer_handle_t,uint64_t);
ETSTimer itwt_probe_timer;
#define portMUX_INITIALIZER_UNLOCKED 0
static unsigned locked;
#define portENTER_CRITICAL_SAFE(lock) do {assert(!locked);locked=1;} while(0)
#define portEXIT_CRITICAL_SAFE(lock) do {assert(locked==1);locked=0;} while(0)
static uintptr_t current_node=0x100000;
static uint8_t current_phase=1;
static bool current_active=true;
/* Non-probe dispatch is a boundary here; its real eight-slot implementation
 * is covered separately by test_wifi_twt_setup_timer. */
static bool esp32_mquickjs_wifi_twt_setup_timer_setfn(void *t,void *f,void *a) {(void)t;(void)f;(void)a;return false;}
static bool esp32_mquickjs_wifi_twt_setup_timer_disarm(void *t) {(void)t;return false;}
static bool esp32_mquickjs_wifi_twt_setup_timer_done(void *t) {(void)t;return false;}
static bool esp32_mquickjs_wifi_twt_setup_timer_arm(void *t,uint64_t u,bool r) {(void)t;(void)u;(void)r;return false;}
static bool esp32_mquickjs_wifi_btwt_timer_setfn(void *t,void *f,void *a) {(void)t;(void)f;(void)a;return false;}
static bool esp32_mquickjs_wifi_btwt_timer_disarm(void *t) {(void)t;return false;}
static bool esp32_mquickjs_wifi_btwt_timer_done(void *t) {(void)t;return false;}
static bool esp32_mquickjs_wifi_btwt_timer_arm(void *t,uint64_t u,bool r) {(void)t;(void)u;(void)r;return false;}
static bool esp32_mquickjs_wifi_twt_information_timer_setfn(void *t,void *f,void *a) {(void)t;(void)f;(void)a;return false;}
static bool esp32_mquickjs_wifi_twt_information_timer_disarm(void *t) {(void)t;return false;}
static bool esp32_mquickjs_wifi_twt_information_timer_done(void *t) {(void)t;return false;}
static bool esp32_mquickjs_wifi_twt_information_timer_arm(void *t,uint64_t u,bool r) {(void)t;(void)u;(void)r;return false;}
static unsigned aborted;
static void cancelled_wake(void);
static esp_err_t control_error;
static void esp32_mquickjs_wifi_twt_probe_result_failed_native(esp_err_t error) {assert(!locked);control_error=error;}
static bool esp32_mquickjs_wifi_twt_sdk_probe_capture_native(const void *arg,uintptr_t *node,uint8_t *phase) {
    assert(!locked);
    if(!current_active || !current_node || arg!=(void *)(current_node+1057) || current_phase>1)return false;
    *node=current_node;*phase=current_phase;return true;
}
static bool esp32_mquickjs_wifi_twt_sdk_probe_matches_native(uintptr_t node,uint8_t phase) {
    assert(!locked);return current_active && current_node && node==current_node && phase==current_phase;
}
static bool esp32_mquickjs_wifi_twt_sdk_probe_active_native(void) {assert(!locked);return current_active;}
static bool esp32_mquickjs_wifi_twt_sdk_probe_abort_native(uintptr_t node,uint8_t phase) {
    if(!esp32_mquickjs_wifi_twt_sdk_probe_matches_native(node,phase))return false;
    current_active=false;++aborted;cancelled_wake();return true;
}
