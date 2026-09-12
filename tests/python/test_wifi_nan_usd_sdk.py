"""Deferred USD production lifecycle/timer fixtures; no execution on import."""
import re
import unittest

from test_idf_nan_control import BASE, NanNativeControl


class NanUsdSdkLifecycle(unittest.TestCase):
    compile_run = NanNativeControl.compile_run

    def production(self):
        header = (BASE / 'internal/esp32_mquickjs_wifi_nan_usd_sdk.h').read_text()
        header = re.sub(r'^#(?:include[^\n]*|pragma once)\n', '', header, flags=re.M)
        module = BASE / 'src/modules/wifi_nan'
        tx = (BASE / 'internal/esp32_mquickjs_wifi_nan_tx.h').read_text()
        tx = re.sub(r'^#(?:include[^\n]*|pragma once)\n', '', tx, flags=re.M)
        return (PREFIX + tx + header + BOUNDARY +
                (module / 'esp32_mquickjs_wifi_nan_usd_state.inc').read_text() +
                (module / 'esp32_mquickjs_wifi_nan_usd_lifecycle.inc').read_text())

    def test_start_failures_preserve_owner_and_close_retries_only_the_suffix(self):
        self.compile_run(self.production() + r'''
int main(void){
 for(unsigned fail=1;fail<=1;fail++){
  register_calls=unregister_calls=0;fail_register=fail;
  assert(esp_nan_usd_init()==-71&&g_nan_de);
  assert(s_esp32qjs_usd_closing&&s_esp32qjs_usd_handlers==((1U<<(fail-1))-1));
  assert(esp_nan_usd_init()==ESP_ERR_INVALID_STATE&&register_calls==fail);
  assert(!esp_nan_usd_deinit()&&!g_nan_de&&!s_esp32qjs_usd_handlers&&!s_esp32qjs_usd_identity);
  assert(unregister_calls==fail-1);
 }
 register_calls=unregister_calls=0;fail_register=0;
 assert(!esp_nan_usd_init()&&s_esp32qjs_usd_handlers==1);
 fail_unregister=1;assert(esp_nan_usd_deinit()==-72);
 assert(g_nan_de&&s_esp32qjs_usd_handlers==1&&s_esp32qjs_usd_closing);
 unsigned destroyed=engine_frees;fail_unregister=0;
 assert(!esp_nan_usd_deinit()&&unregister_calls==2&&engine_frees==destroyed+1);
 assert(!esp_nan_usd_deinit()&&unregister_calls==2&&engine_frees==destroyed+1);
 mac_error=-73;assert(esp_nan_usd_init()==-73&&!g_nan_de&&s_esp32qjs_usd_identity);
 assert(!esp_nan_usd_deinit());mac_error=0;fail_engine=true;
 assert(esp_nan_usd_init()==ESP_ERR_NO_MEM&&!g_nan_de);
 assert(!esp_nan_usd_deinit());
}
''')

    def test_native_buffer_blocks_engine_free_and_handler_retirement(self):
        self.compile_run(self.production() + r'''
int main(void){
 assert(!esp_nan_usd_init());transport_busy=true;
 assert(esp_nan_usd_deinit()==ESP_ERR_NOT_FINISHED&&g_nan_de&&!unregister_calls&&!engine_frees);
 assert(esp_nan_usd_init()==ESP_ERR_INVALID_STATE);
 transport_busy=false;assert(!esp_nan_usd_deinit()&&!g_nan_de&&unregister_calls==1&&engine_frees==1);
}
''')

    def test_dispatched_timer_cannot_enter_reused_engine_and_exhaustion_does_not_wrap(self):
        self.compile_run(self.production() + r'''
static uint32_t old_identity;
static struct nan_de*old_engine;
static pthread_barrier_t pending,dispatch;
static void*late_timer(void*unused){
 (void)unused;pthread_barrier_wait(&pending);pthread_barrier_wait(&dispatch);
 assert(!esp32qjs_nan_usd_timer_enter(old_engine,old_identity));return NULL;
}
int main(void){
 assert(!esp_nan_usd_init());old_engine=g_nan_de;old_identity=s_esp32qjs_usd_identity;
 pthread_barrier_init(&pending,NULL,2);pthread_barrier_init(&dispatch,NULL,2);
 pthread_t t;assert(!pthread_create(&t,NULL,late_timer,NULL));pthread_barrier_wait(&pending);
 assert(!esp_nan_usd_deinit()&&!esp_nan_usd_init());assert(g_nan_de==old_engine);
 assert(s_esp32qjs_usd_identity!=old_identity);
 pthread_barrier_wait(&dispatch);assert(!pthread_join(t,NULL));
 assert(esp32qjs_nan_usd_timer_enter(g_nan_de,s_esp32qjs_usd_identity));esp32qjs_nan_usd_timer_leave();
 esp32qjs_nan_usd_timer_failed(g_nan_de);
 esp32_mquickjs_wifi_nan_usd_sdk_status_t status;esp32_mquickjs_wifi_nan_usd_sdk_status(&status);
 assert(status.error==ESP_ERR_NO_MEM&&status.closing);
 assert(!esp32qjs_nan_usd_timer_enter(g_nan_de,s_esp32qjs_usd_identity));
 assert(!esp_nan_usd_deinit());s_esp32qjs_usd_next_identity=UINT32_MAX;
 assert(!esp_nan_usd_init()&&s_esp32qjs_usd_identity==UINT32_MAX);
 assert(!esp_nan_usd_deinit());unsigned created=engine_creates;
 assert(esp_nan_usd_init()==ESP_ERR_NO_MEM&&!g_nan_de&&engine_creates==created);
}
''')

    def test_full_observation_queue_never_blocks_or_changes_engine_lifecycle(self):
        self.compile_run(self.production() + r'''
int main(void){
 assert(!esp_nan_usd_init());post_error=-74;
 NAN_USD_DATA_LOCK();esp32qjs_nan_usd_post(4,NULL,0);NAN_USD_DATA_UNLOCK();
 assert(post_calls==1&&s_esp32qjs_usd_drops==1&&!s_esp32qjs_usd_closing);
 s_esp32qjs_usd_drops=UINT32_MAX;
 NAN_USD_DATA_LOCK();esp32qjs_nan_usd_post(4,NULL,0);NAN_USD_DATA_UNLOCK();
 assert(s_esp32qjs_usd_drops==UINT32_MAX&&!s_esp32qjs_usd_error);
 assert(!esp_nan_usd_deinit());
}
''')

    def test_command_wait_never_holds_engine_mutex_and_stale_ticket_cannot_mutate(self):
        source = (BASE / 'src/modules/wifi_nan/esp32_mquickjs_wifi_nan_usd_commands.inc').read_text()
        self.compile_run(self.production() + COMMAND_BOUNDARY + source + r'''
int main(void){
 assert(!esp_nan_usd_init());wifi_nan_publish_cfg_t cfg={.service_name="service",.type=NAN_PUBLISH_UNSOLICITED};
 assert(esp_nan_usd_publish(&cfg)==7&&dispatches==1&&native_calls==1&&!lock_depth);
 native_wifi=true;assert(!esp_nan_usd_cancel_service(7)&&dispatches==1&&native_calls==2);native_wifi=false;
 esp32qjs_usd_command_t stale={.kind=USD_CANCEL_SERVICE,.service=7,.identity=s_esp32qjs_usd_identity};
 assert(!esp_nan_usd_deinit()&&!esp_nan_usd_init());native_wifi=true;
 assert(esp32qjs_nan_usd_command_run(&stale,NULL)==ESP_ERR_INVALID_STATE&&native_calls==2);
 assert(!esp_nan_usd_deinit());
}
''')


