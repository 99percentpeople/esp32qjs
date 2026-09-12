"""Deferred SmartConfig native credential/event ownership regression."""
from pathlib import Path
import re
import unittest

from test_wireless_control_regression import compile_run

ROOT = Path(__file__).resolve().parents[2]


class SmartConfigEvents(unittest.TestCase):
    def test_production_record_and_event_post_boundary(self):
        header = (ROOT / 'components/esp32_mquickjs/internal/esp32_mquickjs_wifi_smartconfig_events.h').read_text()
        source = (ROOT / 'components/esp32_mquickjs/src/modules/wifi_smartconfig/esp32_mquickjs_wifi_smartconfig_events.c').read_text()
        header = re.sub(r'^#(?:include|pragma)[^\n]*\n', '', header, flags=re.M)
        source = re.sub(r'^#include[^\n]*\n', '', source, flags=re.M)
        compile_run(self, BOUNDARIES + header + source + CASES)


BOUNDARIES = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API 1
#define CONFIG_LWIP_IPV4 1
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NO_MEM 3
#define ESP_ERR_INVALID_SIZE 4
#define ESP_ERR_TIMEOUT 5
#define portMUX_INITIALIZER_UNLOCKED 0
typedef int esp_err_t,portMUX_TYPE;
typedef uint32_t TickType_t;
typedef const char *esp_event_base_t;
static const char base_name[]="SC_EVENT";
#define SC_EVENT base_name
enum {SC_EVENT_SCAN_DONE,SC_EVENT_FOUND_CHANNEL,SC_EVENT_GOT_SSID_PSWD,SC_EVENT_SEND_ACK_DONE};
typedef enum {SC_TYPE_ESPTOUCH,SC_TYPE_AIRKISS,SC_TYPE_ESPTOUCH_AIRKISS,SC_TYPE_ESPTOUCH_V2} smartconfig_type_t;
typedef struct {
    uint8_t ssid[32],password[64];bool bssid_set;uint8_t bssid[6];
    smartconfig_type_t type;uint8_t token,cellphone_ip[4];
} smartconfig_event_got_ssid_pswd_t;
static int locked,allocations,frees,posts;
static bool fail_alloc;
static size_t allocation_size;
static const void *forwarded_data;
static esp_event_base_t forwarded_base;
static size_t forwarded_length;
static int32_t forwarded_event;
static TickType_t forwarded_wait;
static uint8_t sdk_config[0xb08];
uint8_t *g_config_data=sdk_config;
#define portENTER_CRITICAL(p) do{(void)(p);assert(!locked);locked=1;}while(0)
#define portEXIT_CRITICAL(p) do{(void)(p);assert(locked);locked=0;}while(0)
static void *heap_caps_calloc(size_t n,size_t size,int caps) {
    assert(!locked && caps==(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
    if(fail_alloc)return NULL;allocations++;allocation_size=n*size;return calloc(n,size);
}
static void heap_caps_free(void *p) {
    assert(!locked && p);for(size_t i=0;i<allocation_size;i++)assert(((uint8_t *)p)[i]==0);
    frees++;free(p);
}
static void esp32_mquickjs_wireless_secure_zero(void *data,size_t size) {
    volatile uint8_t *p=data;while(size--)*p++=0;
}
esp_err_t __real_esp_event_post(esp_event_base_t base,int32_t event,const void *data,size_t len,TickType_t wait) {
    assert(!locked);posts++;forwarded_base=base;forwarded_event=event;
    forwarded_data=data;forwarded_length=len;forwarded_wait=wait;return ESP_ERR_TIMEOUT;
}
'''

CASES = r'''
static bool zero(const void *p,size_t size) {
    const uint8_t *v=p;while(size--)if(*v++)return false;return true;
}
int main(void) {
    esp32_mquickjs_wifi_smartconfig_token_t token={0},other={0};
    smartconfig_event_got_ssid_pswd_t input={0};
    esp32_mquickjs_wifi_smartconfig_credentials_t copy;
    sdk_config[0xa41]=64;for(unsigned i=0;i<64;i++)sdk_config[0xaac+i]=(uint8_t)i;
    sdk_config[0xaac+63]=0; /* A trailing zero is payload, not a terminator. */
    memset(input.ssid,0xa5,sizeof(input.ssid));memset(input.password,0x5a,sizeof(input.password));
    input.type=SC_TYPE_ESPTOUCH_V2;input.bssid_set=true;input.token=2;input.cellphone_ip[0]=192;
    /* Unmanaged events preserve exact SDK result, pointer, size and deadline. */
    assert(__wrap_esp_event_post(SC_EVENT,SC_EVENT_GOT_SSID_PSWD,&input,sizeof(input),UINT32_MAX)==ESP_ERR_TIMEOUT);
    assert(posts==1 && forwarded_data==&input && forwarded_length==sizeof(input) && forwarded_wait==UINT32_MAX);
    assert(esp32_mquickjs_wifi_smartconfig_events_begin(0,&token)==ESP_ERR_INVALID_ARG);
    fail_alloc=true;
    assert(esp32_mquickjs_wifi_smartconfig_events_begin(3,&token)==ESP_ERR_NO_MEM && !token.identity);
    fail_alloc=false;
    assert(esp32_mquickjs_wifi_smartconfig_events_begin(3,&token)==ESP_OK);
    assert(esp32_mquickjs_wifi_smartconfig_events_begin(3,&other)==ESP_ERR_INVALID_STATE && !other.identity);
    assert(esp32_mquickjs_wifi_smartconfig_events_release(&token)==ESP_ERR_INVALID_STATE);
    esp32_mquickjs_wifi_smartconfig_events_status_t status;
    assert(esp32_mquickjs_wifi_smartconfig_events_status(&token,&status)==ESP_OK);
    assert(status.reserved_bytes==sizeof(*s_smartconfig_events) && !status.credentials_received);
    /* Foreign bases still take the original path while SmartConfig is owned. */
    static const char wifi[]="WIFI_EVENT";
    assert(__wrap_esp_event_post(wifi,7,&input,8,13)==ESP_ERR_TIMEOUT);
    assert(posts==2 && forwarded_base==wifi && forwarded_event==7 && forwarded_length==8 && forwarded_wait==13);
    int count=allocations;
    assert(__wrap_esp_event_post(SC_EVENT,SC_EVENT_SCAN_DONE,NULL,0,UINT32_MAX)==ESP_OK);
    assert(__wrap_esp_event_post(SC_EVENT,SC_EVENT_FOUND_CHANNEL,NULL,0,UINT32_MAX)==ESP_OK);
    assert(__wrap_esp_event_post(SC_EVENT,SC_EVENT_GOT_SSID_PSWD,&input,sizeof(input),UINT32_MAX)==ESP_OK);
    assert(allocations==count && posts==2);
    assert(esp32_mquickjs_wifi_smartconfig_events_status(&token,&status)==ESP_OK);
    assert(status.scan_done && status.channel_found && status.credentials_received && status.captured_events==3);
    assert(esp32_mquickjs_wifi_smartconfig_credentials_copy(&token,&copy)==ESP_OK);
    assert(!memcmp(copy.network.ssid,input.ssid,32) && !memcmp(copy.network.password,input.password,64));
    assert(copy.custom_length==64 && copy.custom_data[0]==0 && copy.custom_data[62]==62 && copy.custom_data[63]==0);
    memset(sdk_config,0,sizeof(sdk_config)); /* SDK scrubs immediately after the post returns. */
    g_config_data=NULL; /* The retained result no longer needs the SDK allocation. */
    /* Conversion failure: no commit, so an identical second read can retry. */
    esp32_mquickjs_wireless_secure_zero(&copy,sizeof(copy));
    assert(esp32_mquickjs_wifi_smartconfig_credentials_copy(&token,&copy)==ESP_OK && copy.network.password[0]==0x5a && copy.custom_length==64 && copy.custom_data[62]==62);
    input.password[0]=0;
    assert(__wrap_esp_event_post(SC_EVENT,SC_EVENT_GOT_SSID_PSWD,&input,sizeof(input),0)==ESP_OK);
    assert(s_smartconfig_events->credentials.network.password[0]==0x5a && s_smartconfig_events->status.duplicate_credentials==1);
    assert(esp32_mquickjs_wifi_smartconfig_credentials_commit(&token)==ESP_OK);
    assert(zero(&s_smartconfig_events->credentials,sizeof(copy)));
    assert(esp32_mquickjs_wifi_smartconfig_credentials_copy(&token,&copy)==ESP_ERR_INVALID_STATE);
    assert(esp32_mquickjs_wifi_smartconfig_credentials_commit(&token)==ESP_ERR_INVALID_STATE);
    assert(__wrap_esp_event_post(SC_EVENT,SC_EVENT_GOT_SSID_PSWD,&input,sizeof(input),0)==ESP_OK);
    assert(zero(&s_smartconfig_events->credentials,sizeof(copy)) && s_smartconfig_events->status.duplicate_credentials==2);
    /* ACK observation stays metadata, never substitutes native ACK retirement. */
    assert(__wrap_esp_event_post(SC_EVENT,SC_EVENT_SEND_ACK_DONE,NULL,0,0)==ESP_OK);
    assert(s_smartconfig_events->status.ack_observed);
    other=token;other.radio_generation++;
    assert(esp32_mquickjs_wifi_smartconfig_events_close(&other)==ESP_ERR_INVALID_STATE);
    assert(esp32_mquickjs_wifi_smartconfig_events_close(&token)==ESP_OK);
    assert(esp32_mquickjs_wifi_smartconfig_events_close(&token)==ESP_OK);
    assert(__wrap_esp_event_post(SC_EVENT,SC_EVENT_GOT_SSID_PSWD,&input,sizeof(input),UINT32_MAX)==ESP_OK);
    assert(posts==2 && s_smartconfig_events->status.discarded_events==1);
    other=token;
    /* The harness supplies the externally required decoder retirement proof. */
    assert(esp32_mquickjs_wifi_smartconfig_events_release(&token)==ESP_OK && !token.identity);
    assert(esp32_mquickjs_wifi_smartconfig_events_begin(3,&token)==ESP_OK && token.identity>other.identity);
    assert(__wrap_esp_event_post(SC_EVENT,SC_EVENT_GOT_SSID_PSWD,&input,sizeof(input),0)==ESP_OK);
    assert(!s_smartconfig_events->status.credentials_received && s_smartconfig_events->status.event_error==ESP_ERR_INVALID_STATE);
    g_config_data=sdk_config;sdk_config[0xa41]=65;s_smartconfig_events->status.event_error=ESP_OK;
    assert(__wrap_esp_event_post(SC_EVENT,SC_EVENT_GOT_SSID_PSWD,&input,sizeof(input),0)==ESP_OK);
    assert(!s_smartconfig_events->status.credentials_received && s_smartconfig_events->status.event_error==ESP_ERR_INVALID_SIZE);
    sdk_config[0xa41]=0;
    assert(esp32_mquickjs_wifi_smartconfig_events_release(&other)==ESP_ERR_INVALID_STATE);
    assert(esp32_mquickjs_wifi_smartconfig_credentials_commit(&other)==ESP_ERR_INVALID_STATE);
    assert(__wrap_esp_event_post(SC_EVENT,SC_EVENT_GOT_SSID_PSWD,NULL,sizeof(input),0)==ESP_OK);
    assert(__wrap_esp_event_post(SC_EVENT,SC_EVENT_GOT_SSID_PSWD,&input,sizeof(input)-1,0)==ESP_OK);
    ((uint8_t *)&input)[offsetof(smartconfig_event_got_ssid_pswd_t,bssid_set)]=2;
    assert(__wrap_esp_event_post(SC_EVENT,SC_EVENT_GOT_SSID_PSWD,&input,sizeof(input),0)==ESP_OK);
    input.bssid_set=false;input.type=(smartconfig_type_t)99;
    assert(__wrap_esp_event_post(SC_EVENT,SC_EVENT_GOT_SSID_PSWD,&input,sizeof(input),0)==ESP_OK);
    assert(!s_smartconfig_events->status.credentials_received && s_smartconfig_events->status.event_error==ESP_ERR_INVALID_SIZE);
    input.type=SC_TYPE_AIRKISS;
    assert(__wrap_esp_event_post(SC_EVENT,SC_EVENT_GOT_SSID_PSWD,&input,sizeof(input),0)==ESP_OK);
    assert(esp32_mquickjs_wifi_smartconfig_events_close(&token)==ESP_OK);
    assert(zero(&s_smartconfig_events->credentials,sizeof(copy)));
    assert(esp32_mquickjs_wifi_smartconfig_events_release(&token)==ESP_OK);
    assert(allocations==frees);
    /* Native allocator failure is sticky control state, independent of post. */
    esp32_mquickjs_wifi_smartconfig_allocation_failed(); /* No owner: no record. */
    assert(esp32_mquickjs_wifi_smartconfig_events_begin(3,&token)==ESP_OK);
    assert(s_smartconfig_events->status.allocation_error==ESP_OK);
    assert(__wrap_esp_event_post(SC_EVENT,SC_EVENT_GOT_SSID_PSWD,&input,sizeof(input),0)==ESP_OK);
    count=allocations;int old_posts=posts;
    esp32_mquickjs_wifi_smartconfig_allocation_failed();
    esp32_mquickjs_wifi_smartconfig_allocation_failed();
    assert(allocations==count && posts==old_posts);
    assert(esp32_mquickjs_wifi_smartconfig_events_status(&token,&status)==ESP_OK && status.allocation_error==ESP_ERR_NO_MEM);
    assert(esp32_mquickjs_wifi_smartconfig_credentials_copy(&token,&copy)==ESP_ERR_NO_MEM);
    assert(esp32_mquickjs_wifi_smartconfig_credentials_commit(&token)==ESP_ERR_NO_MEM);
    assert(!s_smartconfig_events->status.credentials_consumed);
    assert(__wrap_esp_event_post(SC_EVENT,SC_EVENT_FOUND_CHANNEL,NULL,0,0)==ESP_OK);
    assert(!s_smartconfig_events->status.channel_found && s_smartconfig_events->status.discarded_events==1);
    assert(esp32_mquickjs_wifi_smartconfig_events_close(&token)==ESP_OK);
    assert(zero(&s_smartconfig_events->credentials,sizeof(copy)));
    assert(esp32_mquickjs_wifi_smartconfig_events_release(&token)==ESP_OK);
    assert(esp32_mquickjs_wifi_smartconfig_events_begin(3,&token)==ESP_OK);
    assert(s_smartconfig_events->status.allocation_error==ESP_OK);
    assert(esp32_mquickjs_wifi_smartconfig_events_close(&token)==ESP_OK);
    esp32_mquickjs_wifi_smartconfig_allocation_failed(); /* Closing: no new error. */
    assert(s_smartconfig_events->status.allocation_error==ESP_OK);
    assert(esp32_mquickjs_wifi_smartconfig_events_release(&token)==ESP_OK);
    s_smartconfig_last_identity=UINT64_MAX;
    assert(esp32_mquickjs_wifi_smartconfig_events_begin(3,&token)==ESP_ERR_NO_MEM && !token.identity);
    assert(allocations==frees);
    esp32_mquickjs_wireless_secure_zero(&input,sizeof(input));
    esp32_mquickjs_wireless_secure_zero(&copy,sizeof(copy));
    return 0;
}
'''
