"""Deferred production information operation/result ownership at SDK boundaries.

Not a replacement state machine. This compiles the production source when the
Wi-Fi stage runs; only allocator/lock/task/TX/timer/event functions are injected.
No claim about SDK queue ordering, full Future core, GC, or RF interoperability.
"""
import unittest
from test_wifi_driver_phy import COMPONENT
from test_wifi_rx_target import unit
from test_wireless_control_regression import compile_run


class WiFiTwtInformationOperation(unittest.TestCase):
    def test_exact_tx_completion_abandon_queue_failure_and_exhaustion(self):
        code = PRELUDE
        for name in ('information_timer', 'information'):
            code += unit(COMPONENT / f'internal/esp32_mquickjs_wifi_twt_{name}.h')
        code += BOUNDARIES
        code += unit(COMPONENT / 'src/modules/wifi_twt/esp32_mquickjs_wifi_twt_information.c')
        compile_run(self, code + MAIN)


PRELUDE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define CONFIG_SOC_WIFI_HE_SUPPORT 1
#define CONFIG_IDF_TARGET_ESP32C5 1
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
#define ESP_OK 0
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NOT_FINISHED 0x104
#define WIFI_EVENT_ITWT_SUSPEND 31
#define WIFI_EVENT ((const char *)19)
typedef int esp_err_t;
typedef void *TaskHandle_t;
typedef unsigned portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
static bool locked,fail_allocate;
static unsigned allocations,posts;
static esp_err_t post_error,tx_error,timer_error;
static TaskHandle_t task=(void *)31;
static uint32_t current_identity;
static bool inspect_post;
#define portENTER_CRITICAL_SAFE(lock) do {assert(!locked);locked=true;} while(0)
#define portEXIT_CRITICAL_SAFE(lock) do {assert(locked);locked=false;} while(0)
static TaskHandle_t xTaskGetCurrentTaskHandle(void){return task;}
static void *heap_caps_calloc(size_t n,size_t size,unsigned caps){
    assert(!locked && caps==3);++allocations;return fail_allocate?NULL:calloc(n,size);
}
typedef struct {esp_err_t status;uint8_t flow_id_bitmap;uint32_t actual_suspend_time_ms[8];} wifi_event_sta_itwt_suspend_t;
'''
BOUNDARIES = r'''
static bool resume_allowed=true;
bool esp32_mquickjs_wifi_twt_sdk_information_resume_allowed_native(const esp32_mquickjs_wifi_twt_information_identity_t *native){
    assert(!locked && native->node==17);return resume_allowed;
}

