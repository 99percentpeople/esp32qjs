"""Deferred real Radio Action admission, SDK boundary, event/fence and lease retirement.

Reuses production registry fixture. SDK storage/calls, event-loop post scheduling
and driver-queue fence are injected, not substitutes for the production controller.
No runtime Future, actual SDK-task scheduling or RF proof is implied.
"""
import re
import unittest
from test_wifi_config_controls import sdk_types
from test_wifi_driver_phy import COMPONENT
from test_wifi_rx_target import unit
from test_wifi_vendor_ie import vendor_code
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import extract


def radio_code(profile, ap):
    radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
    header = (COMPONENT / 'internal/esp32_mquickjs_wifi_radio.h').read_text()
    code = vendor_code(profile, ap)
    code = code.replace('struct {unsigned identity,lease_identity;} operation;',
                        'esp32_mquickjs_wifi_radio_operation_t operation;uint32_t next_operation_identity;'
                        'unsigned event_identity,event_revision,event_phase;bool event_fence_posted,event_fence_seen;')
    types = re.search(r'typedef enum \{[^}]*\} esp32_mquickjs_wifi_radio_operation_kind_t;', header).group(0)
    types += re.search(r'typedef struct \{[^}]*\} esp32_mquickjs_wifi_radio_operation_t;', header).group(0)
    index = code.index('static struct {')
    code = code[:index] + types + code[index:]
    extra = ('wifi_action_tx_req_t', 'wifi_roc_req_t', 'wifi_event_action_tx_status_t',
             'wifi_event_roc_done_t', 'wifi_ap_record_t')
    declarations = sdk_types(profile, extra)
    index = code.index('static struct {')
    code = code[:index] + declarations[len(sdk_types(profile)):] + code[index:]
    code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_action_lane.h')
    code += unit(COMPONENT / 'src/modules/wifi_action/esp32_mquickjs_wifi_action_lane.c')
    code += re.search(r'static struct \{\n    esp32_mquickjs_wifi_action_lane_t[^}]*\} s_action = [^;]*;', radio).group(0)
    for name in ('wifi_radio_action_fence_t', 'wifi_radio_event_fence_t'):
        code += re.search(r'typedef struct \{[^}]*\} '+name+';', radio).group(0)
    code += BOUNDARIES
    for name in ('wifi_radio_lifecycle_fence', 'wifi_radio_action_event', 'wifi_radio_action_exact_locked',
                 'wifi_radio_action_parameters', 'wifi_radio_action_admit_locked',
                 'esp32_mquickjs_wifi_radio_action_send', 'esp32_mquickjs_wifi_radio_action_roc',
                 'esp32_mquickjs_wifi_radio_action_cancel', 'esp32_mquickjs_wifi_radio_action_status',
                 'esp32_mquickjs_wifi_radio_action_retire', 'esp32_mquickjs_wifi_radio_end_operation'):
        code += extract(radio, name)
    return code


