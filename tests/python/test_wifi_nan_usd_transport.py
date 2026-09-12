"""Deferred production USD transport: native completion and physical retirement.

The fixture compiles the actual transport include with controlled driver,
recycler and eloop boundaries during the Wi-Fi validation wave only.
"""
import re
import unittest
from test_idf_nan_control import BASE, NanNativeControl


def clean(path):
    return re.sub(r'^#(?:include[^\n]*|pragma once)\n', '', path.read_text(), flags=re.M)


class NanUsdTransport(unittest.TestCase):
    compile_run = NanNativeControl.compile_run

    def production(self):
        header = BASE / 'internal'
        return (PREFIX + clean(header / 'esp32_mquickjs_wifi_nan_tx.h') +
                clean(header / 'esp32_mquickjs_wifi_offchan_frame.h') +
                clean(header / 'esp32_mquickjs_wifi_nan_usd_sdk.h') + BOUNDARIES +
                clean(BASE / 'src/modules/wifi_nan/esp32_mquickjs_wifi_nan_usd_transport.inc') + HELPERS)

    def test_early_result_retained_until_physical_recycler_and_exact_release(self):
        self.compile_run(self.production() + r'''
int main(void){
 assert(!esp32qjs_nan_usd_transport_open());
 begin_message(11);early=true;assert(!send_frame(1,0xa5));
 esp32qjs_usd_transport_t*t=s_esp32qjs_usd_transport;t->message_scope=false;
 esp32_mquickjs_wifi_nan_message_tx_status_t status;
 assert(esp32_mquickjs_wifi_nan_usd_message_status(11,&status));
 assert(status.tx_done&&status.tx_succeeded&&!status.buffer_retired&&status.completed_us==1234);
 assert(esp32_mquickjs_wifi_nan_usd_message_release(11)==ESP_ERR_NOT_FINISHED);
 busy=false;assert(esp32qjs_nan_usd_transport_service(false)==ESP_ERR_NOT_FINISHED);
 physical.buffer_present=false;physical.recycling=true;
 assert(esp32qjs_nan_usd_transport_service(false)==ESP_ERR_NOT_FINISHED);
 physical.recycling=false;physical.recycled=true;
 assert(!esp32qjs_nan_usd_transport_service(false));
 assert(esp32_mquickjs_wifi_nan_usd_message_status(11,&status)&&status.buffer_retired);
 assert(esp32_mquickjs_wifi_nan_usd_message_release(12)==ESP_ERR_INVALID_STATE);
 assert(!esp32_mquickjs_wifi_nan_usd_message_release(11));
 assert(!esp32qjs_nan_usd_transport_free()&&allocations==frees);
}
''')

    def test_pending_copy_is_not_submitted_until_old_buffer_returns(self):
        self.compile_run(self.production() + r'''
int main(void){
 assert(!esp32qjs_nan_usd_transport_open());assert(!send_frame(1,0xa1));
 begin_message(22);assert(!send_frame(2,0xb2));
 esp32qjs_usd_transport_t*t=s_esp32qjs_usd_transport;t->message_scope=false;
 assert(submits==1&&t->pending&&t->pending->data[0]==0xb2&&!t->message.buffer_retired);
 assert(!esp32qjs_nan_usd_service_drain(3)&&t->pending&&submits==1);
 assert(esp32qjs_nan_usd_transport_service(false)==ESP_ERR_NOT_FINISHED);
 physical.buffer_present=false;physical.recycled=true;early=true;
 assert(!esp32qjs_nan_usd_transport_service(false)&&submits==2&&last_byte==0xb2&&!t->pending);
 assert(t->message.ticket==t->ticket&&t->message.tx_done);
 busy=false;physical.buffer_present=false;physical.recycled=true;
 assert(!esp32qjs_nan_usd_transport_service(false));
 assert(!esp32_mquickjs_wifi_nan_usd_message_release(22));
 assert(!esp32qjs_nan_usd_transport_free()&&allocations==frees);
}
''')

    def test_subscriber_list_rotates_only_between_native_operations(self):
        import importlib.util
        from pathlib import Path
        from wireless_vm_fixture import extract
        sdk = Path('/home/zach/esp/esp-idf/components/wpa_supplicant/src/common/nan_de.c')
        if not sdk.exists():
            self.skipTest('reviewed SDK unavailable')
        script = BASE.parents[1] / 'scripts/patch_idf_nan_usd.py'
        spec = importlib.util.spec_from_file_location('usd_patch', script)
        patch = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(patch)
        source = patch.patch_timer(sdk.read_text())
        helper = extract(source, 'esp32qjs_nan_usd_subscribe_channel')
        self.compile_run(r'''
#include <assert.h>
#include <stdbool.h>
#define NAN_DE_SUBSCRIBE 1
struct os_reltime {long sec,usec;};
struct nan_de {int listen_freq,ext_listen_freq,tx_wait_end_freq;};
struct nan_de_service {int type,*freq_list,multi_chan_idx,freq;struct os_reltime next_publish_chan;};
static bool os_reltime_initialized(const struct os_reltime*t){return t->sec||t->usec;}
static bool os_reltime_before(const struct os_reltime*a,const struct os_reltime*b){return a->sec<b->sec||(a->sec==b->sec&&a->usec<b->usec);}
static void os_reltime_add_ms(struct os_reltime*t,unsigned ms){t->sec+=ms/1000;t->usec+=(ms%1000)*1000;}
''' + helper + r'''
int main(void){
 int frequencies[]={2437,5180,0};struct nan_de de={0};
 struct nan_de_service service={.type=NAN_DE_SUBSCRIBE,.freq_list=frequencies};
 struct os_reltime now={.sec=1};esp32qjs_nan_usd_subscribe_channel(&de,&service,&now);assert(service.freq==2437);
 now.sec=2;de.listen_freq=2437;esp32qjs_nan_usd_subscribe_channel(&de,&service,&now);assert(service.freq==2437);
 de.listen_freq=0;de.tx_wait_end_freq=2437;esp32qjs_nan_usd_subscribe_channel(&de,&service,&now);assert(service.freq==2437);
 de.tx_wait_end_freq=0;esp32qjs_nan_usd_subscribe_channel(&de,&service,&now);assert(service.freq==5180);
 now.sec=3;esp32qjs_nan_usd_subscribe_channel(&de,&service,&now);assert(service.freq==2437);
}
''')

    def test_timer_allocation_fails_before_submit_and_owner_can_be_retired(self):
        self.compile_run(self.production() + r'''
int main(void){
 assert(!esp32qjs_nan_usd_transport_open());begin_message(33);timer_failure=true;
 assert(send_frame(1,7)==ESP_ERR_NO_MEM&&!submits&&!physical.held);
 s_esp32qjs_usd_transport->message_scope=false;
 assert(!esp32_mquickjs_wifi_nan_usd_message_release(33));
 assert(!esp32qjs_nan_usd_transport_free()&&allocations==frees);
}
''')