PREFIX = r'''
#define _GNU_SOURCE
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
#include <pthread.h>
#include <string.h>
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define CONFIG_ESP_WIFI_NAN_USD_ENABLE 1
#define CONFIG_ESP_WIFI_NAN_SYNC_ENABLE 1
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERR_NO_MEM 2
#define ESP_ERR_INVALID_ARG 3
#define ESP_FAIL -1
#define ESP_ERR_NOT_SUPPORTED 4
#define ESP_ERR_NOT_FINISHED 5
#define ESP_WIFI_MAX_SVC_SSI_LEN 512
#define ESP_WIFI_MAX_FUP_SSI_LEN 2048
#define NAN_PUBLISH_UNSOLICITED 1
#define NAN_PUBLISH_SOLICITED 2
#define NAN_SUBSCRIBE_ACTIVE 3
#define NAN_SUBSCRIBE_PASSIVE 4
typedef struct {uint32_t ghz_2_channels,ghz_5_channels;}wifi_scan_channel_bitmap_t;
typedef struct {uint8_t usd_default_channel,n_min,n_max,m_min,m_max;wifi_scan_channel_bitmap_t usd_chan_bitmap;}wifi_nan_usd_config_t;
typedef struct {char service_name[256];int type;uint32_t ttl;uint8_t*ssi;uint16_t ssi_len;
 wifi_nan_usd_config_t usd_publish_config,usd_subscribe_config;}wifi_nan_publish_cfg_t;
typedef wifi_nan_publish_cfg_t wifi_nan_subscribe_cfg_t;
typedef struct {uint8_t inst_id,peer_inst_id,peer_mac[6];uint16_t ssi_len;uint8_t*ssi;void*vendor_ie;}wifi_nan_followup_params_t;
typedef struct {int unused;}wifi_event_action_tx_status_t;
typedef enum {ESP32_MQUICKJS_NAN_SDK_EVENT,ESP32_MQUICKJS_NAN_SDK_STARTED,
 ESP32_MQUICKJS_NAN_SDK_STOPPED,ESP32_MQUICKJS_NAN_SDK_SERVICE_BOUND}esp32_mquickjs_wifi_nan_sdk_notice_kind_t;
typedef struct {esp32_mquickjs_wifi_nan_sdk_notice_kind_t kind;int32_t event_id,status;uint8_t service_id;
 const void*data;size_t size;uint32_t context;}esp32_mquickjs_wifi_nan_sdk_notice_t;
#define ETH_ALEN 6
#define WIFI_IF_STA 0
#define WIFI_EVENT 9
#define WIFI_EVENT_ACTION_TX_STATUS 1
#define WIFI_EVENT_ROC_DONE 2
#define WIFI_EVENT_STA_STOP 3
'''

