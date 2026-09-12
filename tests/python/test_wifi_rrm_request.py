"""Deferred native RRM/Radio lifecycle; no fixture execution in the API wave.

Compile production request/registry/Radio bodies; inject only SDK callback
storage, dispatch, worker queue, allocation, clock and locks. The SDK source
itself has separate original/patched coverage in test_idf_rrm.py.
"""
import re
import unittest
from test_wifi_vendor_ie import vendor_code, COMPONENT
from test_wifi_config_controls import structure
from test_wifi_rx_target import unit
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import CORE, extract


class WiFiRRMRequest(unittest.TestCase):
    def test_production_radio_identity_callback_storage_and_retirement(self):
        internal = COMPONENT / 'internal'
        header = (internal / 'esp32_mquickjs_wifi_radio.h').read_text()
        radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
        code = '#include <stdint.h>\n#include <stdlib.h>\n#define CONFIG_ESP_WIFI_RRM_SUPPORT 1\n#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1\n'
        code += '#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT 1\n'
        code += re.search(r'typedef enum \{[^}]*\} esp32_mquickjs_wifi_radio_operation_kind_t;', header).group(0)
        code += structure(header, 'esp32_mquickjs_wifi_radio_operation_t')
        injected = vendor_code('esp32c5/representative')
        injected = injected.replace('struct {unsigned identity,lease_identity;} operation;',
                                    'esp32_mquickjs_wifi_radio_operation_t operation; uint32_t next_operation_identity;')
        code += injected
        code += unit(internal / 'esp32_mquickjs_wifi_rrm_sdk.h')
        code += unit(internal / 'esp32_mquickjs_wifi_rrm_radio.h')
        code += unit(internal / 'esp32_mquickjs_wifi_rrm_request.h')
        code += BOUNDARIES
        code += extract((CORE / 'esp32_mquickjs_wireless_core.c').read_text(), 'esp32_mquickjs_wireless_secure_zero')
        for name in ['wifi_radio_connection_owner_locked', 'wifi_radio_rrm_exact_locked',
                     'esp32_mquickjs_wifi_radio_rrm_reserve', 'esp32_mquickjs_wifi_radio_rrm_command',
                     'esp32_mquickjs_wifi_radio_rrm_retire', 'esp32_mquickjs_wifi_radio_end_operation']:
            code += extract(radio, name)
        code += RESERVE_BRIDGE + COPY_BOUNDARY
        code += unit(COMPONENT / 'src/modules/wifi_roaming/esp32_mquickjs_wifi_rrm_request.c')
        compile_run(self, code + MAIN)


