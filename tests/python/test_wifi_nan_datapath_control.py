"""Deferred production NDP callback/timeout regressions; no execution on import.

The pinned SDK bodies and framework guards are compiled by the stage runner.
Only native driver, allocator, netif and scheduling boundaries are injected.
This does not substitute for the native archive, security handshake or RTOS/RF
integration tests. The before cases must fail the same lifetime assertions.
"""
import unittest

from test_idf_nan_control import NanNativeControl, BASE
from wireless_vm_fixture import extract


class NanDatapathControl(unittest.TestCase):
    compile_run = NanNativeControl.compile_run
    sources = NanNativeControl.sources
    observer = NanNativeControl.observer

    def test_full_rx_null_data_and_timer_bodies_share_close_barrier(self):
        bridge = (BASE / 'src/modules/wifi_nan/esp32_mquickjs_wifi_nan_sdk.inc').read_text()
        code = self.observer() + r'''
static unsigned rx_calls,recycles,null_calls,setup_calls,inactive_calls;
static void inside_native(void){assert(esp32_mquickjs_wifi_nan_sdk_service_quiesce(0)==ESP_ERR_TIMEOUT);}
void __real_nan_rx_naf(void *b,void *f,void *m){assert(b==(void*)1&&f==(void*)2&&m==(void*)3);inside_native();++rx_calls;}
void ic_ebuf_recycle_rx(void *b){assert(b==(void*)1);++recycles;}
void __real_nan_nulldata_txcb(void *b){assert(b==(void*)1);inside_native();++null_calls;}
void __real_nan_ndp_setup_timeout_process(void *p){assert(p==(void*)4);inside_native();++setup_calls;}
void __real_nan_ndp_inactivity_timeout_process(void *p){assert(p==(void*)5);inside_native();++inactive_calls;}
'''
        for name in ('__wrap_nan_rx_naf', '__wrap_nan_nulldata_txcb', 'esp32_mquickjs_wifi_nan_sdk_timer_process'):
            code += extract(bridge, name)
        self.compile_run(code + r'''
int main(void){
 __wrap_nan_rx_naf((void*)1,(void*)2,(void*)3);assert(rx_calls==1&&!recycles);
 assert(!esp32_mquickjs_wifi_nan_sdk_service_quiesce(0));
 __wrap_nan_rx_naf((void*)1,(void*)2,(void*)3);assert(rx_calls==1&&recycles==1);
 __wrap_nan_nulldata_txcb((void*)1);assert(!null_calls);
 assert(!esp32_mquickjs_wifi_nan_sdk_service_resume(0));
 __wrap_nan_nulldata_txcb((void*)1);assert(null_calls==1);
 assert(!esp32_mquickjs_wifi_nan_sdk_service_resume(0));
 esp32_mquickjs_wifi_nan_sdk_timer_process(42,(void*)4);assert(setup_calls==1);
 esp32_mquickjs_wifi_nan_sdk_timer_process(43,(void*)5);assert(!inactive_calls);
 assert(!esp32_mquickjs_wifi_nan_sdk_service_resume(0));
 esp32_mquickjs_wifi_nan_sdk_timer_process(43,(void*)5);assert(inactive_calls==1);
 assert(!esp32_mquickjs_wifi_nan_sdk_service_quiesce(0));
}
''')

    def test_confirm_event_oom_preserves_accepted_native_datapath(self):
        original, prepared = self.sources()
        for source, before in ((original, True), (prepared, False)):
            code = self.observer() + NDP_BOUNDARY + extract(source, 'nan_ndp_confirm_teardown')
            code += extract(source, 'nan_app_ndp_confirm_cb')
            self.compile_run(code + CONFIRM_OOM_MAIN, expect_failure=before)

    def test_rx_registration_failure_reports_original_error_and_retains_failed_cleanup(self):
        _, source = self.sources()
        code = self.observer() + NDP_BOUNDARY + extract(source, 'nan_ndp_confirm_teardown')
        code += extract(source, 'nan_app_ndp_confirm_cb')
        self.compile_run(code + r'''
int main(void){
 uint32_t observer=0;assert(!esp32_mquickjs_wifi_nan_sdk_observe(observe_ndp,&notices,&observer));
 rx_error=-71;end_error=-72;deliver_confirm();
 assert(!depth&&end_calls==1&&!resets&&live_ndl);
 assert(failed_error==-71&&cleanup_error==-72&&!(event_bits&NDP_ACCEPTED));
 assert(event_bits&NDP_REJECTED);
 assert(!esp32_mquickjs_wifi_nan_sdk_unobserve(&observer));
}
''')

    def test_successful_end_submission_does_not_free_inflight_host_security_record(self):
        original, prepared = self.sources()
        for source, before in ((original, True), (prepared, False)):
            code = self.observer() + NDP_BOUNDARY + extract(source, 'nan_ndp_confirm_teardown')
            self.compile_run(code + CONFIRM_STUB + r'''
int main(void){
 NAN_DATA_LOCK();nan_ndp_confirm_teardown(peer,7);NAN_DATA_UNLOCK();
 assert(!depth&&end_calls==1&&!resets&&live_ndl&&(event_bits&NDP_REJECTED));
}
''', expect_failure=before)

    def test_request_wait_timeout_does_not_free_native_request_record(self):
        original, prepared = self.sources()
        for source, before in ((original, True), (prepared, False)):
            code = self.observer() + NDP_BOUNDARY + REQUEST_BOUNDARY
            code += extract(source, 'esp_wifi_nan_datapath_req')
            self.compile_run(code + r'''
int main(void){
 wifi_nan_datapath_req_t req={.pub_id=3,.peer_mac={2,3,4,5,6,7}};
 assert(!esp_wifi_nan_datapath_req(&req));
 assert(!depth&&submitted&&live_ndl&&!resets);
}
''', expect_failure=before)

    def test_complete_naf_scope_survives_notice_and_is_frozen_before_stop(self):
        bridge = (BASE / 'src/modules/wifi_nan/esp32_mquickjs_wifi_nan_sdk.inc').read_text()
        self.compile_run(self.observer() + r'''
static unsigned calls;
static uint8_t buffer[80],metadata[32];
void __real_nan_naf_txcb(void *value){
 assert(value==buffer);++calls;
 /* An app callback/notice has already returned, but the native tail remains. */
 assert(esp32_mquickjs_wifi_nan_sdk_service_quiesce(0)==ESP_ERR_TIMEOUT);
 assert(esp32_mquickjs_wifi_nan_sdk_service_resume(0)==ESP_ERR_INVALID_STATE);
}
''' + extract(bridge, '__wrap_nan_naf_txcb') + r'''
int main(void){
 uint8_t*m=metadata;memcpy(buffer+56,&m,sizeof(m));metadata[9]=7;
 __wrap_nan_naf_txcb(buffer);assert(calls==1);
 assert(!esp32_mquickjs_wifi_nan_sdk_service_quiesce(0));
 __wrap_nan_naf_txcb(buffer);assert(calls==1);
 assert(!esp32_mquickjs_wifi_nan_sdk_service_resume(0));
 __wrap_nan_naf_txcb(buffer);assert(calls==2);
 assert(!esp32_mquickjs_wifi_nan_sdk_service_quiesce(0));
}
''')