PREFIX = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define CONFIG_ESP_WIFI_NAN_SYNC_ENABLE 1
#define CONFIG_ESP_WIFI_NAN_USD_ENABLE 1
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERR_INVALID_ARG 2
#define ESP_ERR_NO_MEM 3
#define ESP_ERR_TIMEOUT 4
#define ESP_ERR_NOT_FINISHED 5
#define ESP_ERR_INVALID_RESPONSE 6
#define ESP32_MQUICKJS_MEMORY_DEFAULT 0
#define ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL 1
#define ESP_WIFI_MAX_FUP_SSI_LEN 2048
#define WIFI_IF_STA 0
#define WIFI_SECOND_CHAN_NONE 0
#define WIFI_OFFCHAN_TX_REQ 1
#define WIFI_OFFCHAN_TX_CANCEL 2
#define WIFI_ROC_CANCEL 2
#define WIFI_ROC_DONE 0
#define WIFI_ACTION_TX_DONE 1
#define WIFI_ACTION_TX_FAILED 2
#define WIFI_ACTION_TX_DURATION_COMPLETED 3
#define WIFI_ACTION_TX_OP_CANCELLED 4
typedef int esp_err_t,wifi_roc_done_status_t;
typedef struct {int unused;}wifi_nan_followup_params_t;
typedef struct {int unused;}wifi_nan_publish_cfg_t;
typedef struct {int unused;}wifi_nan_subscribe_cfg_t;
typedef struct {int unused;}wifi_nan_usd_config_t;
typedef struct {int ifx,type,sec_channel;uint8_t channel,op_id;
 int(*rx_cb)(uint8_t*,uint8_t*,size_t,uint8_t);uint32_t data_len;uint8_t data[];}wifi_action_tx_req_t;
typedef struct {int ifx,type,sec_channel;uint8_t channel,op_id;
 int(*rx_cb)(uint8_t*,uint8_t*,size_t,uint8_t);void(*done_cb)(uint32_t,uint8_t,wifi_roc_done_status_t);}wifi_roc_req_t;
typedef struct {int ifx,status;uintptr_t context;uint8_t channel,op_id;}wifi_event_action_tx_status_t;
'''

BOUNDARIES = r'''
struct nan_de{int unused;};static struct nan_de engine,*g_nan_de=&engine;
static void*s_nan_usd_data_lock=&engine;
static uint32_t s_esp32qjs_usd_identity=1;
static bool s_esp32qjs_usd_closing,busy,early,timer_failure;
static int s_esp32qjs_usd_error;
static uint64_t ticket;
static unsigned allocations,frees,submits,cancels;
static uint8_t last_byte;
static esp32qjs_wifi_offchan_tx_status_t physical;
#define NAN_USD_DATA_LOCK() ((void)0)
#define NAN_USD_DATA_UNLOCK() ((void)0)
bool current_task_is_wifi_task(void){return true;}
int esp_nan_de_rx_action(uint8_t*h,uint8_t*p,size_t n,uint8_t c){(void)h;(void)p;(void)n;(void)c;return 0;}
static void*esp32_mquickjs_memory_wireless_calloc(const char*tag,size_t n,size_t size,int cls,int budget){
 assert(!strcmp(tag,"wifi.nan")&&!cls&&budget==1);void*p=calloc(n,size);if(p)allocations++;return p;}