BOUNDARIES = r'''
#define ESP_FAIL -9
#define ESP_ERR_WIFI_NOT_STARTED -10
#define ESP_ERR_TIMEOUT -11
#define ESP_ERR_INVALID_RESPONSE -12
#define ESP_ERR_INVALID_SIZE -13
#define MALLOC_CAP_8BIT 1
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
static unsigned request_lock;
#define portENTER_CRITICAL(p) do{(void)(p);assert(!request_lock);request_lock=1;}while(0)
#define portEXIT_CRITICAL(p) do{(void)(p);assert(request_lock);request_lock=0;}while(0)
static int64_t now;
static int64_t esp_timer_get_time(void) {assert(!request_lock);return now;}
static unsigned alloc_calls, fail_alloc, live_allocs;
static void *heap_caps_calloc(size_t n,size_t bytes,unsigned flags) {
    (void)flags;assert(!request_lock);if(++alloc_calls==fail_alloc)return NULL;
    void *p=calloc(n,bytes);assert(p);live_allocs++;return p;
}
static void *heap_caps_malloc(size_t bytes,unsigned flags) {return heap_caps_calloc(1,bytes,flags);}
static void heap_caps_free(void *p) {assert(!request_lock && live_allocs);live_allocs--;free(p);}
static bool queue_full;
static void (*queued)(void *);
static void *queued_arg;
static bool esp32_mquickjs_submit_background_worker(void (*fn)(void *),void *arg) {
    assert(!request_lock && !queued);if(queue_full)return false;queued=fn;queued_arg=arg;return true;
}
static void run_worker(void) {assert(queued);void(*fn)(void*)=queued;queued=NULL;now+=200000;fn(queued_arg);}
static esp32_mquickjs_wifi_radio_lease_t application,station,access_point;
static bool rm_enabled=true;
static int config_error;
static esp_err_t esp_wifi_get_config(wifi_interface_t interface,wifi_config_t *config) {
    assert(locks && !critical && !request_lock && interface==WIFI_IF_STA);
    memset(config,0xa5,sizeof(*config));config->sta.rm_enabled=rm_enabled;return config_error;
}
static esp32_mquickjs_wifi_rrm_callback_t pending_callback;
static void *pending_identity;
static unsigned submissions, cancellations, probes;
static bool dispatch_fail, early_report;
static int submit_code,cancel_code;
static uint8_t payload[32]={1,52,13,2,0,0,0,0,1};
static unsigned observation_attempts,observations;
static bool observation_queue_full;
void esp32qjs_rrm_publish_observation(const uint8_t *report,size_t length) {
    assert(!request_lock && !locks && !critical && ((report!=NULL)==(length!=0)));
    observation_attempts++;if(!observation_queue_full)observations++;
}
esp_err_t esp32_mquickjs_wifi_rrm_sdk_command(esp32_mquickjs_wifi_rrm_command_t command,
    esp32_mquickjs_wifi_rrm_callback_t callback,void *id,esp32_mquickjs_wifi_rrm_sdk_result_t *r) {
    assert(locks && !critical && !request_lock && callback && id);
    *r=(esp32_mquickjs_wifi_rrm_sdk_result_t){.ownership=-1};
    if(dispatch_fail)return ESP_FAIL;
    r->entered=true;
    if(command==ESP32_MQUICKJS_WIFI_RRM_SUBMIT) {
        assert(!pending_callback);submissions++;r->tx_attempted=true;r->code=submit_code;
        if(!submit_code){pending_callback=callback;pending_identity=id;}
        if(early_report && pending_callback){callback(id,payload,16);pending_callback=NULL;pending_identity=NULL;}
    } else if(command==ESP32_MQUICKJS_WIFI_RRM_CANCEL) {
        cancellations++;r->code=cancel_code;
        if(!cancel_code && pending_callback==callback && pending_identity==id){pending_callback=NULL;pending_identity=NULL;}
    } else probes++;
    r->ownership=!pending_callback ? 0 : pending_callback==callback && pending_identity==id ? 1 : -16;
    if(command==ESP32_MQUICKJS_WIFI_RRM_QUERY)r->code=r->ownership;
    return ESP_OK;
}
static void deliver(const uint8_t *report,size_t bytes) {
    assert(pending_callback && !locks && !request_lock);
    pending_callback(pending_identity,report,bytes);
    pending_callback=NULL;pending_identity=NULL;
}
'''
RESERVE_BRIDGE = r'''
esp_err_t esp32_mquickjs_wifi_rrm_reserve(esp32_mquickjs_wifi_radio_operation_t *token) {
    return esp32_mquickjs_wifi_radio_rrm_reserve(&application,&station,&access_point,token);
}
'''
COPY_BOUNDARY = r'''
static bool close_during_copy;
static esp32_mquickjs_wifi_rrm_request_t *closing;
static void *interleaved_copy(void *destination,const void *source,size_t length) {
    if(close_during_copy && source==payload) {
        close_during_copy=false;
        esp32_mquickjs_wifi_rrm_counts_t counts;esp32_mquickjs_wifi_rrm_counts(&counts);
        assert(counts.callback_busy && !request_lock);
        esp32_mquickjs_wifi_rrm_close(closing);
        esp32_mquickjs_wifi_rrm_status_t status;assert(esp32_mquickjs_wifi_rrm_status(closing,&status));
        assert(status.closed && status.retained_bytes==16);
    }
    return memcpy(destination,source,length);
}
#define memcpy interleaved_copy
'''
MAIN = r'''
static esp32_mquickjs_wifi_rrm_status_t state(esp32_mquickjs_wifi_rrm_request_t *r) {
    esp32_mquickjs_wifi_rrm_status_t s;assert(esp32_mquickjs_wifi_rrm_status(r,&s));return s;
}
static void progress(void) {now+=200000;assert(esp32_mquickjs_wifi_rrm_service());run_worker();esp32_mquickjs_wifi_rrm_poll_observations(false);}
static esp32_mquickjs_wifi_rrm_request_t *open_request(unsigned bytes) {
    esp32_mquickjs_wifi_rrm_request_t *r=NULL;assert(esp32_mquickjs_wifi_rrm_create(bytes,&r)==ESP_OK);
    assert(esp32_mquickjs_wifi_rrm_start(r)==ESP_OK);progress();return r;
}
int main(void) {
    reset_vendor();s_radio.effective_mode=WIFI_MODE_STA;s_radio.next_operation_identity=1;
    wifi_radio_operation_lock();
    assert(wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION,WIFI_MODE_STA,&application)==ESP_OK);
    assert(wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA,WIFI_MODE_STA,&station)==ESP_OK);
    wifi_radio_operation_unlock();
    for(unsigned n=1;n<=2;n++) {
        alloc_calls=0;fail_alloc=n;esp32_mquickjs_wifi_rrm_request_t *r=NULL;
        assert(esp32_mquickjs_wifi_rrm_create(16,&r)==ESP_ERR_NO_MEM && !r && !live_allocs);
        assert(!s_rrm_handles && !s_rrm_reserved_bytes);
    }
    fail_alloc=0;
    esp32_mquickjs_wifi_rrm_request_t *held[8]={0},*extra=NULL;
    for(unsigned i=0;i<8;i++)assert(esp32_mquickjs_wifi_rrm_create(16,&held[i])==ESP_OK);
    assert(esp32_mquickjs_wifi_rrm_create(16,&extra)==ESP_ERR_NO_MEM);
    for(unsigned i=0;i<8;i++)esp32_mquickjs_wifi_rrm_release(held[i]);
    memset(held,0,sizeof(held));
    for(unsigned i=0;i<4;i++)assert(esp32_mquickjs_wifi_rrm_create(4096,&held[i])==ESP_OK);
    assert(esp32_mquickjs_wifi_rrm_create(1,&extra)==ESP_ERR_NO_MEM);
    for(unsigned i=0;i<4;i++)esp32_mquickjs_wifi_rrm_release(held[i]);
    assert(!s_rrm_reserved_bytes && !live_allocs && !submissions);

    esp32_mquickjs_wifi_rrm_request_t *r=open_request(16);
    assert(state(r).terminal==ESP32_MQUICKJS_WIFI_RRM_PENDING && !state(r).retired);
    esp32_mquickjs_wifi_radio_operation_t old=state(r).operation;
    esp32_mquickjs_wifi_radio_end_operation(&old);assert(old.identity && s_radio.operation.identity==old.identity);
    wifi_radio_operation_lock();wifi_radio_release_locked(&station);wifi_radio_operation_unlock();assert(station.acquired);
    esp32_mquickjs_wifi_rrm_callback_t stale_callback=pending_callback;void *stale_identity=pending_identity;
    esp32_mquickjs_wifi_rrm_cancel(r,true);deliver(payload,16);
    assert(state(r).terminal==ESP32_MQUICKJS_WIFI_RRM_TIMED_OUT && !state(r).retired);
    progress();assert(state(r).retired && !s_radio.operation.identity && submissions==1);
    esp32_mquickjs_wifi_rrm_release(r);assert(!live_allocs);

    r=open_request(16);assert(state(r).operation.identity!=old.identity);
    stale_callback(stale_identity,payload,16);assert(!state(r).callback_seen);
    esp32_mquickjs_wifi_rrm_sdk_result_t native;
    assert(esp32_mquickjs_wifi_radio_rrm_retire(&old,stale_callback,&native)==ESP_ERR_INVALID_STATE && !native.entered);
    deliver(payload,16);assert(state(r).terminal==ESP32_MQUICKJS_WIFI_RRM_REPORT && !state(r).retired);
    uint8_t copy[16]={0};assert(!esp32_mquickjs_wifi_rrm_copy(r,0,copy,16));progress();
    assert(state(r).retired && esp32_mquickjs_wifi_rrm_copy(r,0,copy,16) && !memcmp(copy,payload,16));
    assert(!esp32_mquickjs_wifi_rrm_copy(r,SIZE_MAX,copy,1));
    assert(!esp32_mquickjs_wifi_rrm_copy(r,15,copy,2));
    esp32_mquickjs_wifi_rrm_close(r);assert(!esp32_mquickjs_wifi_rrm_copy(r,0,copy,1));
    esp32_mquickjs_wifi_rrm_release(r);assert(!live_allocs);

    early_report=true;r=open_request(16);assert(state(r).retired && state(r).callback_seen);
    early_report=false;esp32_mquickjs_wifi_rrm_release(r);
    r=open_request(8);deliver(payload,16);progress();assert(state(r).error==ESP_ERR_INVALID_SIZE && state(r).received_bytes==16);
    assert(!esp32_mquickjs_wifi_rrm_copy(r,0,copy,1));esp32_mquickjs_wifi_rrm_release(r);
    r=open_request(16);deliver(NULL,0);progress();assert(state(r).terminal==ESP32_MQUICKJS_WIFI_RRM_NO_REPORT);
    esp32_mquickjs_wifi_rrm_release(r);
    r=open_request(16);assert(esp32_mquickjs_wifi_rrm_waiter_add(r));
    unsigned observed=observation_attempts;deliver(payload,16);progress();
    assert(state(r).retired && observation_attempts==observed && !esp32_mquickjs_wifi_rrm_poll_observations(false));
    esp32_mquickjs_wifi_rrm_waiter_remove(r);observation_queue_full=true;
    assert(esp32_mquickjs_wifi_rrm_poll_observations(false) && observation_attempts==observed+1);
    assert(!esp32_mquickjs_wifi_rrm_poll_observations(false));observation_queue_full=false;
    esp32_mquickjs_wifi_rrm_release(r);assert(!live_allocs);
    r=open_request(16);closing=r;close_during_copy=true;deliver(payload,16);
    assert(!close_during_copy && state(r).terminal==ESP32_MQUICKJS_WIFI_RRM_CANCELLED && !state(r).retired);
    progress();assert(state(r).retired && !state(r).retained_bytes);esp32_mquickjs_wifi_rrm_release(r);

    r=open_request(16);unsigned sent=submissions;
    dispatch_fail=true;esp32_mquickjs_wifi_rrm_close(r);progress();
    assert(!state(r).retired && state(r).cleanup_error==ESP_FAIL && s_radio.operation.identity);
    esp32_mquickjs_wifi_rrm_release(r);assert(s_rrm_handles==1);
    dispatch_fail=false;assert(!esp32_mquickjs_wifi_rrm_prepare_runtime_destroy());run_worker();
    assert(esp32_mquickjs_wifi_rrm_prepare_runtime_destroy() && !live_allocs && submissions==sent);

    r=NULL;assert(esp32_mquickjs_wifi_rrm_create(16,&r)==ESP_OK);assert(esp32_mquickjs_wifi_rrm_start(r)==ESP_OK);
    queue_full=true;assert(!esp32_mquickjs_wifi_rrm_service());assert(state(r).cleanup_error==ESP_ERR_NO_MEM);
    esp32_mquickjs_wifi_rrm_close(r);esp32_mquickjs_wifi_rrm_release(r);
    queue_full=false;progress();assert(!live_allocs && submissions==sent);

    rm_enabled=false;r=open_request(16);assert(state(r).retired && state(r).error==ESP_ERR_INVALID_STATE && submissions==sent);
    esp32_mquickjs_wifi_rrm_release(r);rm_enabled=true;
    submit_code=-12;r=open_request(16);assert(state(r).retired && state(r).submit.tx_attempted && state(r).submit.code==-12);
    esp32_mquickjs_wifi_rrm_release(r);submit_code=0;
    s_radio.next_operation_identity=UINT32_MAX;r=open_request(16);assert(state(r).operation.identity==UINT32_MAX);
    esp32_mquickjs_wifi_rrm_close(r);progress();esp32_mquickjs_wifi_rrm_release(r);sent=submissions;
    r=open_request(16);assert(state(r).retired && state(r).error==ESP_ERR_NO_MEM && submissions==sent);
    esp32_mquickjs_wifi_rrm_release(r);
    assert(!s_rrm_active && !s_rrm_handles && !s_rrm_reserved_bytes && !live_allocs && !s_radio.operation.identity);
    return 0;
}
'''