static void esp32_mquickjs_wifi_twt_tx_cleanup_native(void){assert(!locked);}
static esp_err_t esp32_mquickjs_wifi_twt_tx_information_quiescent_native(uint32_t id){
    assert(!locked);(void)id;return tx_error;
}
void esp32_mquickjs_wifi_twt_information_timer_snapshot(esp32_mquickjs_wifi_twt_information_timer_snapshot_t *out){
    assert(!locked);*out=(esp32_mquickjs_wifi_twt_information_timer_snapshot_t){.fault=timer_error};
}
static esp_err_t esp_event_post(const char *base,int32_t id,const void *data,size_t size,unsigned ticks){
    assert(!locked && base==WIFI_EVENT && id==31 && size==40 && !ticks);++posts;
    const wifi_event_sta_itwt_suspend_t *event=data;const uint8_t *bytes=data;
    assert(!bytes[5] && !bytes[6] && !bytes[7]);
    for(unsigned i=0;i<8;++i)if(!(event->flow_id_bitmap&(1U<<i)))assert(!event->actual_suspend_time_ms[i]);
    if(inspect_post){
        esp32_mquickjs_wifi_twt_information_result_t result;
        assert(esp32_mquickjs_wifi_twt_information_read(current_identity,&result) && result.complete);
        esp32_mquickjs_wifi_twt_information_abandon(current_identity);
        assert(esp32_mquickjs_wifi_twt_information_reap_native(0)==ESP_ERR_NOT_FINISHED);
    }
    return post_error;
}
'''
MAIN = r'''
int main(void){
    esp32_mquickjs_wifi_twt_information_identity_t native={.node=17,.flows=8,.control=0x63};
    native.request_ids[3]=123;
    esp32_mquickjs_wifi_twt_information_result_t result;
    uint32_t id=0,other=0;
    fail_allocate=true;
    assert(esp32_mquickjs_wifi_twt_information_begin_native(10,&native,120,false,&id)==ESP_ERR_NO_MEM && !id);
    fail_allocate=false;
    assert(!esp32_mquickjs_wifi_twt_information_begin_native(10,&native,120,false,&id) && id==1);
    assert(esp32_mquickjs_wifi_twt_information_begin_native(11,&native,120,false,&other)==ESP_ERR_INVALID_STATE && !other);
    assert(esp32_mquickjs_wifi_twt_information_producer_allowed((void *)17,3,3,0,120));
    assert(!esp32_mquickjs_wifi_twt_information_producer_allowed((void *)17,3,3,1,120));
    task=(void *)99;assert(!esp32_mquickjs_wifi_twt_information_producer_allowed((void *)17,3,3,0,120));task=(void *)31;
    esp32_mquickjs_wifi_twt_information_bind_native(&native,42);
    assert(!esp32_mquickjs_wifi_twt_information_producer_allowed((void *)17,3,3,0,120));
    assert(!esp32_mquickjs_wifi_twt_information_callback_begin(41));
    assert(esp32_mquickjs_wifi_twt_information_callback_begin(42));
    wifi_event_sta_itwt_suspend_t event;memset(&event,0xa5,sizeof(event));event.status=0;event.flow_id_bitmap=8;
    assert(!esp32_mquickjs_wifi_twt_information_post(&event,sizeof(event)) && !posts);
    assert(esp32_mquickjs_wifi_twt_information_read(id,&result) && !result.complete);
    current_identity=id;inspect_post=true;post_error=0x107;
    esp32_mquickjs_wifi_twt_information_callback_end(42,true);
    assert(posts==1 && esp32_mquickjs_wifi_twt_information_read(id,&result) && result.complete &&
        !result.native_error && result.observation_error==post_error && result.submitting);
    assert(esp32_mquickjs_wifi_twt_information_reap_native(0)==ESP_ERR_NOT_FINISHED);
    esp32_mquickjs_wifi_twt_information_submitted_native(id,0);
    tx_error=ESP_ERR_NOT_FINISHED;assert(esp32_mquickjs_wifi_twt_information_reap_native(0)==tx_error);
    assert(esp32_mquickjs_wifi_twt_information_read(id,&result) && result.cleanup_error==tx_error);
    tx_error=0;assert(!esp32_mquickjs_wifi_twt_information_reap_native(0));
    assert(!esp32_mquickjs_wifi_twt_information_read(id,&result) && !esp32_mquickjs_wifi_twt_information_pending());
    inspect_post=false;post_error=0;
    assert(!esp32_mquickjs_wifi_twt_information_begin_native(11,&native,120,false,&other) && other>id);
    esp32_mquickjs_wifi_twt_information_abandon(id); /* Old identity cannot abandon the new operation. */
    assert(esp32_mquickjs_wifi_twt_information_read(other,&result) && !result.abandoned);
    assert(!esp32_mquickjs_wifi_twt_information_callback_begin(42));
    event.status=ESP_ERR_NO_MEM;event.flow_id_bitmap=0;
    assert(!esp32_mquickjs_wifi_twt_information_post(&event,sizeof(event)) && posts==1);
    esp32_mquickjs_wifi_twt_information_submitted_native(other,ESP_ERR_NO_MEM);
    assert(posts==2 && esp32_mquickjs_wifi_twt_information_read(other,&result) && result.complete);
    assert(!esp32_mquickjs_wifi_twt_information_reap_native(10)); /* Another Agreement is unaffected. */
    assert(esp32_mquickjs_wifi_twt_information_read(other,&result));
    assert(!esp32_mquickjs_wifi_twt_information_reap_native(11));
    assert(!esp32_mquickjs_wifi_twt_information_begin_native(12,&native,120,false,&id));
    esp32_mquickjs_wifi_twt_information_bind_native(&native,43);
    esp32_mquickjs_wifi_twt_information_submitted_native(id,0);
    assert(esp32_mquickjs_wifi_twt_information_callback_begin(43));
    event.status=0;event.flow_id_bitmap=8;
    assert(!esp32_mquickjs_wifi_twt_information_post(&event,sizeof(event)));
    timer_error=ESP_ERR_NO_MEM;esp32_mquickjs_wifi_twt_information_callback_end(43,true);
    assert(esp32_mquickjs_wifi_twt_information_read(id,&result) && result.native_error==ESP_ERR_NO_MEM);
    assert(!esp32_mquickjs_wifi_twt_information_reap_native(12));
    /* Resume waits for its replacement timer, not merely successful TX. */
    timer_error=0;
    assert(!esp32_mquickjs_wifi_twt_information_begin_native(13,&native,0,true,&id));
    assert(esp32_mquickjs_wifi_twt_information_producer_allowed((void *)17,3,3,0,0));
    assert(!esp32_mquickjs_wifi_twt_information_producer_allowed((void *)17,3,0,0,0));
    esp32_mquickjs_wifi_twt_information_bind_native(&native,44);
    esp32_mquickjs_wifi_twt_information_submitted_native(id,0);
    assert(esp32_mquickjs_wifi_twt_information_callback_begin(44));
    esp32_mquickjs_wifi_twt_information_timer_bound_native(&native,100);
    assert(!esp32_mquickjs_wifi_twt_information_post(&event,sizeof(event)));
    esp32_mquickjs_wifi_twt_information_callback_end(44,true);
    assert(esp32_mquickjs_wifi_twt_information_read(id,&result) && result.tx_complete && !result.complete);
    esp32_mquickjs_wifi_twt_information_timer_finished_native(&native,99,0);
    assert(esp32_mquickjs_wifi_twt_information_read(id,&result) && !result.complete);
    task=(void *)99;esp32_mquickjs_wifi_twt_information_timer_finished_native(&native,100,0);task=(void *)31;
    assert(esp32_mquickjs_wifi_twt_information_read(id,&result) && !result.complete);
    esp32_mquickjs_wifi_twt_information_timer_finished_native(&native,100,0);
    assert(esp32_mquickjs_wifi_twt_information_read(id,&result) && result.complete && result.resume_complete);
    assert(!esp32_mquickjs_wifi_twt_information_reap_native(13));
    assert(!esp32_mquickjs_wifi_twt_information_begin_native(14,&native,0,true,&other));
    esp32_mquickjs_wifi_twt_information_bind_native(&native,45);
    esp32_mquickjs_wifi_twt_information_submitted_native(other,0);
    esp32_mquickjs_wifi_twt_information_timer_finished_native(&native,100,0); /* Old numeric timer. */
    assert(esp32_mquickjs_wifi_twt_information_read(other,&result) && !result.complete);
    resume_allowed=false;esp32_mquickjs_wifi_twt_information_refresh_native();
    assert(esp32_mquickjs_wifi_twt_information_read(other,&result) && result.complete && result.native_error==ESP_ERR_INVALID_STATE);
    assert(!esp32_mquickjs_wifi_twt_information_reap_native(14));resume_allowed=true;
    /* A successful native observation must not erase an earlier timer error. */
    assert(!esp32_mquickjs_wifi_twt_information_begin_native(15,&native,0,true,&id));
    esp32_mquickjs_wifi_twt_information_bind_native(&native,46);
    esp32_mquickjs_wifi_twt_information_submitted_native(id,0);
    assert(esp32_mquickjs_wifi_twt_information_callback_begin(46));
    esp32_mquickjs_wifi_twt_information_timer_bound_native(&native,101);
    esp32_mquickjs_wifi_twt_information_timer_finished_native(&native,101,ESP_ERR_NO_MEM);
    assert(!esp32_mquickjs_wifi_twt_information_post(&event,sizeof(event)));
    esp32_mquickjs_wifi_twt_information_callback_end(46,true);
    assert(esp32_mquickjs_wifi_twt_information_read(id,&result) && result.complete && result.native_error==ESP_ERR_NO_MEM);
    assert(!esp32_mquickjs_wifi_twt_information_reap_native(15));
    unsigned before=allocations;
    assert(esp32_mquickjs_wifi_twt_information_begin_native(13,&native,4294968U,false,&id)==ESP_ERR_INVALID_ARG);
    s_information.last_identity=UINT32_MAX;
    assert(esp32_mquickjs_wifi_twt_information_begin_native(13,&native,120,false,&id)==ESP_ERR_NO_MEM);
    assert(allocations==before && !locked);free(s_information.operation);return 0;
}
'''
