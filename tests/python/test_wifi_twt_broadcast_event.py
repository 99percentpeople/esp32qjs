"""Deferred production broadcast event normalization and synchronous capture.

No fixture import, compilation or execution until Wi-Fi staged validation.
SDK field declarations are read from the pinned SDK; production static asserts
and target builds, rather than Host layout, establish the C5 ABI.
"""
from pathlib import Path
import re
import unittest
from test_wifi_config_controls import structure
from test_wifi_driver_phy import COMPONENT
from test_wifi_rx_target import unit
from test_wifi_twt_setup_timer import PRELUDE
from test_wireless_control_regression import compile_run


def broadcast_types(commands=True):
    sdk = Path('/home/zach/esp/esp-idf/components/esp_wifi/include/esp_wifi_he_types.h').read_text()
    names = ['wifi_btwt_setup_status_t', 'wifi_btwt_teardown_status_t']
    if commands:
        names.insert(0, 'wifi_twt_setup_cmds_t')
    code = ''.join(re.search(r'typedef enum \{[^}]*\} ' + name + ';', sdk).group(0) for name in names)
    for name in ('wifi_event_sta_btwt_setup_t', 'wifi_event_sta_btwt_teardown_t'):
        code += structure(sdk, name)
    return code


BOUNDARIES = r'''
void esp32_mquickjs_wifi_twt_setup_results_connection_closed_native(void);
void esp32_mquickjs_wifi_twt_setup_timers_connection_closed_native(void);
void esp32_mquickjs_wifi_twt_information_timers_connection_closed_native(void);
#define ESP_FAIL (-1)
#define WIFI_EVENT_BTWT_SETUP 33
#define WIFI_EVENT_BTWT_TEARDOWN 34
#define WIFI_EVENT ((const char *)19)
#define ESP_EVENT_DECLARE_BASE(name)
#define ESP32QJS_WIFI_RADIO_CONTROL_EVENT ((const char *)20)
typedef void *TaskHandle_t;
static TaskHandle_t current_task=(void *)1;
static TaskHandle_t xTaskGetCurrentTaskHandle(void){return current_task;}
static esp_err_t esp_event_post(const char *,int32_t,const void *,size_t,unsigned);
'''


class WiFiTwtBroadcastEvent(unittest.TestCase):
    def test_scopes_padding_duplicates_foreign_task_and_saturation(self):
        code = PRELUDE + broadcast_types() + BOUNDARIES
        code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_twt_broadcast_event.h')
        code += unit(COMPONENT / 'src/modules/wifi_twt/esp32_mquickjs_wifi_twt_broadcast_event.c')
        compile_run(self, code + MAIN)


