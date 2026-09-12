"""Deferred production SmartConfig ACK regressions; execute in Wi-Fi phase tests.

Compiles the complete patched SDK translation unit with controllable task,
allocator, socket and event boundaries. No independent lifecycle model.
"""
import os
from pathlib import Path
import re
import sys
import unittest

from test_wireless_control_regression import compile_run

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from patch_idf_smartconfig import patch_source, patch_adapter, patch_decoder_null_stores, function


class SmartConfigNative(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not os.environ.get('IDF_PATH'):
            raise unittest.SkipTest('IDF_PATH must name the reviewed SDK')
        cls.sdk = Path(os.environ['IDF_PATH']) / 'components/esp_wifi'

    def test_drift_and_double_patch_rejected(self):
        for name in ('src/smartconfig.c', 'src/smartconfig_ack.c'):
            source = (self.sdk / name).read_bytes()
            for changed in (source + b'\n', patch_source(name, source)):
                with self.subTest(name=name), self.assertRaises(ValueError):
                    patch_source(name, changed)

    def test_no_credential_log_or_temporary_copy(self):
        name = 'src/smartconfig.c'
        original = (self.sdk / name).read_bytes()
        self.assertIn(b'"PASSWORD:%s"', original)
        patched = patch_source(name, original)
        self.assertNotIn(b'evt->password', patched)
        self.assertNotIn(b'evt->ssid', patched)
        self.assertNotIn(b'"PASSWORD:%s"', patched)

    def test_production_ack_task_and_control(self):
        name = 'src/smartconfig_ack.c'
        source = patch_source(name, (self.sdk / name).read_bytes()).decode()
        source = re.sub(r'^#include[^\n]*\n', '', source, flags=re.M)
        compile_run(self, BOUNDARIES + source + CASES)

    def test_decoder_adapter_preserves_shared_table_and_rejects_drift(self):
        for target in ('esp32c3', 'esp32s3', 'esp32c5'):
            original = (self.sdk / target / 'esp_adapter.c').read_bytes()
            patched = patch_adapter(target, original)
            self.assertTrue(patched.startswith(original))
            self.assertEqual(patched.count(b'._free = free,'), 1)
            self.assertEqual(patched.count(b'._free = esp32qjs_smartconfig_secure_free,'), 1)
            self.assertEqual(patched.count(b'._wifi_calloc = wifi_calloc,'), 1)
            self.assertEqual(patched.count(b'._wifi_calloc = esp32qjs_smartconfig_calloc,'), 1)
            self.assertIn(b'static const wifi_osi_funcs_t s_esp32qjs_smartconfig_osi', patched)
            self.assertIn(b'const wifi_osi_funcs_t *const esp32qjs_smartconfig_osi', patched)
            for invalid in (original + b'\n', patched):
                with self.assertRaises(ValueError):
                    patch_adapter(target, invalid)

    def test_decoder_full_allocation_erased_before_free(self):
        # Production body from the generated adapter, not a second implementation.
        patched = patch_adapter('esp32c3', (self.sdk / 'esp32c3/esp_adapter.c').read_bytes()).decode()
        body = function(patched, 'esp32qjs_smartconfig_secure_free')
        compile_run(self, SECRET_FREE_BOUNDARIES + body + SECRET_FREE_CASES)

    def test_reviewed_decoder_null_stores_only_and_drift_rejection(self):
        # This calls the build's production transformer on real SDK archives.
        # It is structural evidence; target OOM execution remains a separate gate.
        for target in ('esp32c3', 'esp32s3', 'esp32c5'):
            original = (self.sdk / 'lib' / target / 'libsmartconfig.a').read_bytes()
            patched = patch_decoder_null_stores(target, original)
            self.assertEqual(len(original), len(patched))
            differences = [i for i, (a, b) in enumerate(zip(original, patched)) if a != b]
            self.assertTrue(differences)
            self.assertLessEqual(len(differences), 8)
            for invalid in (original + b'\n', patched):
                with self.assertRaises(ValueError):
                    patch_decoder_null_stores(target, invalid)

    def test_production_allocator_reports_only_nonempty_failure(self):
        patched = patch_adapter('esp32c3', (self.sdk / 'esp32c3/esp_adapter.c').read_bytes()).decode()
        body = function(patched, 'esp32qjs_smartconfig_calloc')
        compile_run(self, OBSERVED_CALLOC_BOUNDARIES + body + OBSERVED_CALLOC_CASES)


OBSERVED_CALLOC_BOUNDARIES = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
static char allocation;
static void *next_result;
static size_t seen_count,seen_size;
static int calls,failures;
static void *wifi_calloc(size_t count,size_t size) {
    calls++;seen_count=count;seen_size=size;return next_result;
}
static void esp32_mquickjs_wifi_smartconfig_allocation_failed(void) { failures++; }
'''

OBSERVED_CALLOC_CASES = r'''
int main(void) {
    next_result=&allocation;
    assert(esp32qjs_smartconfig_calloc(92,1)==&allocation);
    assert(calls==1 && failures==0 && seen_count==92 && seen_size==1);
    next_result=NULL;
    assert(esp32qjs_smartconfig_calloc(2824,1)==NULL);
    assert(calls==2 && failures==1 && seen_count==2824 && seen_size==1);
    assert(esp32qjs_smartconfig_calloc(SIZE_MAX,2)==NULL);
    assert(calls==3 && failures==2 && seen_count==SIZE_MAX && seen_size==2);
    assert(esp32qjs_smartconfig_calloc(0,1)==NULL);
    assert(esp32qjs_smartconfig_calloc(1,0)==NULL);
    assert(calls==5 && failures==2);
    return 0;
}
'''


SECRET_FREE_BOUNDARIES = r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
static size_t live_size;
static uint8_t *live;
static void *psni_info;
static int unrelated_owner;
static int queries, scrubs, releases;
static void release_backing(void *p) { free(p); }
static size_t heap_caps_get_allocated_size(void *p) {
    assert(p && p==live);queries++;return live_size;
}
static void esp32_mquickjs_wireless_secure_zero(void *p,size_t length) {
    assert(p==live && length==live_size);scrubs++;
    volatile uint8_t *v=p;while(length--)*v++=0;
}
static void observed_free(void *p) {
    releases++;
    if(!p)return;
    assert(p==live);
    assert(psni_info!=p); /* Revocation precedes the allocator's actual free. */
    for(size_t i=0;i<live_size;i++)assert(live[i]==0);
    /* Adjacent storage/canaries are outside the allocator's reported span. */
    assert(live[-1]==0x71 && live[live_size]==0x72);
}
#define free observed_free
'''

SECRET_FREE_CASES = r'''
int main(void) {
    /* SDK v2 packed buffer, legacy decoder, AirKiss, and small list records. */
    const size_t sizes[]={2824,420,216,16,1};
    for(size_t i=0;i<sizeof(sizes)/sizeof(sizes[0]);i++) {
        live_size=sizes[i];uint8_t *allocation=malloc(live_size+2);assert(allocation);
        live=allocation+1;memset(live,0xa5,live_size);
        live[-1]=0x71;live[live_size]=0x72;
        psni_info=live_size==16 ? live : (void *)&unrelated_owner;
        esp32qjs_smartconfig_secure_free(live);
        assert(psni_info==(live_size==16 ? NULL : (void *)&unrelated_owner));
        release_backing(allocation);live=NULL;
    }
    assert(queries==5 && scrubs==5 && releases==5);
    esp32qjs_smartconfig_secure_free(NULL);
    assert(queries==5 && scrubs==5 && releases==6);
    assert(psni_info==&unrelated_owner);
    return 0;
}
'''

BOUNDARIES = r'''
#define _DEFAULT_SOURCE
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
typedef int esp_err_t;
typedef unsigned TickType_t;
typedef int portMUX_TYPE;
typedef enum {SC_TYPE_ESPTOUCH,SC_TYPE_AIRKISS,SC_TYPE_ESPTOUCH_AIRKISS,SC_TYPE_ESPTOUCH_V2} smartconfig_type_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 2
#define ESP_ERR_INVALID_STATE 3
#define ESP_ERR_NO_MEM 4
#define ESP_ERR_TIMEOUT 5
#define portMUX_INITIALIZER_UNLOCKED 0
#define pdPASS 1
#define portTICK_PERIOD_MS 1
#define WIFI_IF_STA 0
#define SC_EVENT 6
#define SC_EVENT_SEND_ACK_DONE 7
#define LWIP_SOCKET_OFFSET 0
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
static int locked;
#define portENTER_CRITICAL(p) do { (void)(p);assert(!locked);locked=1; } while(0)
#define portEXIT_CRITICAL(p) do { (void)(p);assert(locked);locked=0; } while(0)
typedef struct { int unused; } esp_netif_t;
typedef struct { struct { uint32_t addr; } ip; } esp_netif_ip_info_t;
static esp_netif_t netif;
static uint8_t phone[4]={192,168,1,2};
static void (*scheduled)(void *);
static void *scheduled_arg;
static int creates,deletes,posts,sends,closes,delay_count,stop_on_delay;
static int queue_error,create_error,mac_error,socket_error,send_error;
static bool immediate,missing_netif,allocation_error;
static struct { void *p;size_t size; } allocations[4];
esp_err_t sc_send_ack_start(smartconfig_type_t,uint8_t,uint8_t *);
void sc_send_ack_stop(void);
uint64_t esp32qjs_smartconfig_ack_status(bool *,bool *,bool *,esp_err_t *,esp_err_t *,int *);
static void *test_calloc(size_t n,size_t size) {
    assert(!locked);if(allocation_error)return NULL;
    void *p=calloc(n,size);assert(p);
    for(int i=0;i<4;i++)if(!allocations[i].p){allocations[i].p=p;allocations[i].size=n*size;return p;}
    assert(0);return NULL;
}
static void test_free(void *p) {
    assert(!locked);
    for(int i=0;i<4;i++)if(allocations[i].p==p){
        for(size_t j=0;j<allocations[i].size;j++)assert(((uint8_t *)p)[j]==0);
        allocations[i].p=NULL;free(p);return;
    }
    assert(0);
}
void esp32_mquickjs_wireless_secure_zero(void *p,size_t size) {
    volatile uint8_t *v=p;while(size--)*v++=0;
}
static int xTaskCreate(void (*fn)(void *),const char *name,unsigned stack,void *arg,int priority,void *out) {
    assert(!locked && !scheduled && name && stack==2048 && priority==2 && !out);creates++;
    if(create_error)return 0;
    if(immediate)fn(arg);else {scheduled=fn;scheduled_arg=arg;}
    return pdPASS;
}
static void vTaskDelete(void *task) {assert(!locked && !task);deletes++;}
static void vTaskDelay(unsigned ticks) {
    assert(!locked && ticks);delay_count++;
    if(stop_on_delay && delay_count==stop_on_delay){
        sc_send_ack_stop();
        /* The old task still owns its socket/argument, even after STOP. */
        assert(sc_send_ack_start(SC_TYPE_ESPTOUCH,2,phone)==ESP_ERR_INVALID_STATE);
    }
}
static esp_err_t esp_wifi_get_mac(int interface,uint8_t *mac) {
    assert(!locked && interface==0);memset(mac,0x12,6);return mac_error;
}
static esp_netif_t *esp_netif_get_handle_from_ifkey(const char *key) {
    assert(!locked && !strcmp(key,"WIFI_STA_DEF"));return missing_netif?NULL:&netif;
}
static esp_err_t esp_netif_get_ip_info(esp_netif_t *n,esp_netif_ip_info_t *ip) {
    assert(!locked && n==&netif);ip->ip.addr=1;return ESP_OK;
}
static int test_socket(int af,int type,int protocol) {
    assert(!locked && af==AF_INET && type==SOCK_DGRAM && !protocol);
    if(socket_error){errno=ENOBUFS;return -1;}return 9;
}
static int test_setsockopt(int fd,int level,int option,const void *value,socklen_t len) {
    assert(!locked && fd==9 && level==SOL_SOCKET && option && value && len);return 0;
}
static int test_bind(int fd,const struct sockaddr *addr,socklen_t size) {
    assert(!locked && fd==9 && addr && size);return 0;
}
static int test_recvfrom(int fd,void *data,size_t size,int flags,struct sockaddr *addr,socklen_t *len) {
    assert(!locked && fd==9 && size==1 && !flags && len);*(char *)data=0;
    ((struct sockaddr_in *)addr)->sin_addr.s_addr=2;return 1;
}
static int test_sendto(int fd,const void *data,size_t len,int flags,const struct sockaddr *addr,socklen_t size) {
    assert(!locked && fd==9 && data && (len==7 || len==11) && flags==MSG_DONTWAIT && addr && size);
    sends++;if(send_error){errno=EIO;return -1;}return (int)len;
}
static int test_close(int fd) {assert(!locked && fd==9);closes++;return 0;}
static esp_err_t esp_event_post(int base,int event,const void *data,size_t len,unsigned wait) {
    assert(!locked && base==SC_EVENT && event==SC_EVENT_SEND_ACK_DONE && !data && !len && !wait);
    bool busy,completed;
    assert(esp32qjs_smartconfig_ack_status(&busy,NULL,&completed,NULL,NULL,NULL));
    assert(busy && completed);posts++;return queue_error;
}
#define calloc test_calloc
#define free test_free
#define socket test_socket
#define setsockopt test_setsockopt
#define bind test_bind
#define recvfrom test_recvfrom
#define sendto test_sendto
#define close test_close
'''

CASES = r'''
static void run_task(void) {
    assert(scheduled);void (*fn)(void *)=scheduled;void *arg=scheduled_arg;
    scheduled=NULL;scheduled_arg=NULL;fn(arg);
    for(int i=0;i<4;i++)assert(!allocations[i].p);
}
static void reset_boundaries(void) {
    assert(!scheduled && !s_sc_ack.busy);creates=deletes=posts=sends=closes=delay_count=stop_on_delay=0;
    queue_error=create_error=mac_error=socket_error=send_error=0;
    immediate=missing_netif=allocation_error=false;
}
int main(void) {
    assert(sc_send_ack_start(SC_TYPE_ESPTOUCH,1,NULL)==ESP_ERR_INVALID_ARG);
    assert(sc_send_ack_start(SC_TYPE_ESPTOUCH_AIRKISS,1,phone)==ESP_ERR_INVALID_ARG);
    allocation_error=true;
    assert(sc_send_ack_start(SC_TYPE_ESPTOUCH,1,phone)==ESP_ERR_NO_MEM && !s_sc_ack.identity);
    reset_boundaries();create_error=true;
    assert(sc_send_ack_start(SC_TYPE_ESPTOUCH,1,phone)==ESP_ERR_NO_MEM);
    assert(!s_sc_ack.busy && s_sc_ack.error==ESP_ERR_NO_MEM && s_sc_ack.identity==1);

    reset_boundaries();
    assert(sc_send_ack_start(SC_TYPE_ESPTOUCH,1,phone)==ESP_OK);
    uint64_t old=s_sc_ack.identity;
    sc_send_ack_stop();sc_send_ack_stop();
    assert(s_sc_ack.busy && !sc_ack_allowed(old));
    assert(sc_send_ack_start(SC_TYPE_ESPTOUCH,1,phone)==ESP_ERR_INVALID_STATE);
    run_task();assert(!s_sc_ack.busy && !posts && !sends && !closes);
    assert(sc_send_ack_start(SC_TYPE_ESPTOUCH_V2,3,phone)==ESP_OK && s_sc_ack.identity>old);
    sc_ack_finish(old,ESP_FAIL,123);sc_ack_complete(old);
    assert(s_sc_ack.busy && !s_sc_ack.completed && !posts);
    queue_error=ESP_ERR_TIMEOUT;run_task();
    assert(!s_sc_ack.busy && s_sc_ack.completed && s_sc_ack.error==ESP_OK);
    assert(s_sc_ack.observation_error==ESP_ERR_TIMEOUT && posts==1 && sends==30 && closes==1);

    reset_boundaries();immediate=true;
    assert(sc_send_ack_start(SC_TYPE_AIRKISS,1,phone)==ESP_OK);
    assert(!s_sc_ack.busy && s_sc_ack.completed && deletes==1 && sends==60);
    reset_boundaries();mac_error=ESP_FAIL;
    assert(sc_send_ack_start(SC_TYPE_ESPTOUCH,1,phone)==ESP_OK);run_task();
    assert(s_sc_ack.error==ESP_FAIL && !s_sc_ack.completed && !closes);
    reset_boundaries();socket_error=true;
    assert(sc_send_ack_start(SC_TYPE_ESPTOUCH,1,phone)==ESP_OK);run_task();
    assert(s_sc_ack.error==ESP_FAIL && s_sc_ack.socket_errno==ENOBUFS && !posts && !closes);
    reset_boundaries();send_error=true;
    assert(sc_send_ack_start(SC_TYPE_ESPTOUCH,1,phone)==ESP_OK);run_task();
    assert(s_sc_ack.error==ESP_FAIL && s_sc_ack.socket_errno==EIO && !posts && closes==1);
    reset_boundaries();stop_on_delay=2;
    assert(sc_send_ack_start(SC_TYPE_ESPTOUCH,1,phone)==ESP_OK);run_task();
    assert(!s_sc_ack.busy && s_sc_ack.stopping && !sends && !posts && closes==1);
    reset_boundaries();missing_netif=true;stop_on_delay=3;
    assert(sc_send_ack_start(SC_TYPE_ESPTOUCH,1,phone)==ESP_OK);run_task();
    assert(!s_sc_ack.busy && !sends && !closes);
    reset_boundaries();
    assert(esp32qjs_smartconfig_ack_reserve(21)==ESP_OK);
    assert(esp32qjs_smartconfig_ack_reserve(22)==ESP_ERR_INVALID_STATE);
    assert(sc_send_ack_start(SC_TYPE_ESPTOUCH,1,phone)==ESP_ERR_INVALID_STATE);
    uint64_t receipt=0;
    assert(esp32qjs_smartconfig_ack_start(22,SC_TYPE_ESPTOUCH,1,phone,&receipt)==ESP_ERR_INVALID_STATE && !receipt);
    assert(esp32qjs_smartconfig_ack_start(21,SC_TYPE_ESPTOUCH,1,phone,&receipt)==ESP_OK && receipt);
    sc_send_ack_stop();assert(!s_sc_ack.stopping); /* Foreign legacy stop is not consent. */
    assert(esp32qjs_smartconfig_ack_release(21)==ESP_ERR_INVALID_STATE);
    assert(esp32qjs_smartconfig_ack_stop(21,receipt-1)==ESP_ERR_INVALID_STATE && !s_sc_ack.stopping);
    assert(esp32qjs_smartconfig_ack_stop(22,receipt)==ESP_ERR_INVALID_STATE);
    assert(esp32qjs_smartconfig_ack_stop(21,receipt)==ESP_OK);
    run_task();assert(!s_sc_ack.busy && !sends);
    assert(sc_send_ack_start(SC_TYPE_ESPTOUCH,1,phone)==ESP_ERR_INVALID_STATE);
    assert(esp32qjs_smartconfig_ack_release(22)==ESP_ERR_INVALID_STATE);
    assert(esp32qjs_smartconfig_ack_release(21)==ESP_OK);
    reset_boundaries();create_error=true;receipt=0;
    assert(esp32qjs_smartconfig_ack_reserve(23)==ESP_OK);
    assert(esp32qjs_smartconfig_ack_start(23,SC_TYPE_ESPTOUCH,1,phone,&receipt)==ESP_ERR_NO_MEM && receipt);
    assert(esp32qjs_smartconfig_ack_start(23,SC_TYPE_ESPTOUCH,1,phone,&receipt)==ESP_ERR_INVALID_ARG);
    assert(esp32qjs_smartconfig_ack_stop(23,receipt)==ESP_OK);
    assert(esp32qjs_smartconfig_ack_release(23)==ESP_OK);
    reset_boundaries();s_sc_ack.identity=UINT64_MAX;
    assert(sc_send_ack_start(SC_TYPE_ESPTOUCH,1,phone)==ESP_ERR_NO_MEM && !s_sc_ack.busy && !creates);
    return 0;
}
'''
