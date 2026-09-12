"""Deferred production FTM Radio admission/report retirement schedule.

Extracts actual production functions and registry. Only SDK calls, timer/event
scheduling and lock/storage boundaries are injected. Does not model another FSM,
prove the SDK timer/IPC implementation, or execute the future public JS adapter.
This file is written and AST parsed only until the Wi-Fi stage test gate opens.
"""
import re
import unittest
from test_wifi_action_radio import radio_code
from test_wifi_config_controls import sdk_types
from test_wifi_driver_phy import COMPONENT
from test_wifi_rx_target import unit
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import extract


def ftm_code(profile, ap):
    source = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
    code = re.sub(r'\bowners\(', 'action_fixture_owners(', radio_code(profile, ap))
    extra = sdk_types(profile, ('wifi_ftm_initiator_cfg_t', 'wifi_event_ftm_report_t', 'wifi_ftm_report_entry_t'))
    declarations = extra[len(sdk_types(profile)):]
    declarations += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_ftm_radio.h')
    declarations += TIMER_TYPES
    for pattern in (r'static struct \{\n    esp32_mquickjs_wifi_ftm_state_t[^}]*\} s_ftm;',
                    r'static struct \{\n    esp_timer_handle_t[^}]*\} s_ftm_timer_fence;',
                    r'typedef struct \{[^}]*\} wifi_radio_ftm_fence_t;'):
        declarations += re.search(pattern, source).group(0) + '\n'
    index = code.index('static struct {\n    esp32_mquickjs_wifi_action_lane_t')
    code = code[:index] + declarations + code[index:]
    code = '#define CONFIG_ESP_WIFI_FTM_ENABLE 1\n#define CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT 1\n' + code
    code += BOUNDARIES
    for name in ('wifi_radio_ftm_event', 'wifi_radio_ftm_exact_locked', 'wifi_radio_ftm_recovery_exact_locked', 'wifi_radio_ftm_admit_locked',
                 'esp32_mquickjs_wifi_ftm_config_valid', 'esp32_mquickjs_wifi_radio_ftm_start', 'esp32_mquickjs_wifi_radio_ftm_end',
                 'esp32_mquickjs_wifi_radio_ftm_snapshot', 'esp32_mquickjs_wifi_radio_ftm_status',
                 'wifi_radio_ftm_timer_fence_callback', 'wifi_radio_ftm_timer_fence_locked',
                 'wifi_radio_ftm_fence_locked', 'esp32_mquickjs_wifi_radio_ftm_collect',
                 'esp32_mquickjs_wifi_radio_ftm_retire'):
        code += extract(source, name)
    return code