MAIN = r'''
static unsigned posts;
static esp_err_t post_error;
static wifi_event_sta_btwt_setup_t observed;
static esp_err_t esp_event_post(const char *base,int32_t id,const void *data,size_t size,unsigned wait) {
    assert(!locked && base==WIFI_EVENT && wait==0);++posts;
    if(id==WIFI_EVENT_BTWT_SETUP){assert(size==sizeof(observed));memcpy(&observed,data,size);}
    else assert(id==WIFI_EVENT_BTWT_TEARDOWN && size==sizeof(wifi_event_sta_btwt_teardown_t));
    return post_error;
}
int main(void) {
    esp32_mquickjs_wifi_btwt_event_scope_t outer,inner;
    wifi_event_sta_btwt_setup_t event;
    memset(&event,0xa5,sizeof(event));event.status=BTWT_SETUP_TIMEOUT;event.btwt_id=3;
    assert(esp32_mquickjs_wifi_btwt_event_begin(&outer,WIFI_EVENT_BTWT_SETUP,3));
    assert(!esp32_mquickjs_wifi_btwt_event_begin(&outer,WIFI_EVENT_BTWT_SETUP,3));
    assert(!esp32_mquickjs_wifi_btwt_event_post(WIFI_EVENT_BTWT_SETUP,&event,sizeof(event)));
    assert(outer.seen && !posts && !outer.event.setup.reason && !outer.event.setup.target_wake_time);
    /* Padding and irrelevant bytes do not turn a repeated event into conflict. */
    memset(&event,0x5a,sizeof(event));event.status=BTWT_SETUP_TIMEOUT;event.btwt_id=3;
    assert(!esp32_mquickjs_wifi_btwt_event_post(WIFI_EVENT_BTWT_SETUP,&event,sizeof(event)) && !outer.ambiguous);
    assert(esp32_mquickjs_wifi_btwt_event_begin(&inner,WIFI_EVENT_BTWT_TEARDOWN,32));
    assert(!esp32_mquickjs_wifi_btwt_event_end(&outer));
    wifi_event_sta_btwt_teardown_t down={.btwt_id=32,.status=BTWT_TEARDOWN_SUCCESS};
    assert(!esp32_mquickjs_wifi_btwt_event_post(WIFI_EVENT_BTWT_TEARDOWN,&down,sizeof(down)) && inner.seen && !posts);
    assert(esp32_mquickjs_wifi_btwt_event_end(&inner));
    current_task=(void *)2;
    assert(!esp32_mquickjs_wifi_btwt_event_begin(&inner,WIFI_EVENT_BTWT_SETUP,3));
    assert(!esp32_mquickjs_wifi_btwt_event_end(&outer));
    assert(!esp32_mquickjs_wifi_btwt_event_post(WIFI_EVENT_BTWT_SETUP,&event,sizeof(event)) && posts==1);
    current_task=(void *)1;event.btwt_id=4;
    assert(!esp32_mquickjs_wifi_btwt_event_post(WIFI_EVENT_BTWT_SETUP,&event,sizeof(event)) && posts==2);
    event.btwt_id=3;event.status=BTWT_SETUP_TXFAIL;event.reason=77;
    assert(esp32_mquickjs_wifi_btwt_event_post(WIFI_EVENT_BTWT_SETUP,&event,sizeof(event))==ESP_ERR_INVALID_STATE);
    assert(outer.ambiguous && outer.event.setup.status==BTWT_SETUP_TIMEOUT);
    assert(esp32_mquickjs_wifi_btwt_event_end(&outer) && !s_btwt_event_scope && !s_btwt_event_task);
    post_error=81;assert(esp32_mquickjs_wifi_btwt_event_publish(&outer)==81 && posts==3 && !observed.reason);
    post_error=0;assert(esp32_mquickjs_wifi_btwt_event_post(WIFI_EVENT_BTWT_SETUP,&event,1)==ESP_ERR_INVALID_ARG && posts==3);
    assert(esp32_mquickjs_wifi_btwt_event_begin(&outer,WIFI_EVENT_BTWT_SETUP,3));
    assert(esp32_mquickjs_wifi_btwt_event_post(WIFI_EVENT_BTWT_SETUP,NULL,sizeof(event))==ESP_ERR_INVALID_ARG);
    assert(outer.error==ESP_ERR_INVALID_ARG && !outer.seen);assert(esp32_mquickjs_wifi_btwt_event_end(&outer));
    event=(wifi_event_sta_btwt_setup_t){.status=BTWT_SETUP_SUCCESS,.setup_cmd=TWT_ACCEPT,.btwt_id=3,
        .min_wake_dura=1,.wake_invl_mant=1,.wake_invl_expn=31,.trigger=true,.flow_type=1,.target_wake_time=UINT64_MAX};
    assert(!esp32_mquickjs_wifi_btwt_event_post(WIFI_EVENT_BTWT_SETUP,&event,sizeof(event)));
    assert(observed.target_wake_time==UINT64_MAX && observed.trigger);
    ((uint8_t *)&event)[14]=2;
    assert(esp32_mquickjs_wifi_btwt_event_post(WIFI_EVENT_BTWT_SETUP,&event,sizeof(event))==ESP_ERR_INVALID_ARG);
    assert(!locked);return 0;
}
'''
