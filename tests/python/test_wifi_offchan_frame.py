"""Deferred production off-channel frame provenance and address reuse checks.

The production C helper is compiled with controlled SDK output/reset/recycle
boundaries when the Wi-Fi test wave runs. Do not run during implementation.
"""
from pathlib import Path
import re
import unittest
from test_wireless_control_regression import compile_run

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'components/esp32_mquickjs/src/modules/wifi_action/esp32_mquickjs_wifi_offchan_frame.c'


class OffchanFrameIdentity(unittest.TestCase):
    def test_actual_frame_reset_recycle_and_synchronous_output(self):
        source = re.sub(r'^#include[^\n]*\n', '', SOURCE.read_text(), flags=re.M)
        header = re.sub(r'^#(?:include|pragma once).*\n', '',
            (ROOT / 'components/esp32_mquickjs/internal/esp32_mquickjs_wifi_offchan_frame.h').read_text(), flags=re.M)
        for c5 in (0, 1):
            with self.subTest(c5=c5):
                compile_run(self, BOUNDARIES.replace('#define CONFIG_IDF_TARGET_ESP32C5 0',
                    f'#define CONFIG_IDF_TARGET_ESP32C5 {c5}') + header + source + MAIN)


BOUNDARIES = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NOT_FINISHED 3
#define ESP_ERR_WIFI_POST 0x3012
#define ESP_ERR_NO_MEM 6
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define CONFIG_ESP_WIFI_DPP_SUPPORT 1
#define CONFIG_IDF_TARGET_ESP32C5 0
#define CONFIG_SOC_WIFI_HE_SUPPORT 0
#define IRAM_ATTR
#define DRAM_ATTR
typedef unsigned portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
static unsigned critical;
#define portENTER_CRITICAL_SAFE(m) do{(void)m;assert(!critical);critical++;}while(0)
#define portEXIT_CRITICAL_SAFE(m) do{(void)m;assert(critical==1);critical--;}while(0)
uint8_t g_offchan_ctx[28];
static unsigned posted,delivered,recycled;
static int output_error,last_status;
static uint8_t last_interface;
static bool output_completes,output_recycles,reuse_during_recycle;
static unsigned wakes;
static bool wake_fails;
#define WIFI_EVENT 1
#define WIFI_EVENT_ACTION_TX_STATUS 19
#define WIFI_EVENT_ROC_DONE 20
typedef struct {int ifx;uint32_t context;int status;uint8_t op_id,channel;}wifi_event_action_tx_status_t;
typedef struct {uint32_t context;int status;uint8_t op_id,channel;}wifi_event_roc_done_t;
static unsigned captured,observed,forwarded;
static bool esp32qjs_dpp_is_action_callback(uintptr_t callback){return callback==0x1234;}
static void esp32qjs_dpp_tx_status_capture(const wifi_event_action_tx_status_t*event){assert(!critical&&event->context==0x1234);captured++;}
static int esp_event_post(int base,int id,const void*data,size_t size,int wait){
 assert(!critical&&base==WIFI_EVENT&&data&&!wait);
 if(id==19){assert(size==sizeof(wifi_event_action_tx_status_t)&&captured>observed);observed++;}
 else assert(id==20&&size==sizeof(wifi_event_roc_done_t));
 return 88;
}
int wifi_event_post(int id,void*data,size_t size){(void)id;(void)data;(void)size;assert(!critical);forwarded++;return 77;}
int pp_post(int signal,void*message){assert(!critical&&signal==5&&!message);wakes++;return wake_fails?-1:0;}
'''

MAIN = r'''
struct frame {uint8_t bytes[64],metadata[32];};
static void init_frame(struct frame *frame,uint8_t interface){
 memset(frame,0,sizeof(*frame));void*p=frame->metadata;
 unsigned offset=CONFIG_IDF_TARGET_ESP32C5?56:44,shift=CONFIG_IDF_TARGET_ESP32C5?18:19;
 memcpy(frame->bytes+offset,&p,sizeof(p));frame->bytes[CONFIG_IDF_TARGET_ESP32C5?30:26]=4;
 uint32_t flags=(uint32_t)interface<<shift;memcpy(frame->metadata+16,&flags,sizeof(flags));
}
static void native_operation(uint32_t context,uint8_t id,uint8_t interface,uint8_t channel){
 esp32qjs_wifi_offchan_record_reset(g_offchan_ctx,0,28);
 uint32_t ifx=interface;memcpy(g_offchan_ctx+4,&context,4);memcpy(g_offchan_ctx+8,&ifx,4);
 g_offchan_ctx[12]=id;g_offchan_ctx[2]=channel;
}
int ieee80211_post_hmac_tx(void *buffer){
 assert(!critical);posted++;
 if(output_completes)esp32qjs_wifi_offchan_frame_done(buffer,0);
 if(output_recycles)__wrap_esf_buf_recycle(buffer);
 return output_error;
}
void offchan_send_action_tx_status(uint8_t interface,int status){
 assert(!critical);delivered++;last_status=status;last_interface=interface;
}
void __real_esf_buf_recycle(void *buffer){
 assert(!critical);recycled++;assert(s_offchan_frame.buffer!=buffer);
 if(s_offchan_tx.buffer==buffer){
  esp32qjs_wifi_offchan_tx_status_t status;
  assert(!esp32qjs_wifi_offchan_tx_status(s_offchan_tx.status.ticket,&status));
  assert(status.recycling&&status.buffer_present&&!status.recycled);
  /* Unknown producer ownership stays faulted even during buffer recycling. */
  assert(esp32qjs_wifi_offchan_tx_release(status.ticket)==
         (status.unknown_frames?ESP_ERR_INVALID_STATE:ESP_ERR_NOT_FINISHED));
  assert(s_offchan_tx.status.held);
 }
 /* Another producer can reuse the address after native free, even before the
  * old wrapper returns. Its new binding must survive that return. */
 if(reuse_during_recycle){native_operation(0x9999,7,0,6);esp32qjs_wifi_offchan_frame_post(buffer);}
}
int main(void){
 struct frame one,two;init_frame(&one,0);init_frame(&two,0);
 native_operation(0x9999,7,0,6);assert(!esp32qjs_wifi_offchan_frame_post(&one));
 esp32qjs_wifi_offchan_frame_done(&two,0);assert(!delivered&&s_offchan_frame.buffer==&one);
 esp32qjs_wifi_offchan_frame_done(&one,1);assert(delivered==1&&last_status==1&&!last_interface&&!s_offchan_frame.buffer);
 esp32qjs_wifi_offchan_frame_done(&one,0);assert(delivered==1);
 /* Same driver ID/context after reset does not authorize an old EB, including
  * before the new operation has reached its deferred PM output. */
 esp32qjs_wifi_offchan_frame_post(&one);native_operation(0x9999,7,0,6);
 esp32qjs_wifi_offchan_frame_done(&one,0);assert(delivered==1);
 esp32qjs_wifi_offchan_frame_post(&two);esp32qjs_wifi_offchan_frame_done(&one,0);assert(delivered==1);
 esp32qjs_wifi_offchan_frame_done(&two,0);assert(delivered==2);
 /* Dropped-without-callback EB addresses are invalidated before reuse. */
 esp32qjs_wifi_offchan_frame_post(&one);__wrap_esf_buf_recycle(&one);
 esp32qjs_wifi_offchan_frame_done(&one,0);assert(delivered==2&&recycled==1);
 esp32qjs_wifi_offchan_frame_post(&one);reuse_during_recycle=true;__wrap_esf_buf_recycle(&one);reuse_during_recycle=false;
 assert(s_offchan_frame.buffer==&one);esp32qjs_wifi_offchan_frame_done(&one,0);assert(delivered==3);
 /* Synchronous completion/recycle cannot be undone after output returns. */
 output_completes=true;output_recycles=true;esp32qjs_wifi_offchan_frame_post(&one);
 assert(delivered==4&&!s_offchan_frame.buffer);output_completes=false;output_recycles=false;
 output_error=55;assert(esp32qjs_wifi_offchan_frame_post(&one)==55&&s_offchan_frame.buffer==&one);
 esp32qjs_wifi_offchan_record_reset(g_offchan_ctx,0,28);output_error=0;
 esp32qjs_wifi_offchan_frame_done(&one,0);assert(delivered==4);
 /* Other memsets do not invalidate the active frame. Interface comes from
  * the exact metadata bits used by the original completion caller. */
 init_frame(&one,1);native_operation(0x9999,255,1,11);esp32qjs_wifi_offchan_frame_post(&one);
 uint8_t other[28];assert(esp32qjs_wifi_offchan_record_reset(other,1,28)==other&&other[0]==1);
 esp32qjs_wifi_offchan_frame_done(&one,0);assert(delivered==5&&last_interface==1);
 wifi_event_action_tx_status_t event={.context=0x1234};
 assert(esp32qjs_wifi_offchan_event_post(19,&event,sizeof(event))==88&&captured==1&&observed==1);
 event.context=0x9999;assert(esp32qjs_wifi_offchan_event_post(19,&event,sizeof(event))==77&&forwarded==1&&captured==1);
 assert(esp32qjs_wifi_offchan_event_post(20,&event,sizeof(event))==77&&forwarded==2);
 wifi_event_roc_done_t roc={.context=0x1234};
 assert(esp32qjs_wifi_offchan_event_post(20,&roc,sizeof(roc))==88&&forwarded==2);
 /* A native record reset or TX completion cannot retire the physical EB. */
 init_frame(&one,0);native_operation(0x1234,7,0,6);
 assert(!esp32qjs_wifi_offchan_tx_reserve(1,0x1234,6));
 assert(!esp32qjs_wifi_offchan_frame_post(&one));
 esp32qjs_wifi_offchan_frame_done(&one,0);native_operation(0x1234,7,0,6);
 assert(esp32qjs_wifi_offchan_tx_release(1)==ESP_ERR_NOT_FINISHED);
 assert(esp32qjs_wifi_offchan_tx_reserve(2,0x1234,6)==ESP_ERR_INVALID_STATE);
 __wrap_esf_buf_recycle(&one);
 esp32qjs_wifi_offchan_tx_status_t status;
 assert(!esp32qjs_wifi_offchan_tx_status(1,&status)&&status.recycled&&!status.buffer_present);
 assert(!esp32qjs_wifi_offchan_tx_release(1));
 assert(esp32qjs_wifi_offchan_tx_release(1)==ESP_ERR_INVALID_STATE);
 /* Failed wake leaves exactly one posted EB; retries never re-append it. */
 assert(!esp32qjs_wifi_offchan_tx_reserve(2,0x1234,6));
 output_error=ESP_ERR_WIFI_POST;unsigned old_posts=posted;
 assert(esp32qjs_wifi_offchan_frame_post(&one)==ESP_ERR_WIFI_POST);output_error=0;
 wake_fails=true;assert(!esp32qjs_wifi_offchan_tx_poll_native(2)&&wakes==1);
 assert(!esp32qjs_wifi_offchan_tx_status(2,&status)&&status.wake_pending&&status.buffer_present);
 wake_fails=false;assert(!esp32qjs_wifi_offchan_tx_poll_native(2)&&wakes==2&&posted==old_posts+1);
 assert(!esp32qjs_wifi_offchan_tx_poll_native(2)&&wakes==2);
 uint64_t retiring=esp32qjs_wifi_offchan_frame_recycle(&one);
 assert(retiring==2&&esp32qjs_wifi_offchan_tx_release(2)==ESP_ERR_NOT_FINISHED);
 esp32qjs_wifi_offchan_frame_recycled(1);assert(s_offchan_tx.status.recycling);
 esp32qjs_wifi_offchan_frame_recycled(retiring);assert(!esp32qjs_wifi_offchan_tx_release(2));
 /* Synchronous native free cannot be reversed by output return. */
 assert(!esp32qjs_wifi_offchan_tx_reserve(3,0x1234,6));
 output_recycles=true;output_error=ESP_ERR_WIFI_POST;
 assert(esp32qjs_wifi_offchan_frame_post(&one)==ESP_ERR_WIFI_POST);
 output_recycles=false;output_error=0;
 assert(!esp32qjs_wifi_offchan_tx_status(3,&status)&&status.recycled&&!status.wake_pending);
 assert(!esp32qjs_wifi_offchan_tx_release(3));
 assert(esp32qjs_wifi_offchan_tx_reserve(3,0x1234,6)==ESP_ERR_INVALID_STATE);
 /* A second unaccounted producer poisons retirement, rather than silently
  * allowing a new owner after only one of its buffers was returned. */
 assert(!esp32qjs_wifi_offchan_tx_reserve(4,0x1234,6));
 esp32qjs_wifi_offchan_frame_post(&one);esp32qjs_wifi_offchan_frame_post(&two);
 __wrap_esf_buf_recycle(&one);__wrap_esf_buf_recycle(&two);
 assert(esp32qjs_wifi_offchan_tx_release(4)==ESP_ERR_INVALID_STATE);
 assert(!esp32qjs_wifi_offchan_tx_status(4,&status)&&status.unknown_frames);
 uint64_t next=0;assert(!esp32qjs_wifi_offchan_tx_allocate_ticket(&next)&&next==5);
 assert(esp32qjs_wifi_offchan_tx_allocate_ticket(&next)==ESP_ERR_INVALID_ARG);
 s_offchan_tx_allocated_ticket=UINT64_MAX;next=0;
 assert(esp32qjs_wifi_offchan_tx_allocate_ticket(&next)==ESP_ERR_NO_MEM&&!next);
 assert(!critical&&posted>delivered);return 0;
}
'''