static void esp32_mquickjs_memory_payload_free(void*p){if(p){frees++;free(p);}}
static void forced_memzero(void*p,size_t n){memset(p,0,n);}
static void esp32qjs_nan_usd_record_error(int error){if(!s_esp32qjs_usd_error)s_esp32qjs_usd_error=error;}
static int64_t esp_timer_get_time(void){return 1234;}
static int esp_nan_chan_to_freq(int channel){return 2407+channel*5;}
static int eloop_register_timeout(unsigned s,unsigned us,void(*fn)(void*,void*),void*a,void*b){
 (void)s;(void)us;(void)fn;(void)a;(void)b;return timer_failure?-1:0;}
static void eloop_cancel_timeout(void(*fn)(void*,void*),void*a,void*b){(void)fn;(void)a;(void)b;}
static int eloop_register_timeout_blocking(int(*fn)(void*,void*),void*a,void*b){return fn(a,b);}
static int esp32qjs_wifi_chm_timer_service_native(void){return 0;}
static int esp32_mquickjs_wifi_action_sdk_quiescent(void){return busy?ESP_ERR_TIMEOUT:0;}
esp_err_t esp32qjs_wifi_offchan_tx_allocate_ticket(uint64_t*out){assert(!*out);*out=++ticket;return 0;}
esp_err_t esp32qjs_wifi_offchan_tx_reserve(uint64_t id,uint32_t context,uint8_t channel){
 assert(!physical.held&&context==(uint32_t)(uintptr_t)esp_nan_de_rx_action&&channel==6);
 physical=(esp32qjs_wifi_offchan_tx_status_t){.ticket=id,.held=true};return 0;}
esp_err_t esp32qjs_wifi_offchan_tx_status(uint64_t id,esp32qjs_wifi_offchan_tx_status_t*out){
 if(!physical.held||id!=physical.ticket)return ESP_ERR_INVALID_STATE;
 *out=physical;return 0;}
esp_err_t esp32qjs_wifi_offchan_tx_poll_native(uint64_t id){assert(physical.held&&id==physical.ticket);return 0;}
esp_err_t esp32qjs_wifi_offchan_tx_release(uint64_t id){
 assert(physical.held&&id==physical.ticket&&!physical.buffer_present&&!physical.recycling);physical.held=false;return 0;}
static int esp_wifi_action_tx_req(wifi_action_tx_req_t*request){
 if(request->type==WIFI_OFFCHAN_TX_CANCEL){cancels++;busy=false;return 0;}
 assert(!busy&&physical.held);submits++;last_byte=request->data[0];busy=physical.buffer_present=true;request->op_id=7;
 if(early){wifi_event_action_tx_status_t e={.ifx=0,.status=WIFI_ACTION_TX_DONE,
 .context=(uintptr_t)esp_nan_de_rx_action,.channel=6,.op_id=7};esp32qjs_nan_usd_tx_status_capture(&e);}return 0;}
static int esp_wifi_remain_on_channel(wifi_roc_req_t*request){(void)request;return ESP_ERR_INVALID_STATE;}
static void nan_de_listen_ended(struct nan_de*de,int frequency){assert(de==g_nan_de&&frequency);}
static void nan_de_tx_status(struct nan_de*de,int frequency,const void*peer){assert(de==g_nan_de&&frequency&&!peer);}
static void nan_de_tx_wait_ended(struct nan_de*de){assert(de==g_nan_de);}
void esp32_mquickjs_wifi_nan_usd_sdk_status(esp32_mquickjs_wifi_nan_usd_sdk_status_t*status);
'''

HELPERS = r'''
void esp32_mquickjs_wifi_nan_usd_sdk_status(esp32_mquickjs_wifi_nan_usd_sdk_status_t*status){
 *status=(esp32_mquickjs_wifi_nan_usd_sdk_status_t){.identity=1};esp32qjs_nan_usd_transport_snapshot(status);}
static void begin_message(uint32_t id){
 esp32qjs_usd_transport_t*t=s_esp32qjs_usd_transport;assert(!t->message.identity);
 t->message=(esp32_mquickjs_wifi_nan_message_tx_status_t){.identity=id,.entered=true,.buffer_retired=true};t->message_scope=true;}
static int send_frame(uint8_t service,uint8_t byte){
 wifi_action_tx_req_t*request=calloc(1,sizeof(*request)+1);assert(request);
 request->ifx=0;request->type=WIFI_OFFCHAN_TX_REQ;request->channel=6;request->rx_cb=esp_nan_de_rx_action;
 request->data_len=1;request->data[0]=byte;
 uint8_t previous=esp32qjs_nan_usd_tx_service_scope(service);
 int result=esp32qjs_nan_usd_transport_tx(request,2437);esp32qjs_nan_usd_tx_service_scope(previous);
 request->data[0]=0;free(request);return result;
}
'''