BOUNDARY = r'''
struct nan_de{int dummy;};
static struct nan_de engine,*g_nan_de;
static _Atomic(void*)s_nan_usd_data_lock;
static pthread_mutex_t mutex;
static unsigned register_calls,unregister_calls,fail_register,fail_unregister;
static unsigned engine_creates,engine_frees,post_calls;
static bool fail_engine,transport_busy;
typedef struct {esp32_mquickjs_wifi_nan_message_tx_status_t message;bool message_scope;esp_err_t error;}esp32qjs_usd_transport_t;
static esp32qjs_usd_transport_t transport_storage,*s_esp32qjs_usd_transport;
static int esp32qjs_nan_usd_transport_open(void){assert(!s_esp32qjs_usd_transport);memset(&transport_storage,0,sizeof(transport_storage));s_esp32qjs_usd_transport=&transport_storage;return 0;}
static int esp32qjs_nan_usd_transport_free(void){assert(!transport_busy);s_esp32qjs_usd_transport=NULL;return 0;}
static void esp32qjs_nan_usd_transport_snapshot(esp32_mquickjs_wifi_nan_usd_sdk_status_t*s){s->transport_held=transport_busy;}
esp_err_t esp32_mquickjs_wifi_nan_usd_sdk_poll(bool close){assert(close);return transport_busy?ESP_ERR_NOT_FINISHED:0;}
static void esp32_mquickjs_wifi_nan_sdk_notice(const esp32_mquickjs_wifi_nan_sdk_notice_t*notice){assert(notice);}
static int esp32qjs_nan_usd_service_drain(uint8_t id){assert(id);return transport_busy?ESP_ERR_NOT_FINISHED:0;}
static int mac_error,post_error;
static _Thread_local unsigned lock_depth;
static void*os_recursive_mutex_create(void){
 pthread_mutexattr_t attr;pthread_mutexattr_init(&attr);
 pthread_mutexattr_settype(&attr,PTHREAD_MUTEX_RECURSIVE);
 assert(!pthread_mutex_init(&mutex,&attr));pthread_mutexattr_destroy(&attr);return &mutex;
}
#define NAN_USD_DATA_LOCK() do{pthread_mutex_lock(s_nan_usd_data_lock);++lock_depth;}while(0)
#define NAN_USD_DATA_UNLOCK() do{assert(lock_depth);--lock_depth;pthread_mutex_unlock(s_nan_usd_data_lock);}while(0)
typedef void(*handler_t)(void*,int,int,void*);
static void nan_sta_stop_handler(void*a,int b,int c,void*d){(void)a;(void)b;(void)c;(void)d;}
static void nan_de_tx_event_handler(void*a,int b,int c,void*d){(void)a;(void)b;(void)c;(void)d;}
static int esp_event_handler_register(int base,int event,handler_t handler,void*arg){
 assert(!lock_depth&&base==WIFI_EVENT&&event>=1&&event<=3&&handler&&!arg);
 return ++register_calls==fail_register?-71:0;
}
static int esp_event_handler_unregister(int base,int event,handler_t handler){
 assert(!lock_depth&&base==WIFI_EVENT&&event>=1&&event<=3&&handler);
 return ++unregister_calls==fail_unregister?-72:0;
}
static int esp_event_post(int base,int event,const void*data,size_t size,unsigned wait){
 (void)data;(void)size;assert(lock_depth&&base==WIFI_EVENT&&event==4&&!wait);++post_calls;return post_error;
}
static int esp_wifi_get_mac(int interface,uint8_t*mac){assert(!lock_depth&&!interface);memset(mac,2,6);return mac_error;}
static void esp_nan_de_tx(void){}
static void esp_nan_de_listen(void){}
static void esp_nan_de_discovery_result(void){}
static void esp_nan_de_replied(void){}
static void esp_nan_de_publish_terminated(void){}
static void esp_nan_de_subscribe_terminated(void){}
static void esp_nan_de_receive(void){}
struct nan_callbacks{void(*tx)(void),(*listen)(void),(*discovery_result)(void),(*replied)(void),
 (*publish_terminated)(void),(*subscribe_terminated)(void),(*receive)(void);};
static struct nan_de*nan_de_init(uint8_t*mac,bool offload,bool ap,unsigned listen,struct nan_callbacks*cb){
 assert(lock_depth&&mac&&!offload&&!ap&&listen==1000&&cb->tx==esp_nan_de_tx);++engine_creates;
 return fail_engine?NULL:&engine;
}
static void nan_de_deinit(struct nan_de*de){assert(lock_depth&&de==&engine);++engine_frees;}
'''

