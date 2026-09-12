"""Deferred production EAP init/task/post/deinit scheduling and native snapshot.

The scheduler yields only at an empty queue wait or task exit, running the
actual SDK worker body between boundaries. No replacement EAP state machine.
Allocator, task, queue, semaphore and crypto-storage boundaries are injected.
"""
import os
from pathlib import Path
import sys
import unittest
from test_wireless_control_regression import compile_run
from test_wifi_rx_target import ROOT, INTERNAL, unit
from wireless_vm_fixture import extract
sys.path.insert(0, str(ROOT / 'scripts'))
from patch_idf_eap_control import patch_source


class IDFEAPLifecycle(unittest.TestCase):
    def test_production_worker_exit_before_storage_and_retry(self):
        if not os.environ.get('IDF_PATH'):
            self.skipTest('Set IDF_PATH to the reviewed ESP-IDF')
        component = Path(os.environ['IDF_PATH']) / 'components/wpa_supplicant'
        relative = 'esp_supplicant/src/esp_eap_client.c'
        original = (component / relative).read_bytes()
        source = patch_source(relative, original).decode()
        code = PRELUDE
        start = source.index('static void *s_wpa2_task_hdl')
        end = source.index('static void config_changed_handler', start)
        code += source[start:end]
        start = source.index('#define WPA_ADDR_LEN 6')
        end = source.index('static void wpa2_rxq_init', start)
        code += source[start:end]
        peer = (component / 'src/eap_peer/eap.c').read_text()
        code += '#define ESP_EAP_TYPE_ALL 15\n'
        code += peer[peer.index('u8 *g_wpa_anonymous_identity;'):peer.index('void eap_peer_config_deinit')]
        code += BOUNDARIES
        for name in ['wpa2_rxq_init','wpa2_rxq_enqueue','wpa2_rxq_dequeue','wpa2_rxq_remove','wpa2_rxq_deinit',
                     'wpa2_task','wpa2_post']:
            code += extract(source, name)
        start = source.index('static inline esp_err_t wpa2_task_delete')
        code += source[start:source.index('\n}\n', start)+3]
        code += extract(source, 'eap_peer_sm_deinit')
        code += extract(source, 'eap_peer_sm_init')
        code += extract(source, 'eap_sm_rx_eapol')
        code += extract(source, 'eap_client_disable_fn')
        code += extract(source, 'esp32qjs_eap_native_resources')
        code += extract(source, 'esp32qjs_eap_native_cleanup_error')
        code += extract(source, 'esp32qjs_eap_native_control_error')
        code += unit(INTERNAL / 'esp32_mquickjs_wifi_eap_sdk.h')
        code += unit(ROOT / 'components/esp32_mquickjs/src/modules/wifi_enterprise/esp32_mquickjs_wifi_eap_sdk.c')
        compile_run(self, code + MAIN)

    def test_source_drift_is_rejected(self):
        if not os.environ.get('IDF_PATH'):
            self.skipTest('Set IDF_PATH to the reviewed ESP-IDF')
        relative = 'esp_supplicant/src/esp_eap_client.c'
        original = (Path(os.environ['IDF_PATH']) / 'components/wpa_supplicant' / relative).read_bytes()
        for bad in [original+b'\n', patch_source(relative, original)]:
            with self.assertRaises(ValueError):patch_source(relative, bad)


PRELUDE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <sys/queue.h>
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT 1
#define USE_WPA2_TASK 1
#define EAP_PEER_METHOD 1
#define TRUE 1
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM -2
#define ESP_ERR_INVALID_STATE -3
#define ESP_ERR_INVALID_ARG -4
#define OS_BLOCK UINT32_MAX
#define DATA_MUTEX_TAKE() os_mutex_lock(s_wpa2_data_lock)
#define DATA_MUTEX_GIVE() os_mutex_unlock(s_wpa2_data_lock)
#define WIFI_IF_STA 0
#define WPA2_TASK_STACK_SIZE 4096
#define WPA2_TASK_PRIORITY 7
#define MSG_ERROR 1
#define MSG_DEBUG 2
#define MSG_INFO 3
#define MSG_WARNING 4
#define wpa_printf(...) ((void)0)
typedef uint8_t u8;
typedef uint32_t u32;
typedef int esp_err_t;
typedef enum {WPA2_STATE_DISABLED,WPA2_STATE_ENABLED} wpa2_state_t;
enum {SIG_WPA2_START,SIG_WPA2_RX,SIG_WPA2_TASK_DEL,SIG_WPA2_MAX};
enum {WPA2_ENT_EAP_STATE_NOT_START};
typedef struct {uint32_t sig,par;} ETSEvent;
struct eap_sm {uint32_t wpa2_sig_cnt[SIG_WPA2_MAX];int finish_state,current_identifier;
    u8 ownaddr[6];void *ssl_ctx;int workaround;bool eap_process_started;};