NDP_BOUNDARY = r'''
#include <stdlib.h>
#include <string.h>
#define CONFIG_LWIP_ND6_SUPPORT_STATIC_ENTRIES 1
#define NDP_STATUS_ACCEPTED 1
#define NDP_STATUS_REJECTED 2
#define NDP_ACCEPTED 1U
#define NDP_REJECTED 2U
#define WIFI_IF_NAN 3
#define WIFI_EVENT_NDP_CONFIRM 4
#define WIFI_EVENT 5
#define NAN_IPV6_IDENTIFIER_LEN 8
#define MACADDR_COPY(a,b) memcpy(a,b,6)
#define MACADDR_EQUAL(a,b) (!memcmp(a,b,6))
#define MACSTR "peer"
#define MAC2STR(p) 0
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
#define ESP_LOG_BUFFER_HEXDUMP(...) ((void)0)
#define PP_HTONL(v) (v)
#define IS_ZERO_NAN_IPV6_IDENTIFIER(p) (!memcmp(p,"\0\0\0\0\0\0\0\0",8))
typedef unsigned EventBits_t;
typedef void *wifi_netif_driver_t;
typedef struct {uint32_t addr[4];} ip6_addr_t;
typedef struct {struct {ip6_addr_t ip6;} u_addr;} ip_addr_t;
typedef struct {uint8_t ndp_id,peer_mac[6];} wifi_nan_datapath_end_req_t;
typedef struct {uint8_t status,ndp_id,peer_nmi[6],peer_ndi[6],own_ndi[6],ipv6_identifier[8];uint16_t ssi_len;uint8_t ssi[];} wifi_event_ndp_confirm_t;
struct ndl_info {uint8_t peer_ndi[6];};
struct ndp_cb_peer_info {uint8_t ndp_id,peer_nmi[6],peer_ndi[6];uint8_t*ssi;uint16_t ssi_len;};
static struct {void*nan_netif;} s_nan_ctx={(void*)1};
static unsigned depth,resets,end_calls,notices,accepted,posts;
static int rx_error,end_error,failed_error,cleanup_error;
static bool live_ndl=true,fail_event;
static unsigned event_bits;
static void*nan_event_group=(void*)2;
static uint8_t peer[6]={2,3,4,5,6,7},null_mac[6];
static struct ndl_info ndl;
#define NAN_DATA_LOCK() do{assert(!depth);++depth;}while(0)
#define NAN_DATA_UNLOCK() do{assert(depth==1);--depth;}while(0)
static struct ndl_info*nan_find_ndl(uint8_t id,uint8_t*mac){assert(depth==1);return live_ndl&&id==7&&MACADDR_EQUAL(mac,peer)?&ndl:NULL;}
static void nan_reset_ndl(uint8_t id,bool all){assert(depth==1&&id==7&&!all);++resets;live_ndl=false;}
static esp_err_t esp_nan_internal_datapath_end(wifi_nan_datapath_end_req_t*req){assert(!depth&&req->ndp_id==7&&MACADDR_EQUAL(req->peer_mac,peer));++end_calls;return end_error;}
static void os_event_group_set_bits(void*g,unsigned bits){assert(g==nan_event_group);event_bits|=bits;}
static wifi_netif_driver_t esp_netif_get_io_driver(void*n){assert(n);return n;}
static void esp_netif_receive(void){}
static esp_err_t esp_wifi_register_if_rxcb(void*d,void(*receive)(void),void*n){assert(d==n&&receive==esp_netif_receive);return rx_error;}
static void esp_wifi_nan_get_ipv6_linklocal_from_mac(ip6_addr_t*out,const uint8_t*mac){assert(mac);memset(out,0,sizeof(*out));out->addr[2]=42;}
static esp_err_t esp_wifi_netif_set_static_neighbor(int iface,const uint8_t*mac,bool add){assert(!depth&&iface==WIFI_IF_NAN&&mac&&add);return ESP_OK;}
static void*os_zalloc(size_t size){return fail_event?NULL:calloc(1,size);}
static void os_free(void*p){free(p);}
static void nan_app_post_event(int id,void*data,size_t size){assert(!depth&&id==WIFI_EVENT_NDP_CONFIRM&&data&&size>=sizeof(wifi_event_ndp_confirm_t));++posts;}
static void observe_ndp(uint32_t identity,const esp32_mquickjs_wifi_nan_sdk_notice_t*n,void*opaque){
 assert(identity&&opaque==&notices);++notices;
 if(n->kind==ESP32_MQUICKJS_NAN_SDK_NDP_FAILED)failed_error=n->status;
 if(n->kind==ESP32_MQUICKJS_NAN_SDK_NDP_CLEANUP_FAILED)cleanup_error=n->status;
 if(n->kind==ESP32_MQUICKJS_NAN_SDK_NDP_ACCEPTED){
  assert(!depth&&n->ndp_id==7&&!memcmp(n->peer,peer,6));
  assert(n->size==sizeof(esp32_mquickjs_wifi_nan_sdk_ndp_data_t));
  const esp32_mquickjs_wifi_nan_sdk_ndp_data_t*d=n->data;
  assert(d->ssi_len==3&&d->ssi[0]==1&&d->ssi[2]==3&&d->peer_ndi[5]==8&&d->own_ndi[5]==9);
  ++accepted;
 }
}
static void nan_app_ndp_confirm_cb(uint8_t,struct ndp_cb_peer_info*,uint8_t[6],uint8_t[8]);
static void deliver_confirm(void){
 uint8_t payload[3]={1,2,3},own[6]={2,3,4,5,6,9},ipv6[8]={0};
 struct ndp_cb_peer_info info={.ndp_id=7,.peer_nmi={2,3,4,5,6,7},.peer_ndi={2,3,4,5,6,8},.ssi=payload,.ssi_len=3};
 nan_app_ndp_confirm_cb(NDP_STATUS_ACCEPTED,&info,own,ipv6);
}
'''