COMMAND_BOUNDARY = r'''
static int esp_nan_chan_to_freq(int channel){return channel>=1&&channel<=14?2407+channel*5:-1;}
static bool native_wifi;
static unsigned dispatches,native_calls;
bool current_task_is_wifi_task(void){return native_wifi;}
static int eloop_register_timeout_blocking(int(*fn)(void*,void*),void*data,void*ctx){
 assert(!lock_depth&&!native_wifi&&!ctx);++dispatches;native_wifi=true;
 int result=fn(data,ctx);native_wifi=false;return result;
}
static int esp32qjs_usd_publish_native(const void*c){assert(c&&native_wifi&&lock_depth);++native_calls;return 7;}
static int esp32qjs_usd_subscribe_native(const void*c){return esp32qjs_usd_publish_native(c);}
static int esp32qjs_usd_cancel_service_native(int id){assert(id==7&&native_wifi&&lock_depth);++native_calls;return 0;}
static int esp32qjs_usd_cancel_publish_native(int id){return esp32qjs_usd_cancel_service_native(id);}
static int esp32qjs_usd_cancel_subscribe_native(int id){return esp32qjs_usd_cancel_service_native(id);}
static int esp32qjs_usd_update_publish_native(int id,uint8_t*ssi,uint16_t len){(void)ssi;(void)len;return esp32qjs_usd_cancel_service_native(id);}
static int esp32qjs_usd_transmit_native(int id,const uint8_t*ssi,uint16_t len,const uint8_t*peer,uint8_t peer_id){
 (void)ssi;(void)len;(void)peer;(void)peer_id;return esp32qjs_usd_cancel_service_native(id);
}
'''
