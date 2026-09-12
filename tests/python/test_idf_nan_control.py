"""Production NAN SDK/observer cases. Deferred; no fixture execution on import."""
import importlib.util
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from wireless_vm_fixture import extract

ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / 'components/esp32_mquickjs'
SDK = Path('/home/zach/esp/esp-idf/components/esp_wifi')


class NanNativeControl(unittest.TestCase):
    def sources(self):
        path = SDK / 'wifi_apps/nan_app/src/nan_app.c'
        if not path.exists():
            self.skipTest('reviewed SDK unavailable')
        spec = importlib.util.spec_from_file_location('nan_patch', ROOT / 'scripts/patch_idf_nan.py')
        patch = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(patch)
        original = path.read_text()
        return original, patch.patch_source(original)

    def compile_run(self, source, *, expect_failure=False):
        cc = shutil.which('cc')
        if cc is None:
            self.skipTest('C compiler unavailable')
        with tempfile.TemporaryDirectory() as folder:
            path, binary = Path(folder) / 'case.c', Path(folder) / 'case'
            path.write_text(source)
            result = subprocess.run([cc, '-std=c11', '-pthread', '-Wall', '-Wextra', '-Werror',
                '-Wno-unused-function', '-Wno-unused-variable', '-I' + str(BASE / 'internal'),
                '-I' + str(ROOT / 'tests/c/radio_stubs'), str(path), '-o', str(binary)],
                capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
            if expect_failure:
                self.assertNotEqual(result.returncode, 0, 'original path unexpectedly satisfied regression')
            else:
                self.assertEqual(result.returncode, 0, result.stderr)

    def observer(self):
        source = (BASE / 'src/modules/wifi_nan/esp32_mquickjs_wifi_nan_sdk.inc').read_text()
        source = source.replace('#include "freertos/FreeRTOS.h"', '').replace('#include "freertos/portmacro.h"', '')
        # Queries depend on the SDK service database, absent from this observer
        # fixture. Their complete production include runs in test_wifi_nan_query.
        source = source.replace('#include "esp32_mquickjs_wifi_nan_query_sdk.inc"', '')
        # Load common bridge declarations without the SDK request prototypes;
        # each case supplies its own native request boundary below. Enable the
        # real service callback scopes in the implementation being exercised.
        headers = '\n'.join(line for line in source.splitlines()
                            if line.startswith('#include "esp32_mquickjs_'))
        return PORT_BOUNDARY + headers + '\n#define CONFIG_ESP_WIFI_NAN_SYNC_ENABLE 1\n' + source

    def test_actual_observer_retirement_race_stale_identity_and_exhaustion(self):
        self.compile_run(self.observer() + OBSERVER_MAIN)

    def test_actual_service_callback_remains_owned_after_notice_until_free_returns(self):
        original, prepared = self.sources()
        for source, before in ((original, True), (prepared, False)):
            self.compile_run(self.observer() + SERVICE_CALLBACK_BOUNDARY +
                extract(source, 'nan_app_replied_cb') + SERVICE_CALLBACK_MAIN, expect_failure=before)

    def test_actual_native_tx_scope_survives_application_callback_return(self):
        _, prepared = self.sources()
        bridge = (BASE / 'src/modules/wifi_nan/esp32_mquickjs_wifi_nan_sdk.inc').read_text()
        # The native boundary models the pinned archive's post-app peer write;
        # all admission/quiescence and both wrapper/app scopes are production.
        self.compile_run(self.observer() + SERVICE_CALLBACK_BOUNDARY +
            extract(prepared, 'nan_app_replied_cb') +
            '\nextern void __real_nan_sdf_txcb(void *buffer);\n' +
            extract(bridge, '__wrap_nan_sdf_txcb') + NATIVE_TX_SCOPE_MAIN)

    def test_actual_publish_and_subscribe_release_data_lock_during_driver_call(self):
        original, prepared = self.sources()
        for kind in ('publish', 'subscribe'):
            sdk_name = 'esp_wifi_nan_' + kind + '_service'
            checked = 'esp32_mquickjs_wifi_nan_sdk_' + kind
            for source, before in ((original, True), (prepared, False)):
                body = extract(source, sdk_name) if before else (
                    extract(source, 'esp32qjs_nan_' + kind + '_service') + extract(source, checked))
                main = SERVICE_START_MAIN.replace('CONFIG_TYPE', 'wifi_nan_' + kind + '_cfg_t')
                main = main.replace('CHECKED_CALL', checked)
                if before:
                    main = 'int main(void){wifi_nan_' + kind + '_cfg_t config={.service_name="test"};' + sdk_name + '(&config);return 0;}'
                self.compile_run(SERVICE_START_BOUNDARY + body + main, expect_failure=before)

    def test_actual_cancel_freezes_before_driver_and_preserves_failed_service(self):
        original, prepared = self.sources()
        for source, before in ((original, True), (prepared, False)):
            body = extract(source, 'esp_wifi_nan_cancel_service')
            if not before:
                body += extract(source, 'esp32_mquickjs_wifi_nan_sdk_cancel_service')
            main = 'int main(void){esp_wifi_nan_cancel_service(7);return 0;}' if before else SERVICE_CANCEL_MAIN
            self.compile_run(self.observer() + SERVICE_CANCEL_BOUNDARY + body + main, expect_failure=before)

    def test_actual_native_create_wrapper_binds_host_policy_before_observer(self):
        _, prepared = self.sources()
        self.compile_run(self.observer() + SERVICE_BIND_BOUNDARY +
            extract(prepared, '__wrap_nan_start_publish_service') +
            extract(prepared, '__wrap_nan_start_subscribe_service') + SERVICE_BIND_MAIN)

    def test_actual_message_submit_requires_exact_peer_and_preserves_native_error(self):
        _, prepared = self.sources()
        self.compile_run(MESSAGE_SUBMIT_BOUNDARY + extract(prepared, 'esp32_mquickjs_wifi_nan_sdk_send') + r'''
int main(void){
 uint32_t context=0;wifi_nan_followup_params_t p={.inst_id=7,.peer_inst_id=3,.peer_mac={2,3,4,5,6,7}};
 assert(esp32_mquickjs_wifi_nan_sdk_send(&p,&context)==ESP_ERR_NOT_FOUND&&!calls);
 peer_exists=true;native_error=-71;assert(esp32_mquickjs_wifi_nan_sdk_send(&p,&context)==-71&&calls==1&&!depth);
 native_error=0;assert(!esp32_mquickjs_wifi_nan_sdk_send(&p,&context)&&context==1234&&calls==2);
 assert(esp32_mquickjs_wifi_nan_sdk_send(&p,&context)==ESP_ERR_INVALID_ARG&&calls==2);
 context=0;p.peer_inst_id=0;assert(esp32_mquickjs_wifi_nan_sdk_send(&p,&context)==ESP_ERR_INVALID_ARG&&calls==2);
 p.peer_inst_id=3;p.peer_mac[0]=1;assert(esp32_mquickjs_wifi_nan_sdk_send(&p,&context)==ESP_ERR_INVALID_ARG);
 p.peer_mac[0]=2;p.ssi_len=2049;assert(esp32_mquickjs_wifi_nan_sdk_send(&p,&context)==ESP_ERR_INVALID_ARG);
 p.ssi_len=0;uint8_t body[255]={7};nan_vendor_ie_t vendor={.body_len=256,.body=body};p.vendor_ie=&vendor;
 assert(esp32_mquickjs_wifi_nan_sdk_send(&p,&context)==ESP_ERR_INVALID_ARG&&calls==2);
 vendor.body_len=255;vendor.body=NULL;
 assert(esp32_mquickjs_wifi_nan_sdk_send(&p,&context)==ESP_ERR_INVALID_ARG&&calls==2);
 vendor.body=body;
 assert(!esp32_mquickjs_wifi_nan_sdk_send(&p,&context)&&context==1234&&calls==3);
 assert(!depth&&p.vendor_ie==&vendor&&vendor.body[0]==7);
}
''')

    def test_actual_ndp_response_mac_failure_unlocks_once_and_preserves_error(self):
        original, prepared = self.sources()
        for source, before in ((original, True), (prepared, False)):
            self.compile_run(RESPONSE_BOUNDARY + extract(source, 'esp_wifi_nan_datapath_resp') + r'''
int main(void){wifi_nan_datapath_resp_t request={.ndp_id=1};
    assert(esp_wifi_nan_datapath_resp(&request)==-72);assert(!depth&&!submits);}
''', expect_failure=before)

    def test_actual_termination_completes_before_event_allocation_failure(self):
        original, prepared = self.sources()
        for source, before in ((original, True), (prepared, False)):
            self.compile_run(self.observer() + TERMINATION_BOUNDARY +
                extract(source, 'nan_app_ndp_terminated_cb') + TERMINATION_MAIN,
                expect_failure=before)

    def test_actual_start_timeout_preserves_context_and_bounds_wait(self):
        original, prepared = self.sources()
        for source, before in ((original, True), (prepared, False)):
            prepare = '' if before else extract(source, 'esp32_mquickjs_wifi_nan_sdk_sync_prepare')
            self.compile_run(self.observer() + START_BOUNDARY +
                prepare + extract(source, 'esp_wifi_nan_sync_start') + START_MAIN,
                expect_failure=before)

    def test_ram_only_start_never_loads_persisted_nan_identity(self):
        original, prepared = self.sources()
        boundary = START_BOUNDARY.replace('typedef struct{int channel;}wifi_nan_sync_config_t;',
            'typedef struct{int channel;bool use_nvs_for_caching,reset_current_nvs_creds,group_mgmt_prot;}wifi_nan_sync_config_t;')
        boundary = boundary.replace('static struct{unsigned state;void *nan_netif;}s_nan_ctx',
            'static struct{unsigned state;void *nan_netif;bool own_nik_valid,use_nvs_for_caching,group_mgmt_prot,nira_cached;'
            'unsigned num_peer_creds,nik_lifetime;uint8_t own_nik[16],peer_creds[64];}s_nan_ctx')
        boundary += r'''
#define CONFIG_ESP_WIFI_NAN_SECURITY 1
#define ESP_WIFI_NAN_NIK_LEN 16
#define ESP_LOGW(...) ((void)0)
static unsigned credential_loads,credential_erases,nik_saves;
static int esp_wifi_nan_erase_all_creds(void){++credential_erases;return 0;}
static int esp_wifi_nan_load_saved_creds(void*nik,bool*valid,void*peers,unsigned*count){
 (void)peers;++credential_loads;memset(nik,0x41,16);*valid=true;*count=1;return 0;}
static int os_get_random(void*nik,unsigned n){memset(nik,0x52,n);return 0;}
static int esp_wifi_nan_save_own_nik(void*nik){assert(nik);++nik_saves;return 0;}
'''
        main = r'''
int main(void){wifi_nan_sync_config_t config={.channel=6};
 (void)esp_wifi_nan_sync_start(&config);
 assert(!credential_loads&&!credential_erases&&!nik_saves&&s_nan_ctx.own_nik[0]==0x52&&!s_nan_ctx.num_peer_creds);
 config.use_nvs_for_caching=true;(void)esp_wifi_nan_sync_start(&config);
 assert(credential_loads==1&&s_nan_ctx.own_nik[0]==0x41&&!credential_erases);}
'''
        for source, before in ((original, True), (prepared, False)):
            prepare = '' if before else extract(source, 'esp32_mquickjs_wifi_nan_sdk_sync_prepare')
            self.compile_run(self.observer() + boundary + prepare + extract(source, 'esp_wifi_nan_sync_start') + main,
                expect_failure=before)

    def test_actual_event_post_never_blocks_native_control(self):
        original, prepared = self.sources()
        for source, before in ((original, True), (prepared, False)):
            self.compile_run(self.observer() + POST_BOUNDARY +
                extract(source, 'nan_app_post_event') + POST_MAIN,
                expect_failure=before)

    def test_actual_init_failure_and_revisit_preserve_allocated_native_objects(self):
        original, prepared = self.sources()
        for source, before in ((original, True), (prepared, False)):
            self.compile_run(INIT_BOUNDARY + extract(source, 'esp_nan_app_deinit') +
                extract(source, 'esp_nan_app_init') + INIT_MAIN, expect_failure=before)

    def test_actual_handler_cleanup_preserves_error_and_retries_only_failed_unregister(self):
        original, prepared = self.sources()
        for source, before in ((original, True), (prepared, False)):
            self.compile_run(HANDLER_BOUNDARY + extract(source, 'nan_clear_app_default_handlers') + r'''
int main(void){
    assert(nan_clear_app_default_handlers()==-71&&s_app_default_handlers_set&&calls==1);
    error=0;assert(nan_clear_app_default_handlers()==ESP_OK&&!s_app_default_handlers_set&&calls==2);
    assert(nan_clear_app_default_handlers()==ESP_OK&&calls==2);
}
''', expect_failure=before)


MESSAGE_SUBMIT_BOUNDARY = r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NOT_SUPPORTED 3
#define ESP_ERR_NOT_FOUND 4
#define ESP_WIFI_MAX_FUP_SSI_LEN 2048
#define MACADDR_EQUAL(a,b) (!memcmp(a,b,6))
typedef int esp_err_t;
#define NAN_VENDOR_IE_MAX_BODY_LEN 255
typedef struct{uint8_t vendor_oui[3];uint16_t body_len;uint8_t*body;}nan_vendor_ie_t;
typedef struct{uint8_t inst_id,peer_inst_id,peer_mac[6];uint16_t ssi_len;uint8_t*ssi;nan_vendor_ie_t*vendor_ie;}wifi_nan_followup_params_t;
static unsigned depth,calls;
static int native_error;
static bool peer_exists,s_usd_in_progress;
static void*s_nan_data_lock=(void*)1,*nan_event_group=(void*)1;
static uint8_t null_mac[6];
#define NAN_DATA_LOCK() do{assert(!depth);++depth;}while(0)
#define NAN_DATA_UNLOCK() do{assert(depth==1);--depth;}while(0)
static void*nan_find_own_svc(uint8_t id){assert(depth&&id==7);return (void*)1;}
static void*nan_find_peer_svc(uint8_t id,uint8_t peer,uint8_t*mac){assert(depth&&id==7&&peer==3&&mac[0]==2);return peer_exists?(void*)1:NULL;}
static int esp_nan_internal_send_followup(wifi_nan_followup_params_t*p,uint32_t*context,void*extra){
 assert(!depth&&p&&context&&!extra);++calls;if(!native_error)*context=1234;return native_error;
}
'''

PORT_BOUNDARY = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
typedef pthread_mutex_t portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED PTHREAD_MUTEX_INITIALIZER
static _Thread_local unsigned observer_depth;
uint32_t esp32_mquickjs_wifi_nan_tx_callback_enter(void *p){(void)p;return 0;}
bool esp32_mquickjs_wifi_nan_tx_callback_allowed(uint32_t id){(void)id;return true;}
void esp32_mquickjs_wifi_nan_tx_callback_leave(uint32_t id,bool success){(void)id;(void)success;}
#define portENTER_CRITICAL(p) do{assert(!observer_depth);assert(!pthread_mutex_lock(p));++observer_depth;}while(0)
#define portEXIT_CRITICAL(p) do{assert(observer_depth==1);--observer_depth;assert(!pthread_mutex_unlock(p));}while(0)
'''
HANDLER_BOUNDARY = r'''
#include <assert.h>
#include <stdbool.h>
typedef int esp_err_t;
#define ESP_OK 0
#define IP_EVENT 1
#define IP_EVENT_GOT_IP6 2
static bool s_app_default_handlers_set=true;
static int calls,error=-71;
static void nan_app_action_got_ipv6(void){}
static int esp_event_handler_unregister(int base,int id,void(*handler)(void)){
    assert(base==1&&id==2&&handler==nan_app_action_got_ipv6);++calls;return error;
}
'''
OBSERVER_MAIN = r'''
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition=PTHREAD_COND_INITIALIZER;
static bool entered,proceed;static unsigned count;static uint32_t current;
static void receive_notice(uint32_t identity,const esp32_mquickjs_wifi_nan_sdk_notice_t *notice,void *p){
    assert(!observer_depth&&p==&count&&identity==current&&notice->ndp_id==9);
    pthread_mutex_lock(&mutex);++count;entered=true;pthread_cond_broadcast(&condition);
    while(!proceed){pthread_cond_wait(&condition,&mutex);}pthread_mutex_unlock(&mutex);
}
static void *deliver(void *p){(void)p;esp32qjs_nan_control(ESP32_MQUICKJS_NAN_SDK_NDP_TERMINATED,0,9,1,0,NULL);return NULL;}
int main(void){
    assert(esp32_mquickjs_wifi_nan_sdk_observe(receive_notice,&count,&current)==ESP_OK);
    uint32_t stale=current,next=0;pthread_t sender;assert(!pthread_create(&sender,NULL,deliver,NULL));
    pthread_mutex_lock(&mutex);while(!entered)pthread_cond_wait(&condition,&mutex);pthread_mutex_unlock(&mutex);
    assert(esp32_mquickjs_wifi_nan_sdk_unobserve(&current)==ESP_ERR_TIMEOUT&&current==stale);
    assert(esp32_mquickjs_wifi_nan_sdk_observe(receive_notice,&count,&next)==ESP_ERR_INVALID_STATE&&!next);
    deliver(NULL);assert(count==1);
    pthread_mutex_lock(&mutex);proceed=true;pthread_cond_broadcast(&condition);pthread_mutex_unlock(&mutex);
    assert(!pthread_join(sender,NULL));assert(esp32_mquickjs_wifi_nan_sdk_unobserve(&current)==ESP_OK&&!current);
    assert(esp32_mquickjs_wifi_nan_sdk_observe(receive_notice,&count,&current)==ESP_OK&&current!=stale);
    assert(esp32_mquickjs_wifi_nan_sdk_unobserve(&stale)==ESP_ERR_INVALID_STATE);
    assert(esp32_mquickjs_wifi_nan_sdk_unobserve(&current)==ESP_OK);
    s_esp32qjs_nan_observer.next_identity=UINT32_MAX;
    assert(esp32_mquickjs_wifi_nan_sdk_observe(receive_notice,&count,&current)==ESP_OK&&current==UINT32_MAX);
    assert(esp32_mquickjs_wifi_nan_sdk_unobserve(&current)==ESP_OK);
    assert(esp32_mquickjs_wifi_nan_sdk_observe(receive_notice,&count,&current)==ESP_ERR_NO_MEM&&!current);
}
'''
RESPONSE_BOUNDARY = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "esp_err.h"
typedef struct {uint8_t ndp_id,peer_mac[6];} wifi_nan_datapath_resp_t;
struct ndl_info {uint32_t device_caps;uint8_t peer_nmi[6];};
static struct ndl_info link={.device_caps=8,.peer_nmi={2,3,4,5,6,7}};
typedef struct {union{struct{uint32_t addr[4];}ip6;}u_addr;} ip_addr_t;
static unsigned depth,submits;static uint8_t null_mac[6];static struct{unsigned event;}s_nan_ctx={16};
#define NAN_DATA_LOCK() do{assert(!depth);++depth;}while(0)
#define NAN_DATA_UNLOCK() do{assert(depth==1);--depth;}while(0)
#define NAN_CAPS_NDPE_ATTR 8
#define NDP_INDICATION 16
#define WIFI_IF_NAN 3
#define MACADDR_EQUAL(a,b) (!memcmp(a,b,6))
#define MACADDR_COPY(a,b) memcpy(a,b,6)
#define ESP_LOGE(...) ((void)0)
static struct ndl_info *nan_find_ndl(uint8_t id,void *peer){(void)peer;return id==1?&link:NULL;}
static int esp_wifi_get_mac(int interface,uint8_t *mac){(void)interface;(void)mac;assert(depth==1);return -72;}
static void esp_wifi_nan_get_ipv6_linklocal_from_mac(void *out,void *mac){(void)out;(void)mac;assert(0);}
static int esp_nan_internal_datapath_resp(void *r,void *v){(void)r;(void)v;submits++;return 0;}
'''
TERMINATION_BOUNDARY = r'''
#include <stdlib.h>
static struct{void *nan_netif;unsigned event;}s_nan_ctx;
typedef struct{uint8_t reason,ndp_id,init_ndi[6];}wifi_event_ndp_terminated_t;
static unsigned bits,notifications,resets;static void *nan_event_group=(void *)1;
#define NDP_INDICATION 16
#define NDP_TERMINATED 64
#define NAN_DATA_LOCK() ((void)0)
#define NAN_DATA_UNLOCK() ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define WIFI_EVENT 1
#define WIFI_EVENT_NDP_TERMINATED 2
#define MACADDR_COPY(a,b) memcpy(a,b,6)
static bool nan_is_datapath_active(void){return false;}
static void esp_netif_action_disconnected(void*a,int b,int c,void*d){(void)a;(void)b;(void)c;(void)d;}
static void nan_reset_ndl(uint8_t id,bool all){assert(id==9&&!all);resets++;}
static void *os_zalloc(size_t n){(void)n;return NULL;}
static void os_free(void *p){free(p);}
static void nan_app_post_event(int id,void*p,size_t n){(void)id;(void)p;(void)n;assert(0);}
static void os_event_group_set_bits(void *group,unsigned value){assert(group==nan_event_group);bits|=value;}
'''
TERMINATION_MAIN = r'''
static void receive_notice(uint32_t identity,const esp32_mquickjs_wifi_nan_sdk_notice_t *n,void *p){
    assert(identity&&p==&notifications&&!observer_depth&&resets==0);
    assert(n->kind==ESP32_MQUICKJS_NAN_SDK_NDP_TERMINATED&&n->ndp_id==9&&n->status==7);
    assert(n->peer[0]==2&&(bits&NDP_TERMINATED));++notifications;
}
int main(void){uint32_t identity=0;uint8_t mac[6]={2,3,4,5,6,7};
    assert(esp32_mquickjs_wifi_nan_sdk_observe(receive_notice,&notifications,&identity)==ESP_OK);
    nan_app_ndp_terminated_cb(7,9,mac);assert((bits&NDP_TERMINATED)&&notifications==1);
    assert(esp32_mquickjs_wifi_nan_sdk_unobserve(&identity)==ESP_OK);}
'''
START_BOUNDARY = r'''
typedef int wifi_mode_t;
typedef struct{int channel;}wifi_nan_sync_config_t;
typedef union{wifi_nan_sync_config_t nan;}wifi_config_t;
typedef unsigned EventBits_t;
static void *nan_event_group=(void *)1,*s_nan_data_lock=(void *)2;
static struct{unsigned state;void *nan_netif;}s_nan_ctx={.nan_netif=(void *)3};
static unsigned depth,start_calls,wait_ticks;
static bool s_usd_in_progress;
static int s_esp32qjs_nan_start_error;
#define NAN_DATA_LOCK() do{assert(!depth);++depth;}while(0)
#define NAN_DATA_UNLOCK() do{assert(depth==1);--depth;}while(0)
#define NAN_STARTED_BIT 1
#define WIFI_MODE_NAN 4
#define WIFI_IF_NAN 3
#define ESP_ERR_WIFI_NOT_INIT -90
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_RETURN_ON_ERROR(call,...) do{int e=(call);if(e)return e;}while(0)
#define portMAX_DELAY UINT32_MAX
#define pdFALSE 0
#define pdMS_TO_TICKS(ms) (ms)
static int esp_wifi_get_mode(int *mode){*mode=1;return 0;}
static int esp_wifi_set_mode(int mode){assert(mode==WIFI_MODE_NAN&&!depth);return 0;}
static int esp_wifi_set_config(int iface,void *cfg){assert(iface==WIFI_IF_NAN&&cfg&&!depth);return 0;}
static int esp_wifi_start(void){assert(!depth);start_calls++;return 0;}
static void os_event_group_clear_bits(void *p,unsigned mask){assert(p==nan_event_group&&mask==1);}
static unsigned os_event_group_wait_bits(void*p,unsigned mask,int a,int b,unsigned ticks){
    assert(p==nan_event_group&&mask==1&&!a&&!b);wait_ticks=ticks;return 0;
}
'''
START_MAIN = r'''
int main(void){wifi_nan_sync_config_t config={6};
    assert(esp_wifi_nan_sync_start(&config)==ESP_ERR_TIMEOUT);
    assert(wait_ticks==2000&&start_calls==1&&!depth&&s_nan_ctx.nan_netif==(void *)3);}
'''
POST_BOUNDARY = r'''
static unsigned notified,posted,wait_ticks;
#define WIFI_EVENT 1
#define OSI_FUNCS_TIME_BLOCKING UINT32_MAX
static int native_post(int base,int32_t id,void *data,size_t size,unsigned wait){
    assert(base==1&&id==7&&data&&size==4);posted++;wait_ticks=wait;return ESP_ERR_NO_MEM;
}
static struct{int(*_event_post)(int,int32_t,void*,size_t,unsigned);}g_wifi_osi_funcs={native_post};
'''
POST_MAIN = r'''
static void receive_notice(uint32_t identity,const esp32_mquickjs_wifi_nan_sdk_notice_t *notice,void *p){
    assert(identity&&p==&notified&&!posted&&!observer_depth);
    assert(notice->kind==ESP32_MQUICKJS_NAN_SDK_EVENT&&notice->event_id==7&&notice->size==4);
    assert(*(const uint32_t *)notice->data==42);notified++;
}
int main(void){uint32_t identity=0,data=42;
    assert(esp32_mquickjs_wifi_nan_sdk_observe(receive_notice,&notified,&identity)==ESP_OK);
    nan_app_post_event(7,&data,sizeof(data));assert(notified==1&&posted==1&&!wait_ticks);
    esp32_mquickjs_wifi_nan_sdk_observer_status_t status;
    esp32_mquickjs_wifi_nan_sdk_observer_status(&status);assert(status.dropped_events==1);
    assert(esp32_mquickjs_wifi_nan_sdk_unobserve(&identity)==ESP_OK);
}
'''
INIT_BOUNDARY = r'''
#include <assert.h>
#include <stdbool.h>
#include <string.h>
static void *nan_event_group,*s_nan_data_lock;
static struct{unsigned state;void *netif;}s_nan_ctx;
static int s_nan_secure_dp_funcs;
static unsigned calls,fail_at,live,registered;
#define NAN_DATA_LOCK() ((void)0)
#define NAN_DATA_UNLOCK() ((void)0)
#define ESP_LOGE(...) ((void)0)
static void *allocate(void){++calls;if(calls==fail_at)return NULL;++live;return (void *)(unsigned long)calls;}
static void *os_event_group_create(void){return allocate();}
static void *os_recursive_mutex_create(void){return allocate();}
static void os_event_group_delete(void *p){assert(p&&live);--live;}
static void os_mutex_delete(void *p){assert(p&&live);--live;}
static void esp_nan_internal_register_secure_dp_funcs(void *p){registered=p!=NULL;}
static void nan_reset_service(unsigned id,bool all){assert(!id&&all);}
static void nan_reset_ndl(unsigned id,bool all){assert(!id&&all);}
'''
INIT_MAIN = r'''
int main(void){
    fail_at=1;esp_nan_app_init();assert(!nan_event_group&&!s_nan_data_lock&&!live&&!registered&&calls==1);
    calls=0;fail_at=2;esp_nan_app_init();assert(!nan_event_group&&!s_nan_data_lock&&!live&&!registered&&calls==2);
    fail_at=0;esp_nan_app_init();assert(live==2&&registered);unsigned before=calls;
    void *group=nan_event_group,*lock=s_nan_data_lock;esp_nan_app_init();
    assert(calls==before&&group==nan_event_group&&lock==s_nan_data_lock&&live==2);
    esp_nan_app_deinit();assert(!live&&!registered&&!nan_event_group&&!s_nan_data_lock);
}
'''

SERVICE_CALLBACK_BOUNDARY = r'''
#include <stdlib.h>
#define NAN_DATA_LOCK() ((void)0)
#define NAN_DATA_UNLOCK() ((void)0)
#define MACADDR_EQUAL(a,b) (!memcmp(a,b,6))
#define MACADDR_COPY(a,b) memcpy(a,b,6)
#define ESP_LOGE(...) ((void)0)
#define ESP_LOG_BUFFER_HEXDUMP(...) ((void)0)
#define WIFI_EVENT_NAN_REPLIED 3
struct nan_cb_peer_info {uint8_t peer_svc_id,peer_mac[6],*ssi;uint16_t ssi_len;uint32_t device_caps;};
struct peer_svc_info {uint8_t svc_id,own_svc_id,peer_nmi[6];uint32_t device_caps;};
typedef struct {uint8_t publish_id,subscribe_id,sub_if_mac[6];uint32_t reserved_1,reserved_2;uint16_t ssi_len;uint8_t ssi[];} wifi_event_nan_replied_t;
static struct peer_svc_info peer;
static bool fail_allocation,free_entered,free_continue;
static unsigned posted;
static pthread_mutex_t free_mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t free_condition=PTHREAD_COND_INITIALIZER;
static struct peer_svc_info *nan_find_peer_svc(uint8_t a,uint8_t b,const uint8_t *c){(void)a;(void)b;(void)c;return &peer;}
static void nan_record_peer_svc(uint8_t a,uint8_t b,const uint8_t*c,uint32_t d){(void)a;(void)b;(void)c;(void)d;}
static void *os_zalloc(size_t size){return fail_allocation?NULL:calloc(1,size);}
static void nan_app_post_event(int event,void *data,size_t size){assert(event==3&&data&&size);++posted;}
static void os_free(void *p){
 pthread_mutex_lock(&free_mutex);free_entered=true;pthread_cond_broadcast(&free_condition);
 while(!free_continue)pthread_cond_wait(&free_condition,&free_mutex);
 pthread_mutex_unlock(&free_mutex);free(p);
}
'''
SERVICE_CALLBACK_MAIN = r'''
static void *deliver_replied(void *p){(void)p;struct nan_cb_peer_info info={.peer_svc_id=2,.peer_mac={1,2,3,4,5,6}};nan_app_replied_cb(7,&info);return NULL;}
int main(void){
 assert(!esp32_mquickjs_wifi_nan_sdk_service_resume(0));
 pthread_t task;assert(!pthread_create(&task,NULL,deliver_replied,NULL));
 pthread_mutex_lock(&free_mutex);while(!free_entered)pthread_cond_wait(&free_condition,&free_mutex);pthread_mutex_unlock(&free_mutex);
 esp32_mquickjs_wifi_nan_sdk_observer_status_t status;esp32_mquickjs_wifi_nan_sdk_observer_status(&status);
 assert(posted==1&&!status.callbacks&&status.service_callbacks==1);
 assert(esp32_mquickjs_wifi_nan_sdk_service_quiesce(7)==ESP_ERR_TIMEOUT);
 assert(esp32_mquickjs_wifi_nan_sdk_service_resume(7)==ESP_ERR_INVALID_STATE);
 deliver_replied(NULL);assert(posted==1); /* denied before touching SDK service storage */
 pthread_mutex_lock(&free_mutex);free_continue=true;pthread_cond_broadcast(&free_condition);pthread_mutex_unlock(&free_mutex);
 assert(!pthread_join(task,NULL));assert(!esp32_mquickjs_wifi_nan_sdk_service_quiesce(7));
 assert(!esp32_mquickjs_wifi_nan_sdk_service_resume(7));
 fail_allocation=true;deliver_replied(NULL);assert(!esp32_mquickjs_wifi_nan_sdk_service_quiesce(7));
 assert(!esp32_mquickjs_wifi_nan_sdk_service_resume(7));nan_app_replied_cb(7,NULL);
 assert(!esp32_mquickjs_wifi_nan_sdk_service_quiesce(0));
}
'''

NATIVE_TX_SCOPE_MAIN = r'''
static uint8_t tx_buffer[80],tx_metadata[32];
static bool tail_entered,tail_continue;
static unsigned tail_writes,action_completions;
void __real_nan_sdf_txcb(void *buffer){
 assert(buffer==tx_buffer&&!observer_depth);
 if(tx_metadata[9]&0x7f){++action_completions;return;}
 struct nan_cb_peer_info info={.peer_svc_id=2,.peer_mac={1,2,3,4,5,6}};
 nan_app_replied_cb(7,&info);
 pthread_mutex_lock(&free_mutex);tail_entered=true;pthread_cond_broadcast(&free_condition);
 while(!tail_continue)pthread_cond_wait(&free_condition,&free_mutex);
 ++tail_writes;pthread_mutex_unlock(&free_mutex);
}
static void *deliver_native(void *p){(void)p;__wrap_nan_sdf_txcb(tx_buffer);return NULL;}
int main(void){
 uint8_t *metadata_pointer=tx_metadata;memcpy(tx_buffer+56,&metadata_pointer,sizeof(metadata_pointer));tx_metadata[8]=7;
 free_continue=true;assert(!esp32_mquickjs_wifi_nan_sdk_service_resume(0));
 pthread_t task;assert(!pthread_create(&task,NULL,deliver_native,NULL));
 pthread_mutex_lock(&free_mutex);while(!tail_entered)pthread_cond_wait(&free_condition,&free_mutex);pthread_mutex_unlock(&free_mutex);
 esp32_mquickjs_wifi_nan_sdk_observer_status_t status;esp32_mquickjs_wifi_nan_sdk_observer_status(&status);
 assert(posted==1&&!status.callbacks&&status.service_callbacks==1&&!tail_writes);
 assert(esp32_mquickjs_wifi_nan_sdk_service_quiesce(7)==ESP_ERR_TIMEOUT);
 assert(esp32_mquickjs_wifi_nan_sdk_service_resume(7)==ESP_ERR_INVALID_STATE);
 __wrap_nan_sdf_txcb(tx_buffer);assert(posted==1&&!tail_writes);
 pthread_mutex_lock(&free_mutex);tail_continue=true;pthread_cond_broadcast(&free_condition);pthread_mutex_unlock(&free_mutex);
 assert(!pthread_join(task,NULL));assert(tail_writes==1);
 assert(!esp32_mquickjs_wifi_nan_sdk_service_quiesce(0));
 __wrap_nan_sdf_txcb(tx_buffer);assert(posted==1&&tail_writes==1);
 tx_metadata[9]=2;__wrap_nan_sdf_txcb(tx_buffer);assert(action_completions==1);
 tx_metadata[9]=0;assert(!esp32_mquickjs_wifi_nan_sdk_service_resume(0));
 __wrap_nan_sdf_txcb(tx_buffer);assert(posted==2&&tail_writes==2);
 esp32_mquickjs_wifi_nan_sdk_observer_status(&status);assert(!status.service_callbacks);
}
'''

SERVICE_START_BOUNDARY = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "esp_err.h"
#define CONFIG_ESP_WIFI_NAN_SYNC_ENABLE 1
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define NAN_STARTED_BIT 1
#define ESP_NAN_PUBLISH 2
#define ESP_NAN_SUBSCRIBE 1
#define NAN_SVC_ID_PENDING 255
#define ESP_WIFI_MAX_SVC_SSI_LEN 512
#define WIFI_SVC_PROTO_MAX 16
typedef struct {uint8_t oui[3],proto;}wifi_nan_wfa_ssi_t;
typedef struct {unsigned ignored;}pairing_t;
typedef struct {
 char service_name[256],matching_filter[256];
 uint8_t *ssi;uint16_t ssi_len;pairing_t *pairing;void *security_cfg;
 bool usd_discovery_flag,security_reqd,ndp_resp_needed;
}wifi_nan_publish_cfg_t;
typedef wifi_nan_publish_cfg_t wifi_nan_subscribe_cfg_t;
static unsigned depth,allocations,fail_at,heap_live,submits;
static bool claimed,s_usd_in_progress;
static int native_error=-72;
static void *s_nan_data_lock=(void*)1,*nan_event_group=(void*)2;
static struct{unsigned state;}s_nan_ctx={NAN_STARTED_BIT};
static uint8_t s_wfa_oui[3]={0x50,0x6f,0x9a};
#define NAN_DATA_LOCK() do{assert(!depth);++depth;}while(0)
#define NAN_DATA_UNLOCK() do{assert(depth==1);--depth;}while(0)
static bool nan_services_limit_reached(void){assert(depth==1);return false;}
static void *nan_find_own_svc_by_name(const char*n){assert(depth==1&&n);return NULL;}
static void *os_zalloc(size_t size){if(++allocations==fail_at)return NULL;void*p=calloc(1,size);assert(p);++heap_live;return p;}
static void *os_malloc(size_t size){return os_zalloc(size);}
static void os_free(void *p){assert(p&&heap_live);--heap_live;free(p);}
static bool nan_compute_service_id(const char*n,uint8_t *hash){assert(n&&hash&&depth==1);memset(hash,1,6);return true;}
static bool nan_claim_own_svc_slot(unsigned type,const char*n,void*a,void*b){assert(type&&n&&!a&&depth==1);(void)b;claimed=true;return true;}
static void nan_abort_own_svc(const char*n){assert(n&&claimed&&depth==1);claimed=false;}
static void nan_finalize_own_svc(const char*n,uint8_t id,bool respond,uint8_t *hash){assert(n&&(id==7||id==NAN_SVC_ID_PENDING)&&hash&&claimed&&depth==1);(void)respond;}
static int native_submit(void *config,uint8_t *id,bool cancel){
 assert(!depth&&claimed&&config&&!cancel);++submits;
 /* A callback on the WiFi task must be allowed to acquire its data mutex. */
 NAN_DATA_LOCK();assert(claimed);NAN_DATA_UNLOCK();
 if(!native_error)*id=7;
 return native_error;
}
static int esp_nan_internal_publish_service(void*c,uint8_t*i,bool x){return native_submit(c,i,x);}
static int esp_nan_internal_subscribe_service(void*c,uint8_t*i,bool x){return native_submit(c,i,x);}
'''
SERVICE_START_MAIN = r'''
int main(void){
 CONFIG_TYPE config={.service_name="test"};uint8_t id=0;
 assert(CHECKED_CALL(&config,&id)==-72&&!id&&!claimed&&!heap_live&&submits==1&&!depth);
 allocations=0;fail_at=1;assert(CHECKED_CALL(&config,&id)==ESP_ERR_NO_MEM&&!id&&!heap_live&&submits==1);
 allocations=0;fail_at=2;pairing_t pairing={0};config.pairing=&pairing;
 assert(CHECKED_CALL(&config,&id)==ESP_ERR_NO_MEM&&!id&&!heap_live&&submits==1);
 config.pairing=NULL;fail_at=0;native_error=0;
 assert(!CHECKED_CALL(&config,&id)&&id==7&&claimed&&!heap_live&&submits==2&&!depth);
 assert(CHECKED_CALL(&config,&id)==ESP_ERR_INVALID_ARG&&submits==2);
 id=0;memset(config.service_name,'x',sizeof(config.service_name));
 assert(CHECKED_CALL(&config,&id)==ESP_ERR_INVALID_ARG&&submits==2);
}
'''

SERVICE_CANCEL_BOUNDARY = r'''
#define CONFIG_ESP_WIFI_NAN_SYNC_ENABLE 1
#define ESP_NAN_PUBLISH 2
#define ESP_NAN_SUBSCRIBE 1
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
static unsigned depth,cancel_calls,resets;
static int cancel_result=-72;
static bool ndp_busy;
static bool esp32_mquickjs_wifi_nan_ndp_service_busy(uint8_t id){assert(id==7);return ndp_busy;}
static bool s_usd_in_progress;
static void *s_nan_data_lock=(void*)1,*nan_event_group=(void*)2;
struct own_svc_info{uint8_t type;};
static struct own_svc_info own_service={ESP_NAN_PUBLISH};
#define NAN_DATA_LOCK() do{assert(!depth);++depth;}while(0)
#define NAN_DATA_UNLOCK() do{assert(depth==1);--depth;}while(0)
static struct own_svc_info *nan_find_own_svc(uint8_t id){assert(depth==1);return id==7?&own_service:NULL;}
static void nan_reset_service(uint8_t id,bool all){assert(depth==1&&id==7&&!all);++resets;}
static int native_cancel(void *config,uint8_t *id,bool cancel){
 assert(!depth&&!config&&id&&*id==7&&cancel);++cancel_calls;
 NAN_DATA_LOCK();NAN_DATA_UNLOCK();return cancel_result;
}
static int esp_nan_internal_publish_service(void*c,uint8_t*i,bool x){return native_cancel(c,i,x);}
static int esp_nan_internal_subscribe_service(void*c,uint8_t*i,bool x){return native_cancel(c,i,x);}
'''
SERVICE_CANCEL_MAIN = r'''
int main(void){
 assert(!esp32_mquickjs_wifi_nan_sdk_service_resume(0));
 ndp_busy=true;
 assert(esp32_mquickjs_wifi_nan_sdk_cancel_service(7)==ESP_ERR_INVALID_STATE&&!cancel_calls&&!resets);
 esp32qjs_nan_service_guard_t unfrozen=esp32qjs_nan_service_enter(7);assert(unfrozen.entered);
 esp32qjs_nan_service_leave(&unfrozen);ndp_busy=false;
 esp32qjs_nan_service_guard_t entered=esp32qjs_nan_service_enter(7);assert(entered.entered);
 assert(esp32_mquickjs_wifi_nan_sdk_cancel_service(7)==ESP_ERR_TIMEOUT&&!cancel_calls&&!resets);
 esp32qjs_nan_service_leave(&entered);
 assert(esp32_mquickjs_wifi_nan_sdk_cancel_service(7)==-72&&cancel_calls==1&&!resets&&!depth);
 assert(!esp32qjs_nan_service_enter(7).entered);
 cancel_result=0;assert(!esp32_mquickjs_wifi_nan_sdk_cancel_service(7)&&cancel_calls==2&&resets==1&&!depth);
 assert(!esp32qjs_nan_service_enter(7).entered); /* successful cancel retains freeze for TX retirement */
 assert(!esp32_mquickjs_wifi_nan_sdk_service_resume(7));
 entered=esp32qjs_nan_service_enter(7);assert(entered.entered);esp32qjs_nan_service_leave(&entered);
 assert(esp32_mquickjs_wifi_nan_sdk_cancel_service(0)==ESP_ERR_INVALID_ARG&&cancel_calls==2);
}
'''

SERVICE_BIND_BOUNDARY = r'''
#define ESP_NAN_PUBLISH 2
#define ESP_NAN_SUBSCRIBE 1
#define NAN_SVC_ID_PENDING 255
typedef struct {char service_name[256];bool datapath_reqd;}wifi_nan_publish_cfg_t;
typedef wifi_nan_publish_cfg_t wifi_nan_subscribe_cfg_t;
struct own_svc_info{uint8_t svc_id,type;};
static struct own_svc_info own;
static unsigned depth,notifications;static bool missing_host;
static int create_result;
#define NAN_DATA_LOCK() do{assert(!depth);++depth;}while(0)
#define NAN_DATA_UNLOCK() do{assert(depth==1);--depth;}while(0)
static struct own_svc_info*nan_find_own_svc_by_name(const char*name){assert(depth==1&&name);return missing_host?NULL:&own;}
int __real_nan_start_publish_service(const wifi_nan_publish_cfg_t*c,uint8_t*id){assert(!depth&&c&&own.svc_id==255);if(!create_result)*id=7;return create_result;}
int __real_nan_start_subscribe_service(const wifi_nan_subscribe_cfg_t*c,uint8_t*id){return __real_nan_start_publish_service(c,id);}
'''
SERVICE_BIND_MAIN = r'''
static void bound_notice(uint32_t identity,const esp32_mquickjs_wifi_nan_sdk_notice_t*n,void*opaque){
 assert(identity&&opaque==&notifications&&!depth&&!observer_depth&&n->kind==ESP32_MQUICKJS_NAN_SDK_SERVICE_BOUND&&n->service_id==7);
 if(missing_host)assert(n->status==ESP_ERR_INVALID_RESPONSE);
 else assert(!n->status&&own.svc_id==7&&n->context==own.type&&esp32qjs_nan_is_discovery_only(7));
 ++notifications;
}
int main(void){
 uint32_t observer=0;assert(!esp32_mquickjs_wifi_nan_sdk_observe(bound_notice,&notifications,&observer));
 wifi_nan_publish_cfg_t config={.service_name="test"};uint8_t id=0;
 own=(struct own_svc_info){255,2};assert(!__wrap_nan_start_publish_service(&config,&id)&&id==7&&notifications==1);
 own=(struct own_svc_info){255,1};id=0;assert(!__wrap_nan_start_subscribe_service(&config,&id)&&id==7&&notifications==2);
 own.svc_id=255;id=0;create_result=-71;assert(__wrap_nan_start_subscribe_service(&config,&id)==-71&&!id&&notifications==2);
 create_result=0;missing_host=true;assert(!__wrap_nan_start_subscribe_service(&config,&id)&&id==7&&notifications==3);
 assert(!esp32_mquickjs_wifi_nan_sdk_unobserve(&observer));
}
'''