class WiFiFtmRadio(unittest.TestCase):
    def test_exact_owner_report_transfer_and_control_fences(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            for ap in (False, True):
                with self.subTest(profile=profile, softap=ap):
                    compile_run(self, ftm_code(profile, ap) + MAIN)


TIMER_TYPES = r'''
#include <stdatomic.h>
typedef void *esp_timer_handle_t;
typedef struct {void (*callback)(void *);void *arg;int dispatch_method;const char *name;} esp_timer_create_args_t;
#define ESP_TIMER_TASK 0
'''

BOUNDARIES = r'''
#define WIFI_EVENT_FTM_REPORT 17
#define ESP_ERR_INVALID_RESPONSE -3
static unsigned ftm_start_calls,ftm_end_calls,report_calls,timer_creates,timer_starts,ftm_posts;
static int ftm_start_error,ftm_end_error,report_error,timer_error,ftm_post_error;
static bool ftm_early,report_race,ftm_queued,timer_pending;
static wifi_radio_ftm_fence_t ftm_marker;
static esp_timer_create_args_t timer_args;
static void wifi_radio_ftm_event(int32_t,const void *);
static void ftm_event(void) {
    wifi_event_ftm_report_t event={.status=FTM_STATUS_SUCCESS,.rtt_raw=10,.rtt_est=12,.dist_est=30,.ftm_report_num_entries=70};
    memcpy(event.peer_mac,s_ftm.config.resp_mac,6);wifi_radio_ftm_event(WIFI_EVENT_FTM_REPORT,&event);
}
#undef esp_wifi_ftm_initiate_session
#undef esp_wifi_ftm_end_session
static int esp_wifi_ftm_initiate_session(wifi_ftm_initiator_cfg_t *config) {
    assert(locks && !critical && config==&s_ftm.config && s_ftm.lease.acquired && s_ftm.state.dispatching);
    ++ftm_start_calls;if(ftm_early)ftm_event();return ftm_start_error;
}
static int esp_wifi_ftm_end_session(void){assert(locks && !critical);++ftm_end_calls;return ftm_end_error;}
#define esp_wifi_ftm_initiate_session(...) WIFI_RADIO_MUTATION(esp_wifi_ftm_initiate_session(__VA_ARGS__))
#define esp_wifi_ftm_end_session(...) WIFI_RADIO_MUTATION(esp_wifi_ftm_end_session(__VA_ARGS__))
static int esp_wifi_ftm_get_report(wifi_ftm_report_entry_t *entries,uint8_t count) {
    assert(locks && !critical && s_ftm.state.sdk_fenced && count<=64);++report_calls;
    if(report_error)return report_error;
    if(entries)for(unsigned i=0;i<count;i++)entries[i]=(wifi_ftm_report_entry_t){.rtt=i,.t1=UINT64_MAX-i,.rssi=-30};
    else assert(!count);
    if(report_race)ftm_event();return 0;
}
static int esp_timer_create(const esp_timer_create_args_t *args,esp_timer_handle_t *out) {
    assert(locks && !critical && !*out);++timer_creates;if(timer_error)return timer_error;
    timer_args=*args;*out=(void *)1;return 0;
}
static int esp_timer_start_once(esp_timer_handle_t handle,uint64_t delay) {
    assert(locks && !critical && handle && delay && !timer_pending);++timer_starts;
    if(timer_error)return timer_error;timer_pending=true;return 0;
}
static int ftm_event_post(esp_event_base_t base,int32_t id,const void *data,size_t bytes,unsigned ticks) {
    assert(locks && !critical && base==7 && id==3 && bytes==sizeof(ftm_marker) && !ticks);++ftm_posts;
    if(ftm_post_error)return ftm_post_error;ftm_marker=*(const wifi_radio_ftm_fence_t *)data;ftm_queued=true;return 0;
}
#define esp_event_post ftm_event_post
'''

MAIN = r'''
static void reset_ftm(void) {
    reset_vendor();s_radio.next_operation_identity=1;s_radio.effective_mode=WIFI_MODE_STA;
    memset(&s_ftm,0,sizeof(s_ftm));memset(&s_ftm_timer_fence,0,sizeof(s_ftm_timer_fence));
    ftm_start_calls=ftm_end_calls=report_calls=timer_creates=timer_starts=ftm_posts=0;
    ftm_start_error=ftm_end_error=report_error=timer_error=ftm_post_error=0;
    ftm_early=report_race=ftm_queued=timer_pending=false;quiescent_error=0;
}
static void terminal(void){wifi_radio_operation_lock();ftm_event();wifi_radio_operation_unlock();}
static void timer_fire(void){assert(timer_pending);timer_pending=false;timer_args.callback(timer_args.arg);}
static void marker_fire(void){assert(ftm_queued);wifi_radio_operation_lock();wifi_radio_lifecycle_fence(NULL,7,3,&ftm_marker);wifi_radio_operation_unlock();ftm_queued=false;}
static unsigned owners(void){return s_radio.clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_FTM];}
int main(void) {
    wifi_ftm_initiator_cfg_t cfg={.resp_mac={2,3,4,5,6,7},.channel=6,.frm_count=64,.burst_period=2};
    esp32_mquickjs_wifi_ftm_token_t token={0},old,other={0};esp32_mquickjs_wifi_ftm_state_t state;
    wifi_ftm_report_entry_t entries[64];
    reset_ftm();cfg.frm_count=63;
    assert(esp32_mquickjs_wifi_radio_ftm_start(&cfg,&token)==ESP_ERR_INVALID_ARG && !ftm_start_calls);
    cfg.frm_count=64;cfg.burst_period=1;
    assert(esp32_mquickjs_wifi_radio_ftm_start(&cfg,&token)==ESP_ERR_INVALID_ARG && !ftm_start_calls && !owners());
    cfg.burst_period=2;cfg.channel=1;connected=true;
    assert(esp32_mquickjs_wifi_radio_ftm_start(&cfg,&token)==ESP_ERR_INVALID_STATE && !owners());
    connected=false;cfg.channel=6;ftm_early=true;
    assert(!esp32_mquickjs_wifi_radio_ftm_start(&cfg,&token) && owners()==1 && ftm_start_calls==1);
    assert(esp32_mquickjs_wifi_radio_ftm_start(&cfg,&other)==ESP_ERR_INVALID_STATE && !other.identity);
    old=token;cfg.resp_mac[5]=9;assert(s_ftm.config.resp_mac[5]==7);
    esp32_mquickjs_wifi_radio_operation_t bypass=s_radio.operation;esp32_mquickjs_wifi_radio_end_operation(&bypass);
    wifi_radio_operation_lock();wifi_radio_release_locked(&s_ftm.lease);wifi_radio_operation_unlock();assert(owners()==1);
    assert(esp32_mquickjs_wifi_radio_ftm_collect(&token,entries,SIZE_MAX,64,&state)==ESP_ERR_INVALID_ARG && !report_calls);
    timer_error=77;assert(esp32_mquickjs_wifi_radio_ftm_collect(&token,entries,sizeof(entries),64,&state)==77 && owners()==1);
    timer_error=0;
    assert(esp32_mquickjs_wifi_radio_ftm_collect(&token,entries,sizeof(entries),64,&state)==ESP_ERR_TIMEOUT && timer_pending && !report_calls);
    timer_fire();report_error=88;
    assert(esp32_mquickjs_wifi_radio_ftm_collect(&token,entries,sizeof(entries),64,&state)==88 && !state.report_consumed);
    report_error=0;
    assert(!esp32_mquickjs_wifi_radio_ftm_collect(&token,entries,sizeof(entries),64,&state));
    assert(state.copied_entries==64 && state.report.ftm_report_num_entries==70 && entries[63].t1==UINT64_MAX-63);
    assert(esp32_mquickjs_wifi_radio_ftm_collect(&token,entries,sizeof(entries),64,&state)==ESP_ERR_INVALID_STATE && report_calls==2);
    ftm_post_error=66;assert(esp32_mquickjs_wifi_radio_ftm_retire(&token,&state)==66 && owners()==1);
    ftm_post_error=0;assert(esp32_mquickjs_wifi_radio_ftm_retire(&token,&state)==ESP_ERR_TIMEOUT && ftm_queued);
    marker_fire();assert(!esp32_mquickjs_wifi_radio_ftm_retire(&token,&state) && !token.identity && !owners());
    assert(!esp32_mquickjs_wifi_radio_ftm_start(&cfg,&token) && token.identity!=old.identity);
    assert(esp32_mquickjs_wifi_radio_ftm_end(&old)==ESP_ERR_INVALID_STATE && owners()==1 && !ftm_end_calls);
    /* Setup boundaries may reset a quarantined fixture; production cannot. */
    reset_ftm();token=(esp32_mquickjs_wifi_ftm_token_t){0};ftm_start_error=55;
    assert(esp32_mquickjs_wifi_radio_ftm_start(&cfg,&token)==55 && token.identity && owners()==1);
    ftm_end_error=44;assert(esp32_mquickjs_wifi_radio_ftm_end(&token)==44 && ftm_end_calls==1);
    ftm_end_error=0;assert(!esp32_mquickjs_wifi_radio_ftm_end(&token) && ftm_end_calls==2);
    assert(!esp32_mquickjs_wifi_radio_ftm_end(&token) && ftm_end_calls==2);
    assert(esp32_mquickjs_wifi_radio_ftm_collect(&token,NULL,0,0,&state)==ESP_ERR_TIMEOUT && !timer_pending && !report_calls);
    terminal();assert(esp32_mquickjs_wifi_radio_ftm_collect(&token,NULL,0,0,&state)==ESP_ERR_TIMEOUT);
    timer_fire();assert(!esp32_mquickjs_wifi_radio_ftm_collect(&token,NULL,0,0,&state) && state.report_discarded && state.submit_error==55);
    assert(esp32_mquickjs_wifi_radio_ftm_retire(&token,&state)==ESP_ERR_TIMEOUT);terminal();marker_fire();
    assert(esp32_mquickjs_wifi_radio_ftm_retire(&token,&state)==ESP_ERR_TIMEOUT && state.ambiguous && owners()==1);
    reset_ftm();token=(esp32_mquickjs_wifi_ftm_token_t){0};ftm_early=true;
    assert(!esp32_mquickjs_wifi_radio_ftm_start(&cfg,&token));
    assert(esp32_mquickjs_wifi_radio_ftm_collect(&token,entries,sizeof(entries),64,&state)==ESP_ERR_TIMEOUT);
    timer_fire();report_race=true;
    assert(esp32_mquickjs_wifi_radio_ftm_collect(&token,entries,sizeof(entries),64,&state)==ESP_ERR_INVALID_RESPONSE);
    assert(state.report_consumed && state.ambiguous && !state.copied_entries && !entries[0].t1 && owners()==1);
    reset_ftm();token=(esp32_mquickjs_wifi_ftm_token_t){0};s_radio.next_operation_identity=UINT32_MAX;
    assert(!esp32_mquickjs_wifi_radio_ftm_start(&cfg,&token) && token.identity==UINT32_MAX && !s_radio.next_operation_identity);
    return 0;
}
'''
