
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define CONFIG_SOC_WIFI_HE_SUPPORT 1
#define CONFIG_IDF_TARGET_ESP32C5 1
#define IRAM_ATTR
#define DRAM_ATTR
#define _Static_assert(...)
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
#define ESP_OK 0
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
typedef int esp_err_t;
#define WIFI_EVENT_BTWT_SETUP 33
#define WIFI_EVENT_BTWT_TEARDOWN 34
#ifndef TEST_REAL_TEARDOWN_TX
static esp_err_t esp32_mquickjs_wifi_btwt_event_post(int id,const void *data,size_t size) {
    (void)id;(void)data;(void)size;assert(0);return ESP_ERR_INVALID_ARG;
}
#else
esp_err_t esp32_mquickjs_wifi_btwt_event_post(int,const void *,size_t);
static esp_err_t td_observe(const void *,size_t);
#endif
#define WIFI_EVENT_ITWT_PROBE 30
#define WIFI_EVENT_ITWT_SETUP 28
#define WIFI_EVENT_ITWT_SUSPEND 31
typedef struct {esp_err_t status;uint8_t flow_id_bitmap;uint32_t actual_suspend_time_ms[8];} wifi_event_sta_itwt_suspend_t;
#define WIFI_EVENT_ITWT_TEARDOWN 29
static esp_err_t esp32_mquickjs_wifi_twt_setup_result_post(const void *data,size_t size) {
    (void)data;(void)size;assert(0);return ESP_ERR_INVALID_ARG;
}
static esp_err_t esp32_mquickjs_wifi_twt_setup_result_teardown_post(const void *data,size_t size) {
    (void)data;(void)size;assert(0);return ESP_ERR_INVALID_ARG;
}
#define WIFI_EVENT ((const char *)19)
#define ESP_EVENT_DECLARE_BASE(name)
#define ESP32QJS_WIFI_RADIO_CONTROL_EVENT ((const char *)20)
typedef enum {ITWT_PROBE_FAIL,ITWT_PROBE_SUCCESS,ITWT_PROBE_TIMEOUT,ITWT_PROBE_STA_DISCONNECTED} wifi_itwt_probe_status_t;
typedef struct {wifi_itwt_probe_status_t status;uint8_t reason;} wifi_event_sta_itwt_probe_t;
typedef unsigned portMUX_TYPE;
typedef void *TaskHandle_t;
static TaskHandle_t current_task=(void *)31;
static bool in_isr;
static TaskHandle_t xTaskGetCurrentTaskHandle(void) {return current_task;}
static bool xPortInIsrContext(void) {return in_isr;}
#define portMUX_INITIALIZER_UNLOCKED 0
static unsigned locked,allocations,output_calls,recycle_calls;
static unsigned fail_allocation_at;
static void (*output_hook)(void *);
static bool fail_allocate,early_recycle,reuse_during_recycle;
static int native_result;
static int admission_error,last_callback_status;
static int timer_finish_error;
static int esp32_mquickjs_wifi_twt_probe_timer_finish_native(void) {assert(!locked);return timer_finish_error;}
static unsigned native_probe_calls,native_callback_calls;
static bool early_probe_callback,native_pending,callback_recycle;
static void *callback_buffer;
static int esp32_mquickjs_wifi_twt_sdk_probe_admit_native(void) {return admission_error;}
static void (*probe_hook)(void *);
static uint32_t setup_identity=1;
static unsigned setup_callbacks;
static bool setup_valid=true,early_setup_callback,setup_callback_recycle;
static bool esp32_mquickjs_wifi_twt_setup_tx_capture_native(uintptr_t node,uint8_t dialog,uint8_t flow,uint32_t *out) {
    assert(!locked && node==17 && dialog==9 && flow==3 && *out==0);
    if(!setup_valid)return false;*out=setup_identity;return true;
}
static bool esp32_mquickjs_wifi_twt_setup_tx_matches_native(uint32_t identity,uint8_t dialog,uint8_t flow) {
    assert(!locked);return setup_valid && identity==setup_identity && dialog==9 && flow==3;
}
#define TWT_DEMAND 2
typedef struct {int setup_cmd;uint8_t btwt_id;uint16_t timeout_time_ms;} wifi_btwt_setup_config_t;
static bool broadcast_node_valid=true;
static esp_err_t broadcast_timer_error;
static unsigned broadcast_builds;
static esp_err_t broadcast_begin_error;
static uint32_t broadcast_request[32];
static uint32_t broadcast_held_mask;
static bool esp32_mquickjs_wifi_btwt_setup_held(unsigned slot){assert(!locked && slot<32);return (broadcast_held_mask&(1U<<slot))!=0;}
esp_err_t esp32_mquickjs_wifi_btwt_submit_bind_native(unsigned slot,uint32_t identity,const uint8_t parameter[17]) {
    assert(!locked && broadcast_request[slot]==identity && (parameter[10]>>3)==slot);return 0;
}

