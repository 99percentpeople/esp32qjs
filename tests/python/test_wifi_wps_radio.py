"""Deferred production WPS Radio restoration/admission checks; AST only this wave.

The actual Radio functions, lease release, semantic config comparison and secure
zero helper run with controllable driver/worker/event boundaries. This does not
prove native driver normalization, Flash persistence, RF or Station helper drain.
"""
import os
from pathlib import Path
import re
import unittest
from test_wifi_vendor_ie import vendor_code, COMPONENT
from test_wifi_config_controls import sdk_types, structure
from test_wifi_rx_target import unit
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import CORE, extract


class WPSRadio(unittest.TestCase):
    def test_production_restore_suffix_and_exact_owners(self):
        sdk = os.environ.get('IDF_PATH')
        if not sdk:
            self.skipTest('Set IDF_PATH to the reviewed ESP-IDF')
        radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
        header = (COMPONENT / 'internal/esp32_mquickjs_wifi_radio.h').read_text()
        code = '#define CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API 1\n#define CONFIG_LWIP_IPV4 1\n' + vendor_code('esp32c5/representative')
        before = sdk_types('esp32c5/representative')
        more = sdk_types('esp32c5/representative', ('wifi_ap_record_t', 'wifi_second_chan_t'))
        assert more.startswith(before)
        extra = more[len(before):] + TYPES
        extra += re.search(r'typedef enum \{[^}]*\} esp32_mquickjs_wifi_radio_operation_kind_t;', header).group(0)
        extra += structure(header, 'esp32_mquickjs_wifi_radio_operation_t')
        extra += unit(Path(sdk) / 'components/wpa_supplicant/esp_supplicant/include/esp_wps.h')
        for name in ('sdk', 'worker', 'radio'):
            extra += unit(COMPONENT / 'internal' / ('esp32_mquickjs_wifi_wps_' + name + '.h'))
        extra += structure(radio, 'wifi_radio_wps_t') + GLOBALS
        code = code.replace('static struct {\n    int lock;', extra + '\nstatic struct {\n    int lock;', 1)
        code = code.replace('struct {unsigned identity,lease_identity;} operation;', 'esp32_mquickjs_wifi_radio_operation_t operation;')
        code = code.replace('generation,next_lease_identity,next_lifecycle_identity,wake_locks;', 'generation,next_operation_identity,next_lease_identity,next_lifecycle_identity,wake_locks;')
        code += BOUNDARIES
        code += extract((CORE / 'esp32_mquickjs_wireless_core.c').read_text(), 'esp32_mquickjs_wireless_secure_zero')
        for name in ('wifi_radio_wps_observe_fence', 'wifi_radio_connection_owner_locked',
                     'wifi_radio_config_equal', 'esp32_mquickjs_wifi_radio_end_operation'):
            code += extract(radio, name)
        code += '\n#define calloc binding_calloc\n#define free binding_free\n'
        code += (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_wps_radio.inc').read_text()
        code += '\n#undef calloc\n#undef free\n'
        compile_run(self, code + CASES)


TYPES = r'''
#include <stdlib.h>
#define ESP_ERR_WIFI_NOT_CONNECT -20
#define ESP_ERR_WIFI_NOT_STARTED -21
#define ESP_ERR_INVALID_RESPONSE -22
#define ESP_ERR_NOT_FINISHED -23
'''

GLOBALS = r'''
static wifi_radio_wps_t *s_wps_radio;
static struct {uint32_t owners[3];} *s_sc_radio;
static uint32_t s_wps_fence_posted,s_wps_fence_seen;
#define WIFI_RADIO_DPP_PENDING false
#define WIFI_RADIO_WPS_PENDING (s_wps_radio != NULL)
#define WIFI_RADIO_SMARTCONFIG_PENDING (s_sc_radio != NULL)
#define WIFI_RADIO_EAP_PENDING false
#define WIFI_RADIO_WPS_FENCE_EVENT 8
#define ESP32QJS_WIFI_RADIO_CONTROL_EVENT 1
'''

BOUNDARIES = r'''
struct esp32_mquickjs_wifi_wps_worker {esp32_mquickjs_wifi_wps_worker_status_t status;bool prepared;};
static struct esp32_mquickjs_wifi_wps_worker worker;
static wifi_config_t original,actual;
static wifi_storage_t storage;
static uint8_t channel;
static wifi_second_chan_t secondary;
static int prepare_error,start_error,close_error,storage_error,config_error,channel_error,fence_error;
static int peer_error=ESP_ERR_WIFI_NOT_CONNECT;
static bool corrupt_config,fail_binding;
static unsigned allocations,worker_live,prepares,starts,config_writes,storage_writes,channel_writes,releases;
static esp32_mquickjs_wifi_radio_operation_t posted;
static void *binding_calloc(size_t count,size_t size) {
    assert(locks && !critical && count==1 && size==sizeof(wifi_radio_wps_t));
    if(fail_binding)return NULL;void *p=calloc(count,size);assert(p);allocations++;return p;
}
static void binding_free(void *p) {
    assert(allocations);for(size_t i=0;i<sizeof(wifi_radio_wps_t);i++)assert(!((uint8_t*)p)[i]);
    allocations--;free(p);
}
static int esp_wifi_sta_get_ap_info(wifi_ap_record_t *out) {
    assert(locks && !critical);memset(out,0,sizeof(*out));return peer_error;
}
static int esp_wifi_get_config(wifi_interface_t itf,wifi_config_t *out) {
    assert(locks && !critical && itf==WIFI_IF_STA);*out=actual;
    if(corrupt_config)out->sta.password[0]^=1;return 0;
}
static int (esp_wifi_set_config)(wifi_interface_t itf,const wifi_config_t *value) {
    assert(locks && !critical && itf==WIFI_IF_STA && storage==WIFI_STORAGE_RAM);
    config_writes++;if(config_error)return config_error;actual=*value;return 0;
}
static int (esp_wifi_set_storage)(wifi_storage_t value) {
    assert(locks && !critical && worker.prepared);storage_writes++;
    if(storage_error)return storage_error;storage=value;return 0;
}
static int wifi_radio_get_channel_locked(uint8_t *primary,wifi_second_chan_t *second,uint32_t *generation) {
    assert(locks && !critical);*primary=channel;*second=secondary;*generation=1;return 0;
}
static int wifi_radio_validate_regulatory_channel(uint8_t primary) {
    assert(locks && !critical);return primary==6?0:ESP_ERR_INVALID_ARG;
}
static int (esp_wifi_set_channel)(uint8_t primary,wifi_second_chan_t second) {
    assert(locks && !critical);channel_writes++;if(channel_error)return channel_error;
    channel=primary;secondary=second;return 0;
}
static int esp_event_post(int base,int id,const void *data,size_t size,unsigned wait) {
    assert(locks && !critical && base==ESP32QJS_WIFI_RADIO_CONTROL_EVENT && id==8 && size==sizeof(posted) && !wait);
    if(fence_error)return fence_error;memcpy(&posted,data,size);return 0;
}
esp_err_t esp32_mquickjs_wifi_wps_worker_create(const esp_wps_config_t *config,esp32_mquickjs_wifi_wps_worker_t **out) {
    assert(!locks && !critical && !*out && !worker_live);memset(&worker,0,sizeof(worker));
    if(config->wps_type!=WPS_TYPE_PIN)return ESP_ERR_INVALID_ARG;
    worker_live++;*out=&worker;return 0;
}
esp_err_t esp32_mquickjs_wifi_wps_worker_prepare(esp32_mquickjs_wifi_wps_worker_t *w) {
    assert(locks && !critical && w==&worker && storage==WIFI_STORAGE_FLASH);
    prepares++;worker.prepared=true;return prepare_error;
}
esp_err_t esp32_mquickjs_wifi_wps_worker_start(esp32_mquickjs_wifi_wps_worker_t *w) {
    assert(locks && !critical && w->prepared && storage==WIFI_STORAGE_RAM);
    starts++;memcpy(actual.sta.ssid,"negotiation",12);memset(actual.sta.password,0,sizeof(actual.sta.password));channel=11;
    return start_error;
}
esp_err_t esp32_mquickjs_wifi_wps_worker_status(esp32_mquickjs_wifi_wps_worker_t *w,esp32_mquickjs_wifi_wps_worker_status_t *out) {
    assert(locks && !critical && w==&worker);*out=w->status;return 0;
}
esp_err_t esp32_mquickjs_wifi_wps_worker_finish_capture(esp32_mquickjs_wifi_wps_worker_t *w) {
    assert(locks && !critical && w==&worker);if(close_error)return close_error;
    w->status.capture_retired=true;return 0;
}
esp_err_t esp32_mquickjs_wifi_wps_worker_close(esp32_mquickjs_wifi_wps_worker_t *w) {
    assert(locks && !critical && w==&worker);if(close_error)return close_error;
    w->status.capture_retired=w->status.retired=true;return 0;
}
esp_err_t esp32_mquickjs_wifi_wps_worker_release(esp32_mquickjs_wifi_wps_worker_t **w) {
    assert(locks && !critical && *w==&worker && worker.status.retired && worker_live);
    releases++;worker_live--;*w=NULL;return 0;
}
esp_err_t esp32_mquickjs_wifi_wps_worker_pin(esp32_mquickjs_wifi_wps_worker_t *w,uint8_t pin[8],bool commit) {
    assert(locks && !critical && w==&worker);if(!commit)memcpy(pin,"12345670",8);return 0;
}
esp_err_t esp32_mquickjs_wifi_wps_worker_credentials(esp32_mquickjs_wifi_wps_worker_t *w,esp32_mquickjs_wifi_wps_credentials_t *out,bool commit) {
    assert(locks && !critical && w==&worker && w->status.capture_retired && storage==WIFI_STORAGE_FLASH);
    if(!commit){memset(out,0,sizeof(*out));out->count=1;}return 0;
}
'''

CASES = r'''
static esp32_mquickjs_wifi_radio_lease_t helper[3];
static esp_wps_config_t options={.wps_type=WPS_TYPE_PIN};
static void setup(void) {
    assert(!s_wps_radio && !allocations && !worker_live);reset_vendor();memset(helper,0,sizeof(helper));
    s_radio.effective_mode=WIFI_MODE_STA;s_radio.next_operation_identity=1;s_radio.storage=WIFI_STORAGE_FLASH;
    wifi_radio_operation_lock();
    assert(!wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION,WIFI_MODE_STA,&helper[0]));
    assert(!wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA,WIFI_MODE_STA,&helper[1]));
    wifi_radio_operation_unlock();
    storage=WIFI_STORAGE_FLASH;channel=6;secondary=WIFI_SECOND_CHAN_NONE;
    memset(&original,0,sizeof(original));memcpy(original.sta.ssid,"original",9);memcpy(original.sta.password,"secret",7);
    actual=original;prepare_error=start_error=close_error=storage_error=config_error=channel_error=fence_error=0;
    peer_error=ESP_ERR_WIFI_NOT_CONNECT;corrupt_config=fail_binding=false;
    prepares=starts=config_writes=storage_writes=channel_writes=releases=0;
    s_wps_fence_posted=s_wps_fence_seen=0;memset(&posted,0,sizeof(posted));
}
static void pinned(void) {
    wifi_radio_operation_lock();for(unsigned i=0;i<3;i++)if(helper[i].acquired) {
        wifi_radio_release_locked(&helper[i]);assert(wifi_radio_lease_valid(&helper[i]));
    }wifi_radio_operation_unlock();
}
static void deliver(void) {
    assert(posted.identity);wifi_radio_operation_lock();wifi_radio_wps_observe_fence(&posted);wifi_radio_operation_unlock();
}
static void close_fenced(esp32_mquickjs_wifi_radio_operation_t *token,esp32_mquickjs_wifi_wps_radio_status_t *status) {
    assert(esp32_mquickjs_wifi_radio_wps_close(token,status)==ESP_ERR_NOT_FINISHED && token->identity);
    deliver();assert(!esp32_mquickjs_wifi_radio_wps_close(token,status) && !token->identity && !s_wps_radio);
}
int main(void) {
    setup();esp32_mquickjs_wifi_radio_operation_t token={0};esp32_mquickjs_wifi_wps_radio_status_t status;
    peer_error=0;assert(esp32_mquickjs_wifi_radio_wps_begin(helper,false,&options,&token,&status)==ESP_ERR_INVALID_STATE && !prepares && !token.identity);
    peer_error=ESP_ERR_WIFI_NOT_CONNECT;fail_binding=true;
    assert(esp32_mquickjs_wifi_radio_wps_begin(helper,false,&options,&token,&status)==ESP_ERR_NO_MEM && !allocations && !worker_live);fail_binding=false;
    unsigned released_before_start=releases;
    assert(!esp32_mquickjs_wifi_radio_wps_begin(helper,false,&options,&token,&status) && starts==1 && storage==WIFI_STORAGE_RAM);
    pinned();esp32_mquickjs_wifi_radio_operation_t stale=token;stale.generation++;
    assert(esp32_mquickjs_wifi_radio_wps_close(&stale,&status)==ESP_ERR_INVALID_STATE);
    esp32_mquickjs_wifi_radio_end_operation(&token);assert(token.identity && s_radio.operation.identity);
    esp32_mquickjs_wifi_radio_lease_t foreign={0};wifi_radio_operation_lock();
    assert(wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_ESPNOW,WIFI_MODE_STA,&foreign)==ESP_ERR_INVALID_STATE);wifi_radio_operation_unlock();
    esp32_mquickjs_wifi_wps_credentials_t credentials;
    assert(esp32_mquickjs_wifi_radio_wps_credentials(&token,&credentials,false)==ESP_ERR_INVALID_STATE);
    close_error=600;assert(esp32_mquickjs_wifi_radio_wps_close(&token,&status)==600 && !config_writes);pinned();
    close_error=0;config_error=601;assert(esp32_mquickjs_wifi_radio_wps_close(&token,&status)==601 && !channel_writes && storage==WIFI_STORAGE_RAM);pinned();
    config_error=0;corrupt_config=true;
    assert(esp32_mquickjs_wifi_radio_wps_close(&token,&status)==ESP_ERR_INVALID_RESPONSE && !status.config_restored);
    corrupt_config=false;channel_error=602;
    assert(esp32_mquickjs_wifi_radio_wps_close(&token,&status)==602 && status.config_restored && storage==WIFI_STORAGE_RAM);
    unsigned writes=config_writes;channel_error=0;storage_error=603;
    assert(esp32_mquickjs_wifi_radio_wps_close(&token,&status)==603 && status.channel_restored && !s_radio.storage_configured);
    storage_error=0;fence_error=604;
    assert(esp32_mquickjs_wifi_radio_wps_close(&token,&status)==604 && status.storage_restored && releases==released_before_start);
    assert(config_writes==writes && storage==WIFI_STORAGE_FLASH && !memcmp(&actual,&original,sizeof(actual)));pinned();
    fence_error=0;assert(esp32_mquickjs_wifi_radio_wps_close(&token,&status)==ESP_ERR_NOT_FINISHED);
    wifi_radio_operation_lock();wifi_radio_wps_observe_fence(&stale);wifi_radio_operation_unlock();assert(!s_wps_fence_seen);
    deliver();assert(!esp32_mquickjs_wifi_radio_wps_close(&token,&status) && !allocations && !worker_live && !token.identity);

    setup();prepare_error=701;
    assert(esp32_mquickjs_wifi_radio_wps_begin(helper,false,&options,&token,&status)==701 && token.identity && !storage_writes && !starts);
    close_fenced(&token,&status);assert(!config_writes && !channel_writes && !storage_writes);
    setup();storage_error=702;
    assert(esp32_mquickjs_wifi_radio_wps_begin(helper,false,&options,&token,&status)==702 && token.identity && !starts && !s_radio.storage_configured);
    storage_error=0;close_fenced(&token,&status);assert(!config_writes && !channel_writes && storage==WIFI_STORAGE_FLASH);

    setup();assert(!esp32_mquickjs_wifi_radio_wps_begin(helper,false,&options,&token,&status));
    assert(esp32_mquickjs_wifi_radio_wps_finish_capture(&token,&status)==ESP_ERR_NOT_FINISHED);deliver();
    assert(!esp32_mquickjs_wifi_radio_wps_finish_capture(&token,&status));
    assert(!esp32_mquickjs_wifi_radio_wps_credentials(&token,&credentials,false) && credentials.count==1);
    assert(!esp32_mquickjs_wifi_radio_wps_credentials(&token,NULL,true));
    assert(!esp32_mquickjs_wifi_radio_wps_prepare_close(&token,&status) && token.identity && s_wps_radio);
    pinned();
    assert(!esp32_mquickjs_wifi_radio_wps_close(&token,&status) && !allocations);

    setup();wifi_radio_operation_lock();
    assert(!wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP,WIFI_MODE_AP,&helper[2]));
    wifi_radio_operation_unlock();s_radio.effective_mode=WIFI_MODE_APSTA;
    assert(esp32_mquickjs_wifi_radio_wps_begin(helper,false,&options,&token,&status)==ESP_ERR_INVALID_STATE && !prepares);
    assert(!esp32_mquickjs_wifi_radio_wps_begin(helper,true,&options,&token,&status));pinned();close_fenced(&token,&status);
    setup();s_radio.next_operation_identity=0;
    assert(esp32_mquickjs_wifi_radio_wps_begin(helper,false,&options,&token,&status)==ESP_ERR_NO_MEM && !prepares && !token.identity);
    return 0;
}
'''
