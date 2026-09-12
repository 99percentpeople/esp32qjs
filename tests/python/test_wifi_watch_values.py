"""Deferred production watch capture/conversion with queue/lock/SDK boundaries."""
import tempfile
import unittest
from wireless_vm_fixture import ROOT, build, extract, run
from test_wifi_config_controls import PRELUDE, sdk_types
from test_wifi_rx_target import unit

WATCH = ROOT / 'components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi_watch.c'

BOUNDARIES = r'''
#include <stdatomic.h>
#include <math.h>
#include "cutils.h"
#define ESP32_MQUICKJS_WIFI_MAX_WATCHERS 4
#define WIFI_WATCH_MAX_SEQUENCE UINT64_C(9007199254740991)
#define pdTRUE 1
static void *s_watch_mutex=(void *)1,*s_ingress=(void *)2;
static uint64_t s_sequence;
static uint32_t s_ingress_high_water;
static _Atomic uint32_t s_ingress_dropped;
static wifi_watch_event_t queued_event;
static int locked,sends,wakes;static bool lock_fail,queue_fail;
typedef void esp32_mquickjs_runtime_t;
typedef void esp32_mquickjs_event_queue_t;
static struct {void *runtime,*queue;uint64_t after_sequence,mask;bool all,raw;} source={.runtime=(void *)3,.queue=(void *)4,.all=true};
typedef __typeof__(source) wifi_watch_source_t;
static wifi_watch_source_t *s_sources[4]={&source};
#define portMAX_DELAY UINT32_MAX
#define portMUX_TYPE int
#define portMUX_INITIALIZER_UNLOCKED 0
static int critical;
#define portENTER_CRITICAL(p) do{(void)(p);assert(!critical);critical=1;}while(0)
#define portEXIT_CRITICAL(p) do{(void)(p);assert(critical);critical=0;}while(0)
static bool queued_ready,subscriber_fail;
static unsigned uxQueueMessagesWaiting(void *queue){assert(queue==s_ingress && locked);return queued_ready?1:0;}
static wifi_watch_event_t subscriber_events[4];
static int subscriber_sends;
static int xQueuePeek(void *q,void *e,unsigned t){assert(q==s_ingress && !t && locked);if(!queued_ready)return 0;*(wifi_watch_event_t *)e=queued_event;return 1;}
static int xQueueReceive(void *q,void *e,unsigned t){int ok=xQueuePeek(q,e,t);if(ok)queued_ready=false;return ok;}
#define esp32_mquickjs_memory_queue_delete vQueueDelete
static void vQueueDelete(void *q){assert(q==s_ingress && locked && !queued_ready);}
static bool esp32_mquickjs_wifi_watch_control_ready(int id,uint32_t generation){(void)id;(void)generation;return true;}
static bool esp32_mquickjs_event_queue_send(void *q,const void *e){assert(q==source.queue && locked);subscriber_sends++;if(subscriber_fail)return false;assert(subscriber_sends<=4);subscriber_events[subscriber_sends-1]=*(const wifi_watch_event_t *)e;return true;}

static int xSemaphoreTake(void *mutex,unsigned ticks) {assert(mutex==s_watch_mutex && (ticks==0 || ticks==portMAX_DELAY) && !locked);if(lock_fail)return 0;locked=1;return 1;}
static void xSemaphoreGive(void *mutex) {assert(mutex==s_watch_mutex && locked);locked=0;}
static uint64_t esp_timer_get_time(void) {return 456;}
static int xQueueSend(void *queue,const void *event,unsigned ticks) {
    assert(queue==s_ingress && !ticks && locked);sends++;
    if(queue_fail)return 0;queued_event=*(const wifi_watch_event_t *)event;queued_ready=true;return 1;
}
static void esp32_mquickjs_notify_activity(void *runtime) {assert(runtime==source.runtime && locked);wakes++;}
'''