esp_err_t esp32_mquickjs_wifi_btwt_setup_begin_native(unsigned slot,uint32_t identity,uintptr_t node,const uint8_t parameter[17]) {
    assert(!locked && slot<32 && identity && node==17 && (parameter[10]>>3)==slot);
    if(broadcast_begin_error)return broadcast_begin_error;
    broadcast_request[slot]=identity;return 0;
}
void esp32_mquickjs_wifi_btwt_setup_submitted_native(unsigned slot,uint32_t identity,esp_err_t error) {
    assert(!locked && slot<32 && broadcast_request[slot]==identity && error==native_result);
}

static esp_err_t esp32_mquickjs_wifi_btwt_timer_available_native(unsigned slot) {assert(!locked && slot<32);return broadcast_timer_error;}
static bool esp32_mquickjs_wifi_twt_sdk_broadcast_node_matches_native(uintptr_t node){assert(!locked);return broadcast_node_valid && node==17;}
void esp32_mquickjs_wifi_btwt_setup_tx_complete_native(unsigned slot,uint32_t identity,uintptr_t node,uint8_t dialog,const uint8_t parameter[17],uint8_t status);
int __real_ieee80211_btwt_setup(void *node,const wifi_btwt_setup_config_t *config){assert(!locked && node==(void *)17 && config);++broadcast_builds;return native_result;}
#define portENTER_CRITICAL_SAFE(lock) do {assert(!locked);locked=1;} while(0)
#define portEXIT_CRITICAL_SAFE(lock) do {assert(locked==1);locked=0;} while(0)
static void *heap_caps_calloc(size_t n,size_t size,unsigned caps) {
    assert(!locked && caps==(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
    ++allocations;return fail_allocate || allocations==fail_allocation_at?NULL:calloc(n,size);
}
static void heap_caps_free(void *p) {assert(!locked);free(p);}
int __real_wifi_event_post(int id,void *data,size_t size) {(void)id;(void)data;(void)size;assert(0);return -1;}
#ifndef TEST_REAL_TEARDOWN_TX
esp_err_t esp32_mquickjs_wifi_twt_teardown_tx_output_native(void *node,void *buffer,bool tracked,uint32_t *identity) {
    (void)node;(void)buffer;(void)tracked;*identity=0;return 0;
}
void esp32_mquickjs_wifi_twt_teardown_tx_output_returned(void *buffer,uint32_t identity) {(void)buffer;(void)identity;}
void esp32_mquickjs_wifi_twt_teardown_tx_recycle(void *buffer,uint32_t identity,bool entering) {(void)buffer;(void)identity;(void)entering;}
#endif
#ifdef TEST_REAL_INFORMATION_SUBMIT
static int information_post_error;
static unsigned information_posts;
#endif
static esp_err_t esp_event_post(const char *base,int32_t id,const void *data,size_t size,unsigned ticks) {
#ifdef TEST_REAL_TEARDOWN_TX
    if(id==34){assert(!locked && base==WIFI_EVENT && ticks==0);return td_observe(data,size);}
#endif
#ifdef TEST_REAL_INFORMATION_SUBMIT
    if(id==31){assert(!locked && base==WIFI_EVENT && data && size==40 && ticks==0);++information_posts;return information_post_error;}
#endif
    assert(!locked && base==WIFI_EVENT && id==30 && data && size==8 && ticks==0);return 0;
}
