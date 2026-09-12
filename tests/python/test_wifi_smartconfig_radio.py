"""Deferred production SmartConfig Radio admission, exact pins and restoration."""
import re
import unittest
from test_wifi_vendor_ie import vendor_code, COMPONENT
from test_wifi_config_controls import sdk_types, structure
from test_wifi_rx_target import unit
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import CORE, extract


class SmartConfigRadio(unittest.TestCase):
    def test_production_radio_binding(self):
        radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
        header = (COMPONENT / 'internal/esp32_mquickjs_wifi_radio.h').read_text()
        code = '#define CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API 1\n#define CONFIG_LWIP_IPV4 1\n' + vendor_code('esp32c5/representative')
        # Event-loop fence callbacks only take the critical lock; SDK mutation
        # stubs still require the operation mutex independently.
        code = code.replace('assert(locks && !critical);critical=1;', 'assert(!critical);critical=1;')
        code = code.replace('assert(locks && critical);critical=0;', 'assert(critical);critical=0;')
        enums = re.search(r'typedef enum \{[^}]*\} esp32_mquickjs_wifi_radio_operation_kind_t;', header).group(0)
        before = sdk_types('esp32c5/representative')
        more = sdk_types('esp32c5/representative', ('wifi_ap_record_t',))
        assert more.startswith(before)
        extra = more[len(before):] + enums + structure(header, 'esp32_mquickjs_wifi_radio_operation_t') + SMARTCONFIG_TYPES
        for name in ('events', 'decoder', 'radio'):
            extra += unit(COMPONENT / 'internal' / ('esp32_mquickjs_wifi_smartconfig_' + name + '.h'))
        extra += structure(radio, 'wifi_radio_smartconfig_t')
        extra += '\nstatic wifi_radio_smartconfig_t *s_sc_radio;\n#define WIFI_RADIO_SMARTCONFIG_PENDING (s_sc_radio != NULL)\n#define WIFI_RADIO_EAP_PENDING false\n'
        extra += '\nstatic struct {uint32_t owners[3];} *s_wps_radio;\n#define WIFI_RADIO_DPP_PENDING false\n#define WIFI_RADIO_WPS_PENDING (s_wps_radio != NULL)\n'
        extra += '\nstatic uint32_t s_sc_fence_posted,s_sc_fence_seen;\n#define WIFI_RADIO_SMARTCONFIG_FENCE_EVENT 7\n#define ESP32QJS_WIFI_RADIO_CONTROL_EVENT 1\n'
        code = code.replace('static struct {\n    int lock;', extra + '\nstatic struct {\n    int lock;', 1)
        code = code.replace('struct {unsigned identity,lease_identity;} operation;', 'esp32_mquickjs_wifi_radio_operation_t operation;')
        code = code.replace('generation,next_lease_identity,next_lifecycle_identity,wake_locks;', 'generation,next_operation_identity,next_lease_identity,next_lifecycle_identity,wake_locks;')
        code += BOUNDARIES
        code += extract((CORE / 'esp32_mquickjs_wireless_core.c').read_text(), 'esp32_mquickjs_wireless_secure_zero')
        for name in ('wifi_radio_smartconfig_observe_fence','wifi_radio_smartconfig_fence_locked',
                     'wifi_radio_connection_owner_locked','wifi_radio_smartconfig_exact_locked',
                     'wifi_radio_smartconfig_snapshot_locked','wifi_radio_smartconfig_restore_locked',
                     'esp32_mquickjs_wifi_radio_smartconfig_begin','esp32_mquickjs_wifi_radio_smartconfig_status',
                     'esp32_mquickjs_wifi_radio_smartconfig_finish_capture','esp32_mquickjs_wifi_radio_smartconfig_ack',
                     'esp32_mquickjs_wifi_radio_smartconfig_connection_begin','esp32_mquickjs_wifi_radio_smartconfig_connection_end',
                     'esp32_mquickjs_wifi_radio_smartconfig_credentials','esp32_mquickjs_wifi_radio_smartconfig_close',
                     'esp32_mquickjs_wifi_radio_end_operation'):
            code += extract(radio,name)
        compile_run(self, code + CASES)