MAIN = r'''
static uint32_t number(JSContext *ctx,JSValue object,const char *key) {
    uint32_t value;assert(!JS_ToUint32(ctx,&value,JS_GetPropertyStr(ctx,object,key)));return value;
}
int main(void) {
    for(int scenario=0;scenario<12;scenario++) {
        int total=1;
        for(int nth=0;nth<=total;nth++) {
            union {wifi_event_ftm_report_t ftm;wifi_event_action_tx_status_t action;
                wifi_event_roc_done_t roc;wifi_event_ap_wrong_password_t wrong;
                wifi_event_sta_beacon_offset_unstable_t beacon;wifi_event_sta_connected_t connected;} native={0};
            int id=WIFI_EVENT_FTM_REPORT;const void *input=&native;
            switch(scenario) {
            case 0:case 1:
                native.ftm.peer_mac[0]=2;native.ftm.status=scenario?FTM_STATUS_FAIL:FTM_STATUS_SUCCESS;
                native.ftm.rtt_raw=UINT32_MAX;native.ftm.rtt_est=0x80000000U;
                native.ftm.dist_est=123;native.ftm.ftm_report_num_entries=255;break;
            case 2:id=WIFI_EVENT_ACTION_TX_STATUS;native.action=(wifi_event_action_tx_status_t){
                .ifx=WIFI_IF_AP,.context=0xdeadbeef,.status=WIFI_ACTION_TX_DURATION_COMPLETED,.op_id=255,.channel=177};break;
            case 3:id=WIFI_EVENT_ROC_DONE;native.roc=(wifi_event_roc_done_t){.context=0xdeadbeef,.status=0,.op_id=254,.channel=36};break;
            case 4:id=WIFI_EVENT_AP_WRONG_PASSWORD;native.wrong.mac[0]=2;native.wrong.mac[5]=66;break;
            case 5:case 6:id=WIFI_EVENT_STA_BEACON_OFFSET_UNSTABLE;native.beacon.beacon_success_rate=scenario==5?87.5f:NAN;break;
            case 7:input=NULL;break;
            case 8:id=WIFI_EVENT_DPP_FAILED;input=(void *)1;break;
            case 9:id=WIFI_EVENT_STA_WPS_ER_SUCCESS;input=(void *)1;break;
            case 10:id=-777;input=(void *)1;break;
            case 11:id=WIFI_EVENT_STA_CONNECTED;native.connected.ssid_len=2;
                native.connected.ssid[0]=255;native.connected.ssid[1]=65;native.connected.bssid[0]=2;
                native.connected.channel=6;native.connected.aid=9;break;
            }
            sends=wakes=0;calls=0;inject=1;collect=0;fail_at=0;
            esp32_mquickjs_wifi_watch_capture(id,input,91);
            assert(!locked && sends==1 && wakes==1 && calls==0);
            assert(queued_event.control_generation==91 && queued_event.timestamp_us==456);
            if(scenario==1)assert(!queued_event.unsigned_values[1] && !queued_event.unsigned_values[2] && !queued_event.unsigned_values[3] && !queued_event.count);
            if(scenario==2 || scenario==3) {
                uint32_t cookie=0xdeadbeef;
                for(size_t i=0;i+sizeof(cookie)<=sizeof(queued_event);i++)assert(memcmp((char *)&queued_event+i,&cookie,sizeof(cookie)));
            }
            memset(&native,0,sizeof(native));queued_event.raw_requested=true;
            void *heap=malloc(128*1024);JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);
            test_ctx=ctx;JSGCRef result_ref,data_ref;JSValue *result=JS_PushGCRef(ctx,&result_ref);
            calls=0;fail_at=nth;collect=1;inject=1;
            *result=wifi_watch_to_js(ctx,&queued_event,NULL);
            if(nth==0){assert(!JS_IsException(*result));total=calls;}else assert(JS_IsException(*result));
            collect=0;inject=0;
            if(JS_IsException(*result)){assert(JS_HasException(ctx));JS_GetException(ctx);}
            else {
                JSValue *data=JS_PushGCRef(ctx,&data_ref);*data=JS_GetPropertyStr(ctx,*result,"data");
                assert(JS_IsUndefined(JS_GetPropertyStr(ctx,*result,"rawEventData")));
                assert(JS_IsString(ctx,JS_GetPropertyStr(ctx,*result,"rawEventDataUnavailableReason")));
                if(scenario>=6 && scenario<=10)assert(JS_IsNull(*data));
                else {
                    assert(JS_IsNull(JS_GetPropertyStr(ctx,*result,"dataUnavailableReason")));
                    assert(JS_IsUndefined(JS_GetPropertyStr(ctx,*data,"context")));
                    assert(JS_IsUndefined(JS_GetPropertyStr(ctx,*data,"password")));
                    if(scenario==0)assert(number(ctx,*data,"rttRawNs")==UINT32_MAX && number(ctx,*data,"rttEstimatedNs")==0x80000000U && number(ctx,*data,"distanceCm")==123 && number(ctx,*data,"reportEntries")==255);
                    if(scenario==1)assert(JS_IsNull(JS_GetPropertyStr(ctx,*data,"rttRawNs")) && JS_IsNull(JS_GetPropertyStr(ctx,*data,"reportEntries")));
                    if(scenario==2)assert(number(ctx,*data,"interfaceId")==WIFI_IF_AP && number(ctx,*data,"operationId")==255 && number(ctx,*data,"channel")==177);
                    if(scenario==3)assert(number(ctx,*data,"operationId")==254 && number(ctx,*data,"channel")==36);
                    if(scenario==4)assert(JS_IsString(ctx,JS_GetPropertyStr(ctx,*data,"address")));
                    if(scenario==5){double value;assert(!JS_ToNumber(ctx,&value,JS_GetPropertyStr(ctx,*data,"beaconSuccessRate")) && value==87.5);}
                    if(scenario==11)assert(JS_IsNull(JS_GetPropertyStr(ctx,*data,"ssid")) && JS_IsArray(ctx,JS_GetPropertyStr(ctx,*data,"ssidBytes")));
                }
                JS_PopGCRef(ctx,&data_ref);
            }
            JS_PopGCRef(ctx,&result_ref);assert(!root_count && !native_live);JS_FreeContext(ctx);free(heap);
        }
    }
    uint64_t before=s_sequence;uint32_t dropped=atomic_load(&s_ingress_dropped);
    sends=wakes=0;lock_fail=true;esp32_mquickjs_wifi_watch_capture(WIFI_EVENT_FTM_REPORT,NULL,0);
    assert(!sends && !wakes && s_sequence==before && atomic_load(&s_ingress_dropped)==dropped+1);
    lock_fail=false;queue_fail=true;esp32_mquickjs_wifi_watch_capture(WIFI_EVENT_FTM_REPORT,NULL,0);
    assert(sends==1 && !wakes && s_sequence==before+1 && atomic_load(&s_ingress_dropped)==dropped+2 && !locked);
    atomic_store(&s_ingress_dropped,UINT32_MAX);esp32_mquickjs_wifi_watch_capture(WIFI_EVENT_FTM_REPORT,NULL,0);
    assert(atomic_load(&s_ingress_dropped)==UINT32_MAX);
    queue_fail=false;s_sequence=WIFI_WATCH_MAX_SEQUENCE;before=sends;
    esp32_mquickjs_wifi_watch_capture(WIFI_EVENT_FTM_REPORT,NULL,0);assert(sends==before && !locked);
    return 0;
}
'''