CONFIRM_OOM_MAIN = r'''
int main(void){
 uint32_t observer=0;assert(!esp32_mquickjs_wifi_nan_sdk_observe(observe_ndp,&notices,&observer));
 fail_event=true;deliver_confirm();
 assert(!depth&&!end_calls&&!resets&&live_ndl&&(event_bits&NDP_ACCEPTED));
 assert(accepted==1&&!posts);
 assert(!esp32_mquickjs_wifi_nan_sdk_service_quiesce(0));
 assert(!esp32_mquickjs_wifi_nan_sdk_service_resume(0));
 fail_event=false;deliver_confirm();assert(accepted==2&&posts==1);
 assert(!esp32_mquickjs_wifi_nan_sdk_unobserve(&observer));
}
'''

CONFIRM_STUB = r'''
static void nan_app_ndp_confirm_cb(uint8_t s,struct ndp_cb_peer_info*p,uint8_t own[6],uint8_t ip[8]){(void)s;(void)p;(void)own;(void)ip;}
'''

REQUEST_BOUNDARY = r'''
#define NAN_CAPS_NDPE_ATTR 1U
#define ESP_NAN_PUBLISH 1
#define NAN_DW_INTVL_MS 512U
#define pdFALSE 0
#define pdMS_TO_TICKS(v) (v)
typedef struct {uint8_t pub_id,peer_mac[6];bool confirm_required;} wifi_nan_datapath_req_t;
struct peer_svc_info {uint8_t svc_id,type,own_svc_id,peer_nmi[6];uint32_t device_caps;};
static struct peer_svc_info peer_service={.svc_id=3,.type=ESP_NAN_PUBLISH,.own_svc_id=1,.peer_nmi={2,3,4,5,6,7}};
static bool submitted;
static struct peer_svc_info*nan_find_peer_svc(uint8_t own,uint8_t id,const uint8_t*mac){assert(depth==1&&!own&&id==3&&MACADDR_EQUAL(mac,peer));return &peer_service;}
static esp_err_t esp_wifi_get_mac(int iface,uint8_t*mac){assert(iface==WIFI_IF_NAN);memcpy(mac,peer,6);return ESP_OK;}
static bool ndl_limit_reached(void){return false;}
static void os_event_group_clear_bits(void*g,unsigned bits){assert(g==nan_event_group);event_bits&=~bits;}
static esp_err_t esp_nan_internal_datapath_req(wifi_nan_datapath_req_t*req,uint8_t*id,uint8_t*ipv6){assert(!depth&&req&&ipv6);*id=7;submitted=true;return ESP_OK;}
static void nan_ndl_finalize_ndp_id(uint8_t id,uint8_t pub,uint8_t*mac,uint32_t caps){assert(depth==1&&id==7&&pub==3&&MACADDR_EQUAL(mac,peer)&&!caps);live_ndl=true;}
static EventBits_t os_event_group_wait_bits(void*g,unsigned bits,int clear,int all,unsigned wait){assert(!depth&&g&&bits==(NDP_ACCEPTED|NDP_REJECTED)&&!clear&&!all&&wait);return 0;}
static void nan_ndl_release(uint8_t id){nan_reset_ndl(id,false);}
''' + CONFIRM_STUB