SMARTCONFIG_TYPES = r'''
#include <stdlib.h>
#define ESP_ERR_WIFI_NOT_CONNECT -20
#define ESP_ERR_WIFI_NOT_STARTED -21
#define ESP_ERR_INVALID_RESPONSE -22
#define ESP_ERR_NOT_FINISHED -23
typedef enum {SC_TYPE_ESPTOUCH,SC_TYPE_AIRKISS,SC_TYPE_ESPTOUCH_AIRKISS,SC_TYPE_ESPTOUCH_V2} smartconfig_type_t;
typedef struct {uint8_t ssid[32],password[64];bool bssid_set;uint8_t bssid[6];smartconfig_type_t type;uint8_t token,cellphone_ip[4];} smartconfig_event_got_ssid_pswd_t;
'''

BOUNDARIES = r'''
struct esp32_mquickjs_wifi_smartconfig_decoder {esp32_mquickjs_wifi_smartconfig_decoder_status_t status;};
static struct esp32_mquickjs_wifi_smartconfig_decoder decoder;
static int peer_error=ESP_ERR_WIFI_NOT_CONNECT,start_error,close_error,channel_error;
static unsigned starts,stops,channel_writes,decoder_releases;
static bool mutate=true,construct_fail;
static uint8_t channel=6;
static wifi_second_chan_t secondary=WIFI_SECOND_CHAN_NONE;
static int fence_error;
static esp32_mquickjs_wifi_radio_operation_t posted_fence;
static esp_err_t esp_event_post(int base,int id,const void *data,size_t size,unsigned wait){
 assert(locks && !critical && base==ESP32QJS_WIFI_RADIO_CONTROL_EVENT && id==7 && size==sizeof(posted_fence) && !wait);
 if(!fence_error)memcpy(&posted_fence,data,size);return fence_error;
}
esp_err_t esp32_mquickjs_wifi_smartconfig_decoder_validate_options(const esp32_mquickjs_wifi_smartconfig_decoder_options_t *o){assert(!locks && !critical);return o->channel_timeout_s>=15 ? ESP_OK : ESP_ERR_INVALID_ARG;}
static esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap){assert(locks && !critical);memset(ap,0,sizeof(*ap));return peer_error;}
static esp_err_t wifi_radio_get_channel_locked(uint8_t *primary,wifi_second_chan_t *second,uint32_t *generation){assert(locks && !critical);*primary=channel;*second=secondary;*generation=1;return 0;}
static esp_err_t wifi_radio_validate_regulatory_channel(uint8_t primary){assert(locks && !critical && primary==6);return 0;}
static esp_err_t (esp_wifi_set_channel)(uint8_t primary,wifi_second_chan_t second){assert(locks && !critical);channel_writes++;if(!channel_error){channel=primary;secondary=second;}return channel_error;}
esp_err_t esp32_mquickjs_wifi_smartconfig_decoder_create(uint32_t generation,const esp32_mquickjs_wifi_smartconfig_decoder_options_t *options,esp32_mquickjs_wifi_smartconfig_decoder_t **out){assert(locks && !critical && !*out && options);if(construct_fail)return ESP_ERR_NO_MEM;memset(&decoder,0,sizeof(decoder));decoder.status.token=(esp32_mquickjs_wifi_smartconfig_token_t){111,generation};*out=&decoder;return 0;}
esp_err_t esp32_mquickjs_wifi_smartconfig_decoder_start(esp32_mquickjs_wifi_smartconfig_decoder_t *d){assert(locks && !critical && d==&decoder);starts++;d->status.error=start_error;d->status.started=!start_error;d->status.mutation_attempted=mutate;if(mutate)channel=11;return start_error;}
esp_err_t esp32_mquickjs_wifi_smartconfig_decoder_status(esp32_mquickjs_wifi_smartconfig_decoder_t *d,esp32_mquickjs_wifi_smartconfig_decoder_status_t *out){assert(locks && !critical && d==&decoder);*out=d->status;return 0;}
esp_err_t esp32_mquickjs_wifi_smartconfig_events_status(const esp32_mquickjs_wifi_smartconfig_token_t *t,esp32_mquickjs_wifi_smartconfig_events_status_t *s){assert(locks && !critical && t->identity==111);memset(s,0,sizeof(*s));s->token=*t;return 0;}
esp_err_t esp32_mquickjs_wifi_smartconfig_decoder_finish_capture(esp32_mquickjs_wifi_smartconfig_decoder_t *d){assert(locks && !critical && d==&decoder);d->status.capture_stopped=true;d->status.started=false;return 0;}
esp_err_t esp32_mquickjs_wifi_smartconfig_decoder_ack(esp32_mquickjs_wifi_smartconfig_decoder_t *d){assert(locks && !critical && d->status.capture_stopped);return 0;}
esp_err_t esp32_mquickjs_wifi_smartconfig_decoder_credentials(esp32_mquickjs_wifi_smartconfig_decoder_t *d,esp32_mquickjs_wifi_smartconfig_credentials_t *out,bool commit){assert(locks && !critical && d->status.capture_stopped);if(!commit)memset(out,0,sizeof(*out));return 0;}
esp_err_t esp32_mquickjs_wifi_smartconfig_decoder_close(esp32_mquickjs_wifi_smartconfig_decoder_t *d){assert(locks && !critical && d==&decoder);stops++;d->status.closing=true;if(close_error)return close_error;d->status.capture_stopped=d->status.retired=true;d->status.token.identity=0;return 0;}
esp_err_t esp32_mquickjs_wifi_smartconfig_decoder_release(esp32_mquickjs_wifi_smartconfig_decoder_t **d){assert(locks && !critical && (*d)->status.retired);*d=NULL;decoder_releases++;return 0;}
'''