class WiFiActionRadio(unittest.TestCase):
    def test_radio_admission_native_completion_fences_cancel_and_owner_retention(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            for ap in (False, True):
                with self.subTest(profile=profile, softap=ap):
                    compile_run(self, radio_code(profile, ap) + MAIN)


BOUNDARIES = r'''
#include <stdlib.h>
#define ESP_ERR_TIMEOUT 99
#define ESP_ERR_WIFI_NOT_CONNECT 100
#define ESP32QJS_WIFI_RADIO_CONTROL_EVENT 7
#define WIFI_EVENT_ACTION_TX_STATUS 19
#define WIFI_EVENT_ROC_DONE 20
#define RADIO_EVENTS_IDLE 0
typedef int esp_event_base_t;
static unsigned action_calls,cancel_calls,fence_calls,posts;
static int sdk_error,cancel_error,post_error,fence_error;
static int quiescent_error=ESP_ERR_TIMEOUT;
static unsigned quiescent_calls;
static bool quiescent_event_race;
static bool connected,early,late_in_post,queued;
static uint8_t sdk_id=255;
static wifi_radio_action_fence_t marker;
static void wifi_radio_action_event(int32_t id,const void *data);
static void wifi_radio_lifecycle_fence(void *,esp_event_base_t,int32_t,void *);
static int esp32_mquickjs_wifi_action_receive(uint8_t *a,uint8_t *b,size_t c,uint8_t d){(void)a;(void)b;(void)c;(void)d;return 0;}
static int esp_wifi_get_mode(wifi_mode_t *mode){assert(locks && !critical);*mode=s_radio.effective_mode;return 0;}
static int wifi_radio_validate_regulatory_channel(uint8_t channel){assert(locks && !critical);return channel<=11?0:ESP_ERR_INVALID_ARG;}
static int wifi_radio_get_channel_locked(uint8_t *channel,wifi_second_chan_t *secondary,uint32_t *generation){
    assert(locks && !critical);*channel=6;*secondary=WIFI_SECOND_CHAN_NONE;*generation=s_radio.generation;return 0;
}
static int esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap){(void)ap;assert(locks && !critical);return connected?0:ESP_ERR_WIFI_NOT_CONNECT;}
static void action_event(unsigned status){
    wifi_event_action_tx_status_t event={.ifx=WIFI_IF_STA,.context=(uint32_t)(uintptr_t)esp32_mquickjs_wifi_action_receive,
        .channel=6,.op_id=sdk_id,.status=status};wifi_radio_action_event(WIFI_EVENT_ACTION_TX_STATUS,&event);
}
/* Undef only to declare the injected SDK functions; reapply real production
 * aliases below. The fixture's existing invalidation counter remains active. */
#undef esp_wifi_action_tx_req
#undef esp_wifi_remain_on_channel
static int esp_wifi_action_tx_req(wifi_action_tx_req_t *request){
    assert(locks && !critical && s_radio.operation.identity && s_action.lease.acquired);
    assert(s_radio.clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_ACTION]==1);
    if(request->type==WIFI_OFFCHAN_TX_CANCEL){++cancel_calls;assert(request->op_id==sdk_id);return cancel_error;}
    ++action_calls;request->op_id=sdk_id;if(early){action_event(0);action_event(2);}return sdk_error;
}
static int esp_wifi_remain_on_channel(wifi_roc_req_t *request){
    assert(locks && !critical && s_radio.operation.identity);
    if(request->type==WIFI_ROC_CANCEL){++cancel_calls;return cancel_error;}
    ++action_calls;request->op_id=sdk_id;return sdk_error;
}
#define esp_wifi_action_tx_req(...) WIFI_RADIO_MUTATION(esp_wifi_action_tx_req(__VA_ARGS__))
#define esp_wifi_remain_on_channel(...) WIFI_RADIO_MUTATION(esp_wifi_remain_on_channel(__VA_ARGS__))
static int esp32_mquickjs_wifi_action_sdk_fence(void){assert(locks && !critical);++fence_calls;return fence_error;}
static int esp32_mquickjs_wifi_action_sdk_quiescent(void) {
    assert(locks && !critical);++quiescent_calls;
    if(quiescent_event_race){quiescent_event_race=false;action_event(99);}
    return quiescent_error;
}
static int esp_event_post(int base,int id,const void *data,size_t length,unsigned ticks){
    assert(locks && !critical && base==7 && id==2 && length==sizeof(marker) && !ticks);++posts;
    if(late_in_post){late_in_post=false;action_event(2);}
    if(post_error)return post_error;marker=*(const wifi_radio_action_fence_t *)data;queued=true;return 0;
}
'''

MAIN = r'''
static void reset_action(void){
    reset_vendor();s_radio.next_operation_identity=1;s_radio.effective_mode=WIFI_MODE_STA;
    memset(&s_action,0,sizeof(s_action));s_action.lane.next_identity=1;
    action_calls=cancel_calls=fence_calls=posts=0;sdk_error=cancel_error=post_error=fence_error=0;
    connected=early=late_in_post=queued=false;sdk_id=255;
    quiescent_error=ESP_ERR_TIMEOUT;quiescent_calls=0;quiescent_event_race=false;
}
static void deliver(void){assert(queued);wifi_radio_operation_lock();wifi_radio_lifecycle_fence(NULL,7,2,&marker);wifi_radio_operation_unlock();queued=false;}
static void event(unsigned status){wifi_radio_operation_lock();action_event(status);wifi_radio_operation_unlock();}
static unsigned action_owners(void){return s_radio.clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_ACTION];}
int main(void){
    size_t size=sizeof(wifi_action_tx_req_t)+1;wifi_action_tx_req_t *request=calloc(1,size);assert(request);
    *request=(wifi_action_tx_req_t){.ifx=WIFI_IF_STA,.type=WIFI_OFFCHAN_TX_REQ,.channel=6,.wait_time_ms=100,
        .rx_cb=esp32_mquickjs_wifi_action_receive,.data_len=1};
    esp32_mquickjs_wifi_action_token_t token={0},stale;esp32_mquickjs_wifi_action_lane_t state;
    reset_action();request->data_len=UINT32_MAX;
    assert(esp32_mquickjs_wifi_radio_action_send(request,size,&token)==ESP_ERR_INVALID_ARG && !action_owners() && !action_calls);
    request->data_len=1;request->wait_time_ms=0;
    assert(esp32_mquickjs_wifi_radio_action_send(request,size,&token)==ESP_ERR_INVALID_ARG && !action_owners());request->wait_time_ms=100;
    s_radio.started=false;assert(esp32_mquickjs_wifi_radio_action_send(request,size,&token)==ESP_ERR_INVALID_STATE && !action_owners());s_radio.started=true;
    connected=true;request->channel=1;
    assert(esp32_mquickjs_wifi_radio_action_send(request,size,&token)==ESP_ERR_INVALID_STATE && !action_owners());
    connected=false;s_radio.effective_mode=WIFI_MODE_APSTA;
    assert(esp32_mquickjs_wifi_radio_action_send(request,size,&token)==ESP_ERR_INVALID_STATE && !action_owners());
    s_radio.effective_mode=WIFI_MODE_STA;request->channel=6;early=true;
    assert(esp32_mquickjs_wifi_radio_action_send(request,size,&token)==0 && token.identity && action_owners()==1 && invalidations==1);
    stale=token;wifi_radio_operation_lock();wifi_radio_release_locked(&s_action.lease);wifi_radio_operation_unlock();assert(action_owners()==1);
    esp32_mquickjs_wifi_radio_operation_t operation=s_radio.operation;esp32_mquickjs_wifi_radio_end_operation(&operation);assert(s_radio.operation.identity);
    assert(esp32_mquickjs_wifi_radio_action_retire(&token,&state)==ESP_ERR_TIMEOUT && fence_calls==1 && queued && action_owners()==1);
    deliver();assert(esp32_mquickjs_wifi_radio_action_retire(&token,&state)==0 && !action_owners() && !token.identity && !s_radio.operation.identity);
    assert(esp32_mquickjs_wifi_radio_action_cancel(&stale)==ESP_ERR_INVALID_STATE);
    assert(state.tx_status==0 && state.terminal_status==2);

    reset_action();request->op_id=0;
    assert(esp32_mquickjs_wifi_radio_action_send(request,size,&token)==0);event(0);
    assert(esp32_mquickjs_wifi_radio_action_retire(&token,&state)==ESP_ERR_TIMEOUT && !posts && !fence_calls && action_owners()==1);
    event(2);post_error=66;
    assert(esp32_mquickjs_wifi_radio_action_retire(&token,&state)==66 && !s_action.fence_posted && action_owners()==1);
    post_error=0;late_in_post=true;
    assert(esp32_mquickjs_wifi_radio_action_retire(&token,&state)==ESP_ERR_TIMEOUT);deliver();
    assert(!s_action.lane.event_fenced && !s_action.fence_posted);
    assert(esp32_mquickjs_wifi_radio_action_retire(&token,&state)==ESP_ERR_TIMEOUT && fence_calls==2);deliver();
    assert(esp32_mquickjs_wifi_radio_action_retire(&token,&state)==0 && !action_owners());

    reset_action();request->op_id=0;sdk_error=77;
    assert(esp32_mquickjs_wifi_radio_action_send(request,size,&token)==77 && token.identity && action_owners()==1);
    cancel_error=88;assert(esp32_mquickjs_wifi_radio_action_cancel(&token)==88 && cancel_calls==1 && action_owners()==1);
    cancel_error=0;assert(esp32_mquickjs_wifi_radio_action_cancel(&token)==0 && cancel_calls==2);
    assert(esp32_mquickjs_wifi_radio_action_cancel(&token)==0 && cancel_calls==2);
    assert(esp32_mquickjs_wifi_radio_action_retire(&token,&state)==ESP_ERR_TIMEOUT && !posts && action_owners()==1);
    event(3);assert(esp32_mquickjs_wifi_radio_action_retire(&token,&state)==ESP_ERR_TIMEOUT);deliver();
    assert(esp32_mquickjs_wifi_radio_action_retire(&token,&state)==0 && state.submit_error==77);

    reset_action();wifi_roc_req_t roc={.ifx=WIFI_IF_STA,.type=WIFI_ROC_REQ,.channel=1,.wait_time_ms=20,.rx_cb=esp32_mquickjs_wifi_action_receive};
    sdk_id=0;assert(esp32_mquickjs_wifi_radio_action_roc(&roc,&token)==0 && roc.op_id==0 && action_owners()==1);
    wifi_event_roc_done_t done={.context=(uint32_t)(uintptr_t)esp32_mquickjs_wifi_action_receive,.channel=1,.op_id=0,.status=WIFI_ROC_DONE};
    wifi_radio_operation_lock();wifi_radio_action_event(WIFI_EVENT_ROC_DONE,&done);wifi_radio_operation_unlock();
    assert(esp32_mquickjs_wifi_radio_action_retire(&token,&state)==ESP_ERR_TIMEOUT);deliver();
    assert(esp32_mquickjs_wifi_radio_action_retire(&token,&state)==0 && !action_owners());
    /* No terminal event: SDK queue proof alone still cannot release Radio.
     * An unknown matching event during the query invalidates its revision. */
    reset_action();request->op_id=0;sdk_error=77;
    assert(esp32_mquickjs_wifi_radio_action_send(request,size,&token)==77 && token.identity);
    assert(esp32_mquickjs_wifi_radio_action_retire(&token,&state)==ESP_ERR_TIMEOUT && !queued);
    quiescent_error=0;quiescent_event_race=true;
    assert(esp32_mquickjs_wifi_radio_action_retire(&token,&state)==ESP_ERR_TIMEOUT && !queued && action_owners()==1);
    assert(state.ambiguous && !state.sdk_quiescent);
    post_error=66;
    assert(esp32_mquickjs_wifi_radio_action_retire(&token,&state)==66 && action_owners()==1);
    assert(state.sdk_quiescent && !state.terminal && !state.event_fenced);
    unsigned cancels=cancel_calls;
    assert(esp32_mquickjs_wifi_radio_action_cancel(&token)==0 && cancel_calls==cancels);
    post_error=0;assert(esp32_mquickjs_wifi_radio_action_retire(&token,&state)==ESP_ERR_TIMEOUT && queued);
    deliver();assert(esp32_mquickjs_wifi_radio_action_retire(&token,&state)==0 && !action_owners());
    assert(state.sdk_quiescent && state.ambiguous && !state.terminal && state.terminal_status==-1);
    free(request);return 0;
}
'''