static struct eap_sm *gEapSm;
static void *s_wpa2_data_lock;
static bool g_wpa_config_changed;
struct wpa_sm {int (*wpa_sm_eap_disable)(void);};
static struct wpa_sm gWpaSm;
static void eap_peer_sm_deinit(void);
int wpa2_post(uint32_t,uint32_t);
static void eap_start_eapol(void *a,void *b){(void)a;(void)b;}
'''

BOUNDARIES = r'''
static unsigned allocations,fail_at,live,cancelled,config_frees,global_resets,method_unregisters;
static bool queue_fail,wait_fail,in_worker,lock_held,dispatch_fail;
static int unregister_error;
static void (*worker)(void *);
static jmp_buf scheduler;
static int task_token;
static void *allocate(size_t n) {if(++allocations==fail_at)return NULL;void *p=calloc(1,n);assert(p);live++;return p;}
static void os_free(void *p){if(p){live--;free(p);}}
#define os_zalloc allocate
static void *os_recursive_mutex_create(void){return allocate(1);}
static void os_mutex_lock(void *m){assert(m && !lock_held);lock_held=true;}
static void os_mutex_unlock(void *m){assert(m && lock_held);lock_held=false;}
static void os_mutex_delete(void *m){assert(!lock_held && !esp32qjs_eap_task_started);os_free(m);}
struct queue {ETSEvent events[16];unsigned read,count;};
static void *os_queue_create(unsigned n,size_t size){assert(n==SIG_WPA2_MAX && size==sizeof(ETSEvent));return allocate(sizeof(struct queue));}
static int os_queue_send(void *p,const void *event,unsigned ticks){(void)ticks;if(queue_fail)return 0;assert(p);struct queue *q=p;assert(q->count<16);q->events[(q->read+q->count++)%16]=*(const ETSEvent *)event;return 1;}
static int os_queue_recv(void *p,void *event,unsigned ticks){assert(in_worker && p && ticks==OS_BLOCK);struct queue *q=p;if(!q->count)longjmp(scheduler,2);*(ETSEvent *)event=q->events[q->read++%16];q->count--;return 1;}
static void os_queue_delete(void *p){assert(p && (in_worker || !esp32qjs_eap_task_started));os_free(p);}
static void *os_semphr_create(unsigned max,unsigned initial){assert(max==1 && !initial);return allocate(sizeof(unsigned));}
static void os_semphr_delete(void *p){assert(!esp32qjs_eap_task_started && !in_worker);os_free(p);}
static void os_semphr_give(void *p){assert(p && *(unsigned *)p==0);*(unsigned *)p=1;}
static void run_worker(void){assert(worker && !in_worker);in_worker=true;if(!setjmp(scheduler))worker(NULL);in_worker=false;}
static int os_semphr_take(void *p,unsigned ticks){assert(p && ticks==OS_BLOCK && !in_worker);if(wait_fail){wait_fail=false;return 0;}if(!*(unsigned *)p)run_worker();assert(*(unsigned *)p==1);*(unsigned *)p=0;return 1;}
static int os_task_create(void (*fn)(void *),const char *name,unsigned stack,void *arg,unsigned priority,void **out){
    (void)name;(void)stack;(void)priority;assert(!arg && s_wpa2_queue && s_wifi_wpa2_sync_sem && esp32qjs_eap_exit_sem);
    if(++allocations==fail_at)return 0;worker=fn;*out=&task_token;return TRUE;
}
static void *os_task_get_current_task(void){return in_worker?&task_token:NULL;}
static unsigned os_task_ms_to_tick(unsigned ms){return ms;}
static void os_task_delete(void *p){assert(!p && in_worker);worker=NULL;longjmp(scheduler,1);}
static void wpa2_set_eap_state(int state){gEapSm->finish_state=state;}
static bool wpa2_is_enabled(void){return s_wpa2_state==WPA2_STATE_ENABLED;}
static int esp_wifi_get_macaddr_internal(int interface,u8 mac[6]){assert(interface==0);memset(mac,0,6);return 0;}
static int eap_peer_blob_init(struct eap_sm *sm){assert(sm);return 0;}
static int eap_peer_config_init(struct eap_sm *sm,const u8 *pw,int n){(void)pw;(void)n;assert(sm);return 0;}
static void eap_peer_config_deinit(struct eap_sm *sm){assert(sm && !esp32qjs_eap_task_started);config_frees++;}
static void eap_peer_blob_deinit(struct eap_sm *sm){assert(sm && !esp32qjs_eap_task_started);}
static void eap_deinit_prev_method(struct eap_sm *sm,const char *reason){(void)reason;assert(sm && !esp32qjs_eap_task_started);}
static void eap_sm_abort(struct eap_sm *sm){assert(sm && !esp32qjs_eap_task_started);}
static void *tls_init(void *p){assert(!p);return allocate(1);}
static void tls_deinit(void *p){assert(!esp32qjs_eap_task_started);os_free(p);}
static int wpa2_start_eapol_internal(void){assert(in_worker && gEapSm && !config_frees);return 0;}
static int eap_sm_rx_eapol_internal(u8 *a,u8 *b,u32 c,u8 *d){(void)a;(void)b;(void)c;(void)d;assert(in_worker);return 0;}
static int eloop_cancel_timeout(void (*fn)(void *,void *),void *a,void *b){assert(fn==eap_start_eapol && !a && !b && !in_worker);cancelled++;return 0;}
static int esp_wifi_unregister_wpa2_cb_internal(void){assert(!gEapSm && !esp32qjs_eap_task_started);return unregister_error;}
static int esp_eap_client_get_disable_time_check(bool *disabled){*disabled=false;return ESP_OK;}
static void eap_globals_reset(void){assert(!gEapSm && !esp32qjs_eap_task_started);global_resets++;}
static void eap_peer_unregister_methods(void){method_unregisters++;}
bool current_task_is_wifi_task(void){return false;}
static int eloop_register_timeout_blocking(int (*fn)(void *,void *),void *a,void *b){return dispatch_fail?-1:fn(a,b);}
'''

MAIN = r'''
static void assert_empty(void){assert(!gEapSm && !worker && !live && !(esp32qjs_eap_native_resources() & ~ESP32_MQUICKJS_WIFI_EAP_SDK_DRIVER_UNKNOWN) && !esp32qjs_eap_cleanup_error && !lock_held);}
static void init(void){allocations=fail_at=config_frees=0;assert(eap_peer_sm_init()==ESP_OK);assert(gEapSm && worker && esp32qjs_eap_task_started);}
int main(void) {
    unsigned count;
    init();count=allocations;
    assert(wpa2_post(SIG_WPA2_MAX,0)==ESP_ERR_INVALID_STATE);
    in_worker=true;assert(wpa2_task_delete(NULL)==ESP_ERR_INVALID_STATE);in_worker=false;
    eap_peer_sm_deinit();assert_empty();
    for(unsigned nth=1;nth<=count;nth++) {
        allocations=config_frees=0;fail_at=nth;
        assert(eap_peer_sm_init()!=ESP_OK);assert_empty();
    }
    /* Queue rejection keeps SM, crypto and global credentials for retry. */
    init();struct eap_sm *old=gEapSm;unsigned owners=live;queue_fail=true;
    unsigned old_resets=global_resets;
    assert(eap_client_disable_fn(NULL)!=ESP_OK);
    assert(gEapSm==old && live==owners && !config_frees && esp32qjs_eap_retiring && esp32qjs_eap_cleanup_error!=ESP_OK && global_resets==old_resets);
    assert(eap_peer_sm_init()==ESP_ERR_INVALID_STATE && gEapSm==old && live==owners);
    u8 packet[6]={0};assert(eap_sm_rx_eapol(packet,packet,6,packet)!=ESP_OK && live==owners);
    queue_fail=false;assert(eap_client_disable_fn(NULL)==ESP_OK);assert_empty();
    /* A stale shared RX ack is not a worker-exit ack. */
    init();*(unsigned *)s_wifi_wpa2_sync_sem=1;eap_peer_sm_deinit();assert_empty();
    /* If waiting fails, later task completion alone still requires consuming
     * the dedicated ack before deleting semaphores or SM storage. */
    init();old=gEapSm;wait_fail=true;eap_peer_sm_deinit();
    assert(gEapSm==old && esp32qjs_eap_task_started && !config_frees);
    run_worker();assert(!worker && !s_wpa2_task_hdl && esp32qjs_eap_task_started && *(unsigned *)esp32qjs_eap_exit_sem==1);
    eap_peer_sm_deinit();assert_empty();
    /* Unregister failure does not reset borrowed global credentials. */
    init();old_resets=global_resets;unregister_error=123;esp32qjs_eap_callbacks=true;
    assert(eap_client_disable_fn(NULL)==123 && !gEapSm && global_resets==old_resets);
    unregister_error=0;assert(eap_client_disable_fn(NULL)==0);assert_empty();
    esp32_mquickjs_wifi_eap_sdk_snapshot_t snapshot;
    dispatch_fail=true;assert(esp32_mquickjs_wifi_eap_sdk_snapshot(&snapshot)!=ESP_OK && !snapshot.entered && snapshot.resources==UINT32_MAX);
    dispatch_fail=false;assert(esp32_mquickjs_wifi_eap_sdk_snapshot(&snapshot)==ESP_OK && snapshot.entered && !snapshot.resources);
    init();assert(esp32_mquickjs_wifi_eap_sdk_snapshot(&snapshot)==ESP_OK && (snapshot.resources&ESP32_MQUICKJS_WIFI_EAP_SDK_EXIT_PENDING));
    eap_peer_sm_deinit();assert_empty();return 0;
}
'''