CASES = r'''
static esp32_mquickjs_wifi_radio_lease_t helper[3];
static esp32_mquickjs_wifi_smartconfig_decoder_options_t options={.type=SC_TYPE_ESPTOUCH,.channel_timeout_s=60};
static void setup(void){
 assert(!s_sc_radio);reset_vendor();memset(helper,0,sizeof(helper));
 s_radio.effective_mode=WIFI_MODE_STA;s_radio.next_operation_identity=1;
 wifi_radio_operation_lock();
 assert(wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION,WIFI_MODE_STA,&helper[0])==0);
 assert(wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA,WIFI_MODE_STA,&helper[1])==0);
 wifi_radio_operation_unlock();channel=6;secondary=WIFI_SECOND_CHAN_NONE;peer_error=ESP_ERR_WIFI_NOT_CONNECT;
 start_error=close_error=channel_error=fence_error=0;mutate=true;construct_fail=false;
 s_sc_fence_posted=s_sc_fence_seen=0;memset(&posted_fence,0,sizeof(posted_fence));
}
static void pinned(void){wifi_radio_operation_lock();for(unsigned i=0;i<3;i++)if(helper[i].acquired){wifi_radio_release_locked(&helper[i]);assert(wifi_radio_lease_valid(&helper[i]));}wifi_radio_operation_unlock();}
static void deliver(void){assert(posted_fence.identity);wifi_radio_smartconfig_observe_fence(&posted_fence);}
static void close_fenced(esp32_mquickjs_wifi_radio_operation_t *token,esp32_mquickjs_wifi_smartconfig_radio_status_t *status){
 assert(esp32_mquickjs_wifi_radio_smartconfig_close(token,status)==ESP_ERR_NOT_FINISHED && token->identity);deliver();
 assert(!esp32_mquickjs_wifi_radio_smartconfig_close(token,status) && !token->identity);
}
int main(void){
 setup();esp32_mquickjs_wifi_radio_operation_t token={0};esp32_mquickjs_wifi_smartconfig_radio_status_t status;
 peer_error=ESP_OK;assert(esp32_mquickjs_wifi_radio_smartconfig_begin(helper,false,&options,&token,&status)==ESP_ERR_INVALID_STATE && !token.identity && !starts);peer_error=ESP_ERR_WIFI_NOT_CONNECT;
 construct_fail=true;assert(esp32_mquickjs_wifi_radio_smartconfig_begin(helper,false,&options,&token,&status)==ESP_ERR_NO_MEM && !token.identity);construct_fail=false;
 assert(esp32_mquickjs_wifi_radio_smartconfig_begin(helper,false,&options,&token,&status)==0 && token.identity && channel==11);
 pinned();esp32_mquickjs_wifi_radio_operation_t stale=token;stale.generation++;
 assert(esp32_mquickjs_wifi_radio_smartconfig_close(&stale,&status)==ESP_ERR_INVALID_STATE && !stops);
 esp32_mquickjs_wifi_radio_end_operation(&token);assert(token.identity && s_radio.operation.identity);
 esp32_mquickjs_wifi_radio_lease_t foreign={0};wifi_radio_operation_lock();assert(wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_ESPNOW,WIFI_MODE_STA,&foreign)==ESP_ERR_INVALID_STATE);wifi_radio_operation_unlock();
 assert(esp32_mquickjs_wifi_radio_smartconfig_ack(&token,&status)==ESP_ERR_INVALID_STATE);
 channel_error=77;assert(esp32_mquickjs_wifi_radio_smartconfig_finish_capture(&token,&status)==77 && !status.channel_restored);pinned();
 channel_error=0;fence_error=99;
 assert(esp32_mquickjs_wifi_radio_smartconfig_finish_capture(&token,&status)==ESP_ERR_NOT_FINISHED && status.error==99 && !status.event_fenced);pinned();
 fence_error=0;assert(esp32_mquickjs_wifi_radio_smartconfig_finish_capture(&token,&status)==ESP_ERR_NOT_FINISHED && status.channel_restored && channel==6);
 assert(esp32_mquickjs_wifi_radio_smartconfig_connection_begin(&token)==ESP_ERR_INVALID_STATE);
 wifi_radio_smartconfig_observe_fence(&stale);assert(!s_sc_fence_seen);deliver();
 assert(!esp32_mquickjs_wifi_radio_smartconfig_finish_capture(&token,&status) && status.event_fenced);
 assert(!esp32_mquickjs_wifi_radio_smartconfig_connection_begin(&token));
 assert(esp32_mquickjs_wifi_radio_smartconfig_close(&token,&status)==ESP_ERR_NOT_FINISHED && !stops);
 assert(esp32_mquickjs_wifi_radio_smartconfig_connection_end(&stale)==ESP_ERR_INVALID_STATE);
 assert(!esp32_mquickjs_wifi_radio_smartconfig_connection_end(&token));
 assert(esp32_mquickjs_wifi_radio_smartconfig_connection_begin(&token)==ESP_ERR_INVALID_STATE);
 close_error=88;assert(esp32_mquickjs_wifi_radio_smartconfig_close(&token,&status)==88 && token.identity);pinned();
 close_error=0;assert(esp32_mquickjs_wifi_radio_smartconfig_close(&token,&status)==0 && !token.identity && !s_sc_radio && !s_radio.operation.identity && decoder_releases==1);
 setup();start_error=99;mutate=false;unsigned writes=channel_writes;
 assert(esp32_mquickjs_wifi_radio_smartconfig_begin(helper,false,&options,&token,&status)==99 && token.identity);channel=11;
 close_fenced(&token,&status);assert(channel==11 && channel_writes==writes);
 setup();wifi_radio_operation_lock();assert(wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP,WIFI_MODE_AP,&helper[2])==0);wifi_radio_operation_unlock();s_radio.effective_mode=WIFI_MODE_APSTA;
 assert(esp32_mquickjs_wifi_radio_smartconfig_begin(helper,false,&options,&token,&status)==ESP_ERR_INVALID_STATE && !token.identity);
 assert(esp32_mquickjs_wifi_radio_smartconfig_begin(helper,true,&options,&token,&status)==0);pinned();close_fenced(&token,&status);
 setup();s_radio.next_operation_identity=0;assert(esp32_mquickjs_wifi_radio_smartconfig_begin(helper,false,&options,&token,&status)==ESP_ERR_NO_MEM && !token.identity);
 return 0;
}
'''
