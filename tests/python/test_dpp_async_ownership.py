"""Deferred execution of the production DPP eloop ticket registry.

Only the allocator and native scheduler are controlled. AST checks do not
constitute execution or Wi-Fi/RTOS qualification.
"""
from pathlib import Path
import unittest
from test_wireless_control_regression import compile_run

ROOT = Path(__file__).resolve().parents[2]
PART = ROOT / 'components/esp32_mquickjs/src/modules/wifi_dpp/esp32_mquickjs_dpp_async.inc'


class DppAsyncOwnership(unittest.TestCase):
    def test_detached_cancel_payload_lifetime_stale_tickets_and_bounded_admission(self):
        compile_run(self, BOUNDARIES + PART.read_text() + MAIN)


BOUNDARIES = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
typedef int esp_err_t;
typedef void (*eloop_timeout_handler)(void *,void *);
#define ESP_OK 0
#define ESP_ERR_NOT_FINISHED 1
#define ESP_ERR_INVALID_STATE 2
#define ELOOP_ALL_CTX ((void *)-1)
struct action_rx_param {unsigned frm_len;unsigned char bytes[];};
static struct {bool dpp_deinit_pending;}s_dpp_ctx;
static atomic_bool s_dpp_init_done=true,dpp_shutting_down;
static int locked,live,fail_at,allocations,calls,bootstrap_calls;
static int dpp_api_lock(void){locked++;return 0;}
static int dpp_api_unlock(void){assert(locked);locked--;return 0;}
static void *os_zalloc(size_t n){if(++allocations==fail_at)return NULL;void *p=calloc(1,n);assert(p);live++;return p;}
static void os_free(void *p){if(p){live--;free(p);}}
static void bin_clear_free(void *p,size_t n){if(p){volatile unsigned char *q=p;for(size_t i=0;i<n;i++)q[i]=0;os_free(p);}}
struct queued {eloop_timeout_handler fn;void *a,*b;};
static struct queued queue[64];
static bool queue_fail;
static int eloop_register_timeout(unsigned s,unsigned u,eloop_timeout_handler fn,void*a,void*b){
 (void)s;(void)u;assert(locked);if(queue_fail)return -1;
 for(unsigned i=0;i<64;i++)if(!queue[i].fn){queue[i]=(struct queued){fn,a,b};return 0;}return -1;
}
static int eloop_cancel_timeout(eloop_timeout_handler fn,void*a,void*b){
 assert(locked);for(unsigned i=0;i<64;i++)if(queue[i].fn==fn&&queue[i].a==a&&queue[i].b==b){queue[i].fn=NULL;return 1;}return 0;
}
static struct queued detach_first(void){for(unsigned i=0;i<64;i++)if(queue[i].fn){struct queued q=queue[i];queue[i].fn=NULL;return q;}abort();}
'''

MAIN = r'''
static void esp_dpp_rx_action(void *p,void*u){(void)u;calls++;assert(esp32qjs_dpp_async_drain_locked()==ESP_ERR_NOT_FINISHED);os_free(p);}
static void esp_dpp_bootstrap_gen(void*p,void*u){(void)p;(void)u;bootstrap_calls++;}
static void nop(void*p,void*u){(void)p;(void)u;calls++;}
int main(void){
 s_dpp_generation=1;
 fail_at=1;assert(esp32qjs_dpp_async_register(0,0,nop,NULL,NULL)==-1&&!live);fail_at=0;
 queue_fail=true;assert(esp32qjs_dpp_async_register(0,0,nop,NULL,NULL)==-1&&!live);queue_fail=false;
 struct action_rx_param *rx=os_zalloc(sizeof(*rx)+4);rx->frm_len=4;
 assert(!esp32qjs_dpp_async_register(0,0,esp_dpp_rx_action,rx,NULL));
 struct queued old=detach_first();
 assert(esp32qjs_dpp_async_cancel(esp_dpp_rx_action,rx,NULL)==0&&live==2&&s_dpp_async_count==1);
 old.fn(old.a,old.b);assert(!calls&&!live&&!s_dpp_async_count);
 assert(!esp32qjs_dpp_async_register(0,0,nop,NULL,NULL));old.fn(old.a,old.b);assert(!calls&&s_dpp_async_count==1);
 struct queued next=detach_first();next.fn(next.a,next.b);assert(calls==1&&!live);
 for(unsigned i=0;i<ESP32QJS_DPP_ASYNC_MAX;i++)assert(!esp32qjs_dpp_async_register(1,0,nop,NULL,NULL));
 assert(esp32qjs_dpp_async_register(0,0,nop,NULL,NULL)==-1);
 assert(esp32qjs_dpp_async_cancel(nop,ELOOP_ALL_CTX,ELOOP_ALL_CTX)==ESP32QJS_DPP_ASYNC_MAX&&!live);
 rx=os_zalloc(sizeof(*rx));assert(!esp32qjs_dpp_async_register(0,0,esp_dpp_rx_action,rx,NULL));
 next=detach_first();next.fn(next.a,next.b);assert(calls==2&&!live&&!s_dpp_async_active);
 rx=os_zalloc(sizeof(*rx));assert(!esp32qjs_dpp_async_register(0,0,esp_dpp_rx_action,rx,NULL));
 next=detach_first();s_dpp_generation++;next.fn(next.a,next.b);assert(calls==2&&!live);
 rx=os_zalloc(sizeof(*rx));assert(!esp32qjs_dpp_async_register(0,0,esp_dpp_rx_action,rx,NULL));
 s_dpp_ctx.dpp_deinit_pending=true;next=detach_first();next.fn(next.a,next.b);assert(!live);
 assert(esp32qjs_dpp_async_register(0,0,nop,NULL,NULL)==-1);s_dpp_ctx.dpp_deinit_pending=false;
 assert(!esp32qjs_dpp_async_register(0,0,esp_dpp_bootstrap_gen,(void*)1,(void*)2));
 s_dpp_ctx.dpp_deinit_pending=true;next=detach_first();next.fn(next.a,next.b);assert(bootstrap_calls==1&&!live);
 s_dpp_ctx.dpp_deinit_pending=false;s_dpp_async_last_ticket=UINT64_MAX;
 assert(esp32qjs_dpp_async_register(0,0,nop,NULL,NULL)==-1&&!live&&!locked);
 return 0;
}
'''
