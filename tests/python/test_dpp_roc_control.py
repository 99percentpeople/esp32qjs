"""Deferred production ROC producer, numeric dispatch and native retirement.

The native driver, task/critical-section boundary and eloop admission are
controlled here. This implementation wave only AST-parses the fixture.
"""
from pathlib import Path
import re
import unittest
from test_wireless_control_regression import compile_run

ROOT = Path(__file__).resolve().parents[2]
PARTS = ROOT / 'components/esp32_mquickjs/src/modules/wifi_dpp'


def strip_includes(source):
    return re.sub(r'^#(?:include|pragma once).*\n', '', source, flags=re.M)


class DppRocControl(unittest.TestCase):
    def test_native_completion_reuse_cancellation_and_failed_admission(self):
        header = strip_includes((ROOT / 'components/esp32_mquickjs/internal/esp32_mquickjs_wifi_dpp_result.h').read_text())
        production = strip_includes((PARTS / 'esp32_mquickjs_dpp_roc.inc').read_text())
        compile_run(self, TYPES + header + BOUNDARIES + production + MAIN)


TYPES = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdatomic.h>
typedef int esp_err_t;
typedef int wifi_roc_done_status_t;
typedef struct {int unused;}wifi_event_action_tx_status_t;
typedef struct {char unused;}esp_dpp_config_data_t;
typedef void (*eloop_timeout_handler)(void*,void*);
typedef void (*wifi_action_roc_done_cb_t)(uint32_t,uint8_t,wifi_roc_done_status_t);
typedef struct {
 int ifx,type,sec_channel;uint8_t op_id,channel;void *rx_cb;
 wifi_action_roc_done_cb_t done_cb;
}wifi_roc_req_t;
#define WIFI_IF_STA 0
#define WIFI_SECOND_CHAN_NONE 0
#define WIFI_ROC_CANCEL 0
#define WIFI_ROC_REQ 1
#define WIFI_ROC_DONE 0
#define WIFI_ROC_FAIL 1
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERR_NO_MEM 2
#define ESP_ERR_NOT_FINISHED 3
#define ESP_ERR_TIMEOUT 4
#define DPP_ROC_EVENT_HANDLED 1
typedef unsigned portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
static unsigned critical,depth;
#define portENTER_CRITICAL(lock) do{(void)(lock);assert(!critical);critical++;}while(0)
#define portEXIT_CRITICAL(lock) do{(void)(lock);assert(critical==1);critical--;}while(0)
'''

BOUNDARIES = r'''
static struct {esp32_mquickjs_wifi_dpp_result_status_t status;bool ever_bound;}result={.ever_bound=true};
static __typeof__(result)*s_dpp_result=&result;
static struct {bool dpp_listen_ongoing;}s_dpp_ctx;
static uint32_t s_dpp_generation=1;
static atomic_bool s_dpp_init_done=true,dpp_shutting_down,roc_in_progress;
static bool s_dpp_roc_submitted,s_dpp_roc_cancelled;
static void *s_action_rx_cb=(void*)0x1234,*s_dpp_event_group=(void*)1;
static bool native_task=true,present,synchronous,complete_cancel=true;
static esp_err_t submit_error,cancel_error,abort_error;
static unsigned submits,cancels,next_channels;
static uint8_t driver_id=7;
static unsigned bits;
static wifi_action_roc_done_cb_t native_done;
static bool current_task_is_wifi_task(void){assert(!critical);return native_task;}
static int dpp_api_lock(void){assert(!critical);depth++;return 0;}
static int dpp_api_unlock(void){assert(!critical&&depth);depth--;return 0;}
static void esp32qjs_dpp_result_count(uint32_t*p){if(*p!=UINT32_MAX)++*p;}
static void os_event_group_set_bits(void*p,unsigned value){assert(p&&!critical&&depth);bits|=value;}
static void os_event_group_clear_bits(void*p,unsigned value){assert(p&&!critical&&depth);bits&=~value;}
static int esp32_mquickjs_wifi_action_sdk_quiescent(void){assert(native_task&&!critical&&depth);return present?ESP_ERR_TIMEOUT:ESP_OK;}
static int esp32qjs_wifi_chm_timer_service_native(void){assert(native_task&&!critical&&depth);return 0;}
struct queued {eloop_timeout_handler fn;void *high,*low;};
static struct queued queue[4];
static bool queue_full;
static int esp32qjs_dpp_async_register(unsigned s,unsigned us,eloop_timeout_handler fn,void*h,void*l){
 assert(!critical&&depth&&!s&&us==10000);if(queue_full)return -1;
 for(unsigned i=0;i<4;i++)if(!queue[i].fn){queue[i]=(struct queued){fn,h,l};return 0;}return -1;
}
static int esp32qjs_dpp_async_cancel(eloop_timeout_handler fn,void*h,void*l){
 assert(!critical&&depth);int count=0;
 for(unsigned i=0;i<4;i++)if(queue[i].fn==fn&&queue[i].high==h&&queue[i].low==l){queue[i].fn=NULL;count++;}return count;
}
static struct queued detach(void){for(unsigned i=0;i<4;i++)if(queue[i].fn){struct queued q=queue[i];queue[i].fn=NULL;return q;}assert(0);return(struct queued){0};}
static int esp_wifi_remain_on_channel(wifi_roc_req_t*request);
static void dpp_listen_next_channel(void*a,void*b){(void)a;(void)b;assert(!critical);next_channels++;}
'''

MAIN = r'''
static int esp_wifi_remain_on_channel(wifi_roc_req_t*request){
 assert(!critical&&depth&&native_task&&request->rx_cb==s_action_rx_cb);
 if(request->type==WIFI_ROC_REQ){
  submits++;present=true;native_done=request->done_cb;assert(native_done);
  assert(s_dpp_roc.held&&!s_dpp_roc.returned&&roc_in_progress&&!(bits&DPP_ROC_EVENT_HANDLED));
  request->op_id=driver_id;
  if(synchronous){native_done(0x1234,driver_id,WIFI_ROC_DONE);assert(s_dpp_roc.held&&s_dpp_roc.complete&&!s_dpp_roc.returned);present=false;}
  return submit_error;
 }
 assert(request->type==WIFI_ROC_CANCEL&&request->op_id==driver_id&&request->channel==6);
 cancels++;if(cancel_error)return cancel_error;
 if(complete_cancel){native_done(0x1234,driver_id,WIFI_ROC_FAIL);present=false;}
 return 0;
}
static void dpp_abort_failure_locked(uint32_t error){
 assert(depth&&!critical);abort_error=error;result.status.terminal=true;result.status.error=error;
 s_dpp_ctx.dpp_listen_ongoing=false;esp32qjs_dpp_roc_cancel_locked();dpp_api_unlock();
}
static esp_err_t submit(void){
 wifi_roc_req_t request={.type=WIFI_ROC_REQ,.channel=6,.rx_cb=s_action_rx_cb};
 dpp_api_lock();esp_err_t error=esp32qjs_dpp_roc_submit_locked(&request);dpp_api_unlock();return error;
}
static void tick(void){struct queued q=detach();q.fn(q.high,q.low);assert(!depth&&!critical);}
static int cancel(void){dpp_api_lock();int error=esp32qjs_dpp_roc_cancel_locked();dpp_api_unlock();return error;}
int main(void){
 /* Scheduler reservation failure precedes native ownership or bit changes. */
 bits=1;queue_full=true;assert(submit()==ESP_ERR_NO_MEM&&!submits&&!esp32qjs_dpp_roc_held()&&bits==1);queue_full=false;
 assert(!submit()&&submits==1&&s_dpp_roc.held&&s_dpp_roc.returned);
 native_done(0x5678,driver_id,WIFI_ROC_DONE);native_done(0x1234,driver_id+1,WIFI_ROC_DONE);
 assert(!s_dpp_roc.complete&&s_dpp_roc.ignored==2);
 native_done(0x1234,driver_id,WIFI_ROC_DONE);native_done(0x1234,driver_id,WIFI_ROC_DONE);
 assert(s_dpp_roc.complete&&s_dpp_roc.duplicates==1);
 s_dpp_ctx.dpp_listen_ongoing=true;tick();assert(s_dpp_roc.held&&!next_channels);
 present=false;tick();assert(!s_dpp_roc.held&&next_channels==1&&bits==1&&!roc_in_progress);
 /* A callback during submission is captured, not dispatched before return. */
 synchronous=true;assert(!submit()&&s_dpp_roc.complete&&s_dpp_roc.held);tick();assert(!s_dpp_roc.held&&next_channels==2);synchronous=false;
 /* Detached old dispatch uses its non-reused ticket, even after driver ID
  * wrap/reuse. No old queued status is fed through the callback as if native. */
 assert(!submit());struct queued stale=detach();uint64_t old=s_dpp_roc.ticket;
 assert(!cancel()&&!s_dpp_roc.held);assert(!submit()&&s_dpp_roc.ticket>old);
 stale.fn(stale.high,stale.low);assert(s_dpp_roc.held&&!s_dpp_roc.complete&&roc_in_progress);
 assert(!cancel());
 for(unsigned i=0;i<300;i++){driver_id=(uint8_t)i;assert(!submit());old=s_dpp_roc.ticket;stale.fn(stale.high,stale.low);assert(s_dpp_roc.ticket==old&&s_dpp_roc.held);assert(!cancel());}
 /* Successful cancellation submission is not repeated while the physical
  * record remains present; a failed cancellation is retried exactly. */
 assert(!submit());complete_cancel=false;unsigned n=cancels;
 assert(cancel()==ESP_ERR_TIMEOUT&&cancels==n+1&&s_dpp_roc.held);
 assert(cancel()==ESP_ERR_TIMEOUT&&cancels==n+1);present=false;assert(!cancel()&&cancels==n+1);
 complete_cancel=true;assert(!submit());cancel_error=77;n=cancels;
 assert(cancel()==77&&cancels==n+1&&s_dpp_roc.held);cancel_error=0;assert(!cancel()&&cancels==n+2);
 /* Native submission errors retain exact cleanup obligations. */
 submit_error=78;assert(submit()==78&&s_dpp_roc.held&&result.status.roc_submit_error==78);
 submit_error=0;assert(!cancel());
 /* Lost/silent native completion fails explicitly after physical retirement. */
 assert(!submit());present=false;tick();assert(abort_error==ESP_ERR_INVALID_STATE&&!s_dpp_roc.held);
 result.status.terminal=false;abort_error=0;
 /* A later dispatch admission failure captures failure and cancels native
  * work. Callback capture itself has no queue/allocator dependency. */
 assert(!submit());queue_full=true;tick();assert(abort_error==ESP_ERR_NO_MEM&&!s_dpp_roc.held);queue_full=false;
 result.status.terminal=false;
 /* Result snapshots expose only this SDK generation, without secret data. */
 esp32_mquickjs_wifi_dpp_result_status_t out={0};esp32qjs_dpp_roc_status_locked(&out);
 assert(out.roc_operation==s_dpp_roc.ticket&&!out.roc_held);
 s_dpp_generation++;memset(&out,0,sizeof(out));esp32qjs_dpp_roc_status_locked(&out);assert(!out.roc_operation);
 s_dpp_roc_last_ticket=UINT64_MAX;n=submits;assert(submit()==ESP_ERR_NO_MEM&&submits==n&&!s_dpp_roc.held);
 assert(!depth&&!critical);return 0;
}
'''