def watch_code():
    source = WATCH.read_text()
    types = source[source.index('typedef enum {'):source.index('typedef struct {\n    esp32_mquickjs_runtime_t')]
    header = (ROOT / 'components/esp32_mquickjs/internal/esp32_mquickjs_wifi.h').read_text()
    limits = '\n'.join(line for line in header.splitlines() if line.startswith('#define ESP32_MQUICKJS_WIFI_WATCH_')) + '\n'
    code = PRELUDE + limits + sdk_types('esp32c5/representative', extra=(
        'wifi_event_t', 'wifi_event_sta_scan_done_t', 'wifi_event_sta_connected_t',
        'wifi_event_sta_disconnected_t', 'wifi_event_sta_authmode_change_t',
        'wifi_event_ap_staconnected_t', 'wifi_event_ap_stadisconnected_t',
        'wifi_event_ap_probe_req_rx_t', 'wifi_event_bss_rssi_low_t',
        'wifi_event_home_channel_change_t', 'wifi_event_ftm_report_t',
        'wifi_event_action_tx_status_t', 'wifi_event_roc_done_t',
        'wifi_event_ap_wrong_password_t', 'wifi_event_sta_beacon_offset_unstable_t',
        'wifi_event_sta_itwt_setup_t', 'wifi_event_sta_btwt_setup_t', 'wifi_event_sta_itwt_teardown_t',
        'wifi_event_sta_btwt_teardown_t', 'wifi_event_sta_itwt_probe_t', 'wifi_event_sta_itwt_suspend_t',
        'wifi_event_sta_twt_wakeup_t', 'wifi_event_neighbor_report_t'))
    code += '#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1\n'
    code += unit(ROOT / "components/esp32_mquickjs/internal/esp32_mquickjs_wifi_neighbor.h")
    code += unit(ROOT / "components/esp32_mquickjs/src/modules/wifi_common/esp32_mquickjs_wifi_neighbor.c")
    code += types + BOUNDARIES
    code += source[source.index('static portMUX_TYPE s_neighbor_mux'):source.index('static void wifi_watch_drop(void)')]
    config = WATCH.with_name('esp32_mquickjs_wifi_config.c').read_text()
    code += ''.join(extract(config, name) for name in (
        'esp32_mquickjs_wifi_ssid_text', 'esp32_mquickjs_wifi_set_ssid_properties'))
    code += ''.join(extract(source, name) for name in (
        'wifi_watch_drop', 'wifi_watch_descriptor', 'esp32_mquickjs_wifi_watch_capture', 'wifi_watch_poll',
        'wifi_watch_number', 'wifi_watch_twt_setup_properties', 'wifi_watch_twt_suspend_properties', 'wifi_watch_neighbor_properties', 'wifi_watch_value_to_js', 'wifi_watch_to_js', 'wifi_watch_closed'))
    return code


class WiFiWatchValues(unittest.TestCase):
    def test_native_copy_unsigned_fields_redaction_queue_pressure_and_vm_roots(self):
        with tempfile.TemporaryDirectory() as tmp:
            run([str(build(tmp, watch_code(), MAIN))])
