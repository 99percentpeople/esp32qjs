"""Deferred TWT SDK snapshot/admission using production wrappers and validators.

Native table bytes and forwarding calls are controlled boundaries. Host pointer
width is substituted only at ioctl transport/ETSTimer declarations; C5 archive
layout, static assertions and actual linker wrapping require target evidence.
No fake SDK retirement oracle: a snapshot has no quiescent/retired result.
"""
from pathlib import Path
import re
import unittest
from test_wifi_action_lane import PRELUDE
from test_wifi_twt_broadcast_event import broadcast_types
from test_wifi_action_sdk import BOUNDARIES as ACTION_BOUNDARIES
from test_wifi_config_controls import sdk_types, structure
from test_wifi_driver_phy import COMPONENT
from test_wifi_rx_target import unit
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import extract


class WiFiTwtSdk(unittest.TestCase):
    def test_tables_pending_capacity_flow_selection_and_private_dispatch(self):
        sdk = Path('/home/zach/esp/esp-idf/components/esp_wifi/include/esp_wifi_he_types.h').read_text()
        action = (COMPONENT / 'src/modules/wifi_action/esp32_mquickjs_wifi_action_sdk.c').read_text()
        twt = COMPONENT / 'src/modules/wifi_twt'
        code = PRELUDE + r'''
#define CONFIG_SOC_WIFI_HE_SUPPORT 1
#define CONFIG_IDF_TARGET_ESP32C5 1
#define ESP_ERR_WIFI_TWT_FULL 0x3017
#define ESP_ERR_NOT_SUPPORTED 0x106
#define ESP_ERR_INVALID_RESPONSE 0x108
#define ESP_ERR_WIFI_NOT_ASSOC 0x3015
#define ESP_ERR_NOT_FINISHED 0x10c
typedef int JSValue;
typedef struct JSContext JSContext;
typedef struct { uint32_t prefix[4], timer_arg; } ETSTimer;
#define _Static_assert(...)
'''
        code += re.search(r'typedef enum \{[^}]*\} wifi_twt_setup_cmds_t;', sdk).group(0)
        code += structure(sdk, 'wifi_twt_config_t')
        code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_twt_controls.h')
        code += structure(sdk, 'wifi_twt_setup_config_t')
        code += '\ntypedef wifi_twt_setup_config_t wifi_itwt_setup_config_t;\n'
        code += structure(sdk, 'wifi_btwt_setup_config_t')
        code += structure(sdk, 'esp_wifi_btwt_info_t')
        code += re.search(r'typedef enum \{[^}]*\} wifi_itwt_probe_status_t;', sdk).group(0)
        code += structure(sdk, 'wifi_event_sta_itwt_probe_t')
        code += structure(sdk, 'wifi_event_sta_itwt_setup_t')
        code += broadcast_types(commands=False)
        code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_twt_options.h')
        code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_twt_tx.h')
        code += structure(sdk, 'wifi_event_sta_itwt_suspend_t')
        code += '#define WIFI_EVENT_ITWT_SUSPEND 31\n'
        code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_twt_probe_timer.h')
        code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_twt_probe_result.h')
        code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_twt_probe_wake.h')
        code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_twt_setup_timer.h')
        code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_twt_setup_result.h')
        code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_twt_teardown_tx.h')
        code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_twt_information_timer.h')
        code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_twt_information.h')
        code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_twt_broadcast_timer.h')
        code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_twt_sdk.h')
        # Entire production native validators, excluding the unrelated JS parser.
        code += unit(twt / 'esp32_mquickjs_wifi_twt_options.c').split('#define READ(')[0] + '\n#endif\n'
        code += sdk_types('esp32c5/representative', ('wifi_action_tx_req_t', 'wifi_roc_req_t'))
        code += ACTION_BOUNDARIES
        code += structure(action, 'action_sdk_twt_request_t')
        code += structure(action, 'action_sdk_twt_broadcast_request_t')
        code += '#define ACTION_SDK_TWT_BROADCAST_SNAPSHOT_TYPE ((wifi_action_tx_t)(INT32_MAX - 8))\n'
        code += structure(action, 'action_sdk_twt_probe_cancel_request_t')
        code += structure(action, 'action_sdk_twt_probe_control_t')
        code += structure(action, 'action_sdk_twt_setup_control_t')
        code += structure(action, 'action_sdk_btwt_control_t')
        code += '#define ACTION_SDK_BTWT_CONTROL_TYPE ((wifi_action_tx_t)(INT32_MAX - 9))\nenum { BTWT_CANCEL, BTWT_QUIESCENT, BTWT_RELEASE, BTWT_TEARDOWN };\n'

        code += '#define ACTION_SDK_TWT_SETUP_CONTROL_TYPE ((wifi_action_tx_t)(INT32_MAX - 7))\n'
        code += 'enum { TWT_SETUP_QUIESCENT, TWT_SETUP_RELEASE, TWT_SETUP_TEARDOWN, TWT_TEARDOWN_TX_QUIESCENT, TWT_TEARDOWN_TX_RELEASE, TWT_INFORMATION_SUBMIT, TWT_INFORMATION_REAP, TWT_INFORMATION_RESUME };\n'
        code += '#define ACTION_SDK_TWT_PROBE_CONTROL_TYPE ((wifi_action_tx_t)(INT32_MAX - 5))\n'
        code += 'enum { TWT_PROBE_SUBMIT, TWT_PROBE_QUIESCENT, TWT_PROBE_RELEASE };\n'
        code += '#define ACTION_SDK_TWT_SNAPSHOT_TYPE ((wifi_action_tx_t)(INT32_MAX - 3))\n'
        code += '#define ACTION_SDK_TWT_PROBE_CANCEL_TYPE ((wifi_action_tx_t)(INT32_MAX - 4))\n'
        code += '#define ACTION_SDK_TWT_SETUP_CANCEL_TYPE ((wifi_action_tx_t)(INT32_MAX - 6))\n'
        code += BOUNDARIES
        code += unit(twt / 'esp32_mquickjs_wifi_twt_probe_result.c')
        code += unit(twt / 'esp32_mquickjs_wifi_twt_sdk.c')
        for name in ('esp32_mquickjs_wifi_action_receive', 'action_sdk_owner', 'action_sdk_guarded_callback',
                     '__wrap_wifi_action_tx_process', 'esp32_mquickjs_wifi_twt_sdk_broadcast_teardown', 'esp32_mquickjs_wifi_twt_sdk_broadcast_cancel', 'esp32_mquickjs_wifi_twt_sdk_broadcast_quiescent', 'esp32_mquickjs_wifi_twt_sdk_broadcast_release', 'esp32_mquickjs_wifi_twt_sdk_snapshot',
                     'esp32_mquickjs_wifi_twt_sdk_probe_cancel', 'esp32_mquickjs_wifi_twt_sdk_setup_cancel', 'action_sdk_probe_control',
                     'esp32_mquickjs_wifi_twt_sdk_probe_submit', 'esp32_mquickjs_wifi_twt_sdk_probe_quiescent',
                     'esp32_mquickjs_wifi_twt_sdk_probe_release', 'esp32_mquickjs_wifi_twt_sdk_setup_quiescent',
                     'esp32_mquickjs_wifi_twt_sdk_setup_release','esp32_mquickjs_wifi_twt_sdk_setup_teardown', 'esp32_mquickjs_wifi_twt_sdk_teardown_tx_quiescent',
                     'esp32_mquickjs_wifi_twt_sdk_teardown_tx_release', 'esp32_mquickjs_wifi_twt_sdk_information_submit',
                     'esp32_mquickjs_wifi_twt_sdk_information_reap', 'esp32_mquickjs_wifi_twt_sdk_broadcast_snapshot'):
            code += extract(action, name)
        compile_run(self, code + MAIN)


BOUNDARIES = r'''
uint8_t g_ic[256],g_pm_cfg[128];
bool current_task_is_wifi_task(void) {return ioctl_task;}
#define ESP_FAIL (-1)
/* Control dispatch is exercised by test_wifi_twt_controls; this fixture never
 * enters those routes while testing setup, snapshots and native retirement. */
static int eloop_register_timeout_blocking(int (*fn)(void *,void *),void *a,void *b) {
    (void)fn;(void)a;(void)b;assert(0);return ESP_FAIL;
}
void pm_twt_set_config(wifi_twt_config_t *value) {(void)value;assert(0);}
int ieee80211_itwt_get_flow_id_status(int *out) {(void)out;assert(0);return ESP_FAIL;}
int wifi_sta_itwt_set_target_wake_time_offset_process(void *message) {(void)message;assert(0);return ESP_FAIL;}

#ifndef ESP_ERR_INVALID_SIZE
#define ESP_ERR_INVALID_SIZE 0x104
#endif
static uint8_t broadcast_count;
static unsigned broadcast_gets;
static int broadcast_get_error;
static esp_wifi_btwt_info_t broadcast_records[32];
#define DRAM_ATTR
#define WIFI_EVENT_BTWT_SETUP 33
#define WIFI_EVENT_BTWT_TEARDOWN 34
static esp_err_t esp32_mquickjs_wifi_btwt_event_post(int id,const void *data,size_t size) {
    (void)id;(void)data;(void)size;assert(0);return ESP_ERR_INVALID_ARG;
}
#define WIFI_EVENT_ITWT_PROBE 30
#define WIFI_EVENT ((const char *)19)
#define ESP_EVENT_DECLARE_BASE(name)
#define ESP32QJS_WIFI_RADIO_CONTROL_EVENT ((const char *)20)
typedef unsigned portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
static unsigned locked;
uint8_t wifi_sta_get_btwt_num(void) {assert(ioctl_task && !locked);return broadcast_count;}
esp_err_t ieee80211_get_btwt_info(uint8_t capacity,esp_wifi_btwt_info_t *out) {
    assert(ioctl_task && !locked && capacity==32);++broadcast_gets;
    memcpy(out,broadcast_records,sizeof(broadcast_records));return broadcast_get_error;
}


#define portENTER_CRITICAL_SAFE(lock) do {assert(!locked);locked=1;} while(0)
#define portEXIT_CRITICAL_SAFE(lock) do {assert(locked==1);locked=0;} while(0)
int __real_wifi_event_post(int id,void *data,size_t size) {(void)id;(void)data;(void)size;assert(0);return -1;}
static esp_err_t esp_event_post(const char *base,int32_t id,const void *data,size_t size,unsigned ticks) {
    assert(!locked && data && size==8 && ticks==0);
    assert((base==WIFI_EVENT && id==30) || (base==ESP32QJS_WIFI_RADIO_CONTROL_EVENT && id==4));return 0;
}
static esp32_mquickjs_wifi_twt_probe_wake_snapshot_t wake_snapshot;
void esp32_mquickjs_wifi_twt_probe_wake_snapshot(esp32_mquickjs_wifi_twt_probe_wake_snapshot_t *out) {*out=wake_snapshot;}
static esp32_mquickjs_wifi_twt_setup_timer_snapshot_t setup_snapshot;
static unsigned broadcast_completions,broadcast_binds;
static uint8_t broadcast_callback_status,broadcast_callback_parameter[17];
void ieee80211_btwt_setup_txcb(void *buffer) {
    assert(!locked);const uint8_t *descriptor,*frame,*metadata;uint16_t flags;
    memcpy(&descriptor,(uint8_t *)buffer+4,sizeof(descriptor));
    memcpy(&frame,descriptor+4,sizeof(frame));memcpy(&metadata,(uint8_t *)buffer+56,sizeof(metadata));
    memcpy(&flags,(uint8_t *)buffer+40,sizeof(flags));assert(!flags);
    assert(frame[24]==22 && frame[25]==6 && frame[26]==9);
    broadcast_callback_status=metadata[19];memcpy(broadcast_callback_parameter,frame+27,17);++broadcast_completions;
}
void esp32_mquickjs_wifi_btwt_timer_bind_response_native(uintptr_t node,uint8_t dialog,const uint8_t parameter[17]) {
    assert(!locked && node==17 && dialog==9 && parameter[0]==216);++broadcast_binds;
}
static esp32_mquickjs_wifi_btwt_timer_snapshot_t broadcast_timer_snapshot;
static esp_err_t btwt_control_error;static unsigned btwt_control_calls;
static bool btwt_owner=true;static unsigned btwt_teardown_calls,btwt_teardown_completions;
bool esp32_mquickjs_wifi_btwt_setup_owner_native(unsigned slot,uint32_t identity,uintptr_t *node) {
    assert(!locked && slot==3 && identity==777);if(!btwt_owner)return false;*node=17;return true;
}
esp_err_t esp32_mquickjs_wifi_twt_teardown_tx_begin_broadcast_native(uint32_t identity,uintptr_t node,uint8_t slot) {
    assert(ioctl_task && identity==777 && node==17 && slot==3);return btwt_control_error;
}
int ieee80211_btwt_teardown(void *node,unsigned slot) {
    assert(ioctl_task && !locked && node==(void *)17 && slot==3);++btwt_teardown_calls;return 0;
}
void ieee80211_btwt_teardown_txcb(void *node,void *buffer) {
    assert(!locked && node==(void *)17);const uint8_t *descriptor,*frame,*metadata;
    memcpy(&descriptor,(uint8_t *)buffer+4,sizeof(descriptor));memcpy(&frame,descriptor+4,sizeof(frame));
    memcpy(&metadata,(uint8_t *)buffer+56,sizeof(metadata));assert(frame[24]==22 && frame[25]==7 && frame[26]==0x63 && metadata[19]==1);
    ++btwt_teardown_completions;
}
esp_err_t esp32_mquickjs_wifi_btwt_setup_release_native(unsigned slot,uint32_t identity,const esp32_mquickjs_wifi_btwt_cut_t *cut,uint32_t sequence){
    assert(ioctl_task && !locked && slot==3 && identity==777 && cut->tx_revision==17 && cut->timer_revision==23 && cut->teardown_revision==31 && sequence==9);
    ++btwt_control_calls;return btwt_control_error;
}

esp_err_t esp32_mquickjs_wifi_btwt_setup_cancel_native(unsigned slot,uint32_t identity){assert(ioctl_task && !locked && slot==3 && identity==777);++btwt_control_calls;return btwt_control_error;}
esp_err_t esp32_mquickjs_wifi_btwt_setup_quiescent_native(unsigned slot,uint32_t identity,esp32_mquickjs_wifi_btwt_cut_t *out){
    assert(ioctl_task && !locked && slot==3 && identity==777);++btwt_control_calls;
    if(btwt_control_error)return btwt_control_error;*out=(esp32_mquickjs_wifi_btwt_cut_t){17,23,31};return 0;
}

esp_err_t esp32_mquickjs_wifi_btwt_submit_enter_native(const wifi_btwt_setup_config_t *config,bool *managed) {
    assert(config && !*managed);return 0;
}
bool esp32_mquickjs_wifi_btwt_submit_driver_native(const wifi_btwt_setup_config_t *config){(void)config;assert(0);return false;}
void esp32_mquickjs_wifi_btwt_submit_complete_native(const wifi_btwt_setup_config_t *config,esp_err_t error){(void)config;(void)error;assert(0);}
esp_err_t esp32_mquickjs_wifi_twt_tx_broadcast_admit_native(unsigned slot){assert(slot<32);return broadcast_timer_snapshot.fault;}

void esp32_mquickjs_wifi_btwt_timer_snapshot(esp32_mquickjs_wifi_btwt_timer_snapshot_t *out) {*out=broadcast_timer_snapshot;}
esp_err_t esp32_mquickjs_wifi_btwt_timer_error(void) {return broadcast_timer_snapshot.fault;}
void esp32_mquickjs_wifi_twt_tx_cleanup_native(void) {assert(!locked && ioctl_task);}
void esp32_mquickjs_wifi_twt_information_timer_snapshot(esp32_mquickjs_wifi_twt_information_timer_snapshot_t *out) {memset(out,0,sizeof(*out));}
static int information_cleanup_error,information_query_error;
static uint32_t information_revision=5;
esp_err_t esp32_mquickjs_wifi_twt_information_timer_cleanup_native(int16_t request_id,uint8_t flows) {
    (void)flows;assert(ioctl_task && request_id>=0 && !locked);return information_cleanup_error;
}
esp_err_t esp32_mquickjs_wifi_twt_information_timer_quiescent_native(int16_t request_id,uint32_t *revision) {
    assert(ioctl_task && request_id>=0 && !locked);
    if(information_query_error)return information_query_error;
    *revision=information_revision;return 0;
}
#define WIFI_EVENT_ITWT_SETUP 28
#define WIFI_EVENT_ITWT_TEARDOWN 29
/* The dedicated result fixture uses the real bounded registry and event
 * dispatch. This SDK fixture controls only its reserve/publication boundary. */
static uint32_t setup_result_identity;
static int setup_result_error,setup_result_submit_error;
static unsigned setup_result_begins,setup_result_submissions;
static esp32_mquickjs_wifi_twt_setup_result_t cancel_result;
bool esp32_mquickjs_wifi_twt_setup_request_closed_native(int16_t request_id) {
    assert(!locked);return cancel_result.identity && cancel_result.request_id==request_id &&
        (cancel_result.flags&ESP32_MQUICKJS_WIFI_TWT_SETUP_NATIVE_CLOSED);
}
static unsigned setup_cancels;
static uint8_t cancelled_pending;
static int setup_cancel_error;
esp_err_t esp32_mquickjs_wifi_twt_setup_result_read(uint32_t identity,esp32_mquickjs_wifi_twt_setup_result_t *out) {
    assert(ioctl_task && !locked);
    if(!identity)return ESP_ERR_INVALID_ARG;
    if(identity!=cancel_result.identity)return ESP_ERR_INVALID_STATE;
    *out=cancel_result;return ESP_OK;
}
static esp32_mquickjs_wifi_twt_teardown_tx_snapshot_t teardown_snapshot;
static unsigned teardown_tx_begins,teardown_tx_abandons,teardown_tx_ends;
static int teardown_tx_begin_error,teardown_tx_end_error;
esp_err_t esp32_mquickjs_wifi_twt_teardown_tx_begin_native(uint32_t identity,uintptr_t node,uint8_t flow) {
    assert(ioctl_task && !locked && identity==cancel_result.identity && node && flow==3);++teardown_tx_begins;
    return teardown_tx_begin_error;
}
bool esp32_mquickjs_wifi_twt_teardown_tx_abandon_native(uint32_t identity) {
    assert(ioctl_task && !locked && identity==cancel_result.identity);++teardown_tx_abandons;return true;
}
esp_err_t esp32_mquickjs_wifi_twt_teardown_tx_end_native(uint32_t identity,esp_err_t error) {
    (void)error;if(identity==777){assert(ioctl_task && !locked);return 0;}
    assert(ioctl_task && !locked && identity==cancel_result.identity);++teardown_tx_ends;return teardown_tx_end_error;
}
void esp32_mquickjs_wifi_twt_teardown_tx_snapshot(esp32_mquickjs_wifi_twt_teardown_tx_snapshot_t *out) {*out=teardown_snapshot;}
esp_err_t esp32_mquickjs_wifi_twt_teardown_tx_quiescent_native(uint32_t identity,uint32_t *revision) {
    assert(ioctl_task && !locked);if(identity!=cancel_result.identity)return ESP_ERR_INVALID_STATE;
    *revision=17;return 0;
}
bool esp32_mquickjs_wifi_twt_teardown_tx_release_native(uint32_t identity,uint32_t revision) {
    assert(ioctl_task && !locked);return identity==cancel_result.identity && revision==17;
}
static unsigned teardown_begins,teardown_calls,teardown_finishes;
static int teardown_begin_error,teardown_driver_error;
esp_err_t esp32_mquickjs_wifi_twt_setup_result_teardown_begin_native(uint32_t identity,uint8_t flow) {
    assert(ioctl_task && !locked && identity==cancel_result.identity && flow==3);++teardown_begins;
    if(teardown_begin_error)return teardown_begin_error;
    cancel_result.flags|=ESP32_MQUICKJS_WIFI_TWT_TEARDOWN_SUBMITTING|ESP32_MQUICKJS_WIFI_TWT_TEARDOWN_ATTEMPTED;return 0;
}
void esp32_mquickjs_wifi_twt_setup_result_teardown_submitted_native(uint32_t identity,esp_err_t error) {
    assert(ioctl_task && !locked && identity==cancel_result.identity && error==teardown_driver_error);++teardown_finishes;
    cancel_result.teardown_error=error;cancel_result.flags&=(uint16_t)~ESP32_MQUICKJS_WIFI_TWT_TEARDOWN_SUBMITTING;
}
int wifi_sta_itwt_teardown_process(void *message) {
    assert(ioctl_task && !locked && message && ((uint8_t *)message)[8]==3);++teardown_calls;
    assert(cancel_result.flags&ESP32_MQUICKJS_WIFI_TWT_TEARDOWN_SUBMITTING);return teardown_driver_error;
}
static unsigned setup_releases;
static bool setup_release_ready;
static int setup_timer_query_error;
bool esp32_mquickjs_wifi_twt_setup_result_cancelled_native(uint32_t identity,uint8_t flows) {
    assert(ioctl_task && !locked && identity==cancel_result.identity);
    cancel_result.cancelled_flows|=flows;cancel_result.flags|=ESP32_MQUICKJS_WIFI_TWT_SETUP_CANCELLED;
    ++cancel_result.revision;return true;
}
bool esp32_mquickjs_wifi_twt_setup_result_release_native(uint32_t identity,uint32_t revision,uint32_t sequence) {
    assert(ioctl_task && !locked && identity==cancel_result.identity && revision==cancel_result.revision && sequence==7);
    ++setup_releases;return setup_release_ready;
}
esp_err_t esp32_mquickjs_wifi_twt_setup_timer_quiescent_native(int16_t request_id,uint32_t *revision) {
    assert(ioctl_task && !locked && request_id==cancel_result.request_id);
    if(setup_timer_query_error)return setup_timer_query_error;
    *revision=setup_snapshot.revision;return ESP_OK;
}
esp_err_t esp32_mquickjs_wifi_twt_setup_timer_cancel_native(int16_t request_id,uint8_t pending) {
    assert(ioctl_task && !locked && request_id==cancel_result.request_id);
    ++setup_cancels;cancelled_pending=pending;return setup_cancel_error;
}
esp_err_t esp32_mquickjs_wifi_twt_setup_result_begin_native(int16_t id,uint32_t *identity) {
    assert(ioctl_task && id>=0 && identity && !*identity);++setup_result_begins;
    if(setup_result_error)return setup_result_error;*identity=++setup_result_identity;return ESP_OK;
}
void esp32_mquickjs_wifi_twt_setup_result_submitted_native(uint32_t identity,esp_err_t error) {
    assert(ioctl_task && identity==setup_result_identity);++setup_result_submissions;setup_result_submit_error=error;
}
void esp32_mquickjs_wifi_twt_setup_results_snapshot(esp32_mquickjs_wifi_twt_setup_results_snapshot_t *out) {
    memset(out,0,sizeof(*out));out->last_identity=setup_result_identity;
}
esp_err_t esp32_mquickjs_wifi_twt_setup_result_post(const void *data,size_t size) {
    (void)data;(void)size;assert(0);return ESP_ERR_INVALID_ARG;
}
esp_err_t esp32_mquickjs_wifi_twt_setup_result_teardown_post(const void *data,size_t size) {
    (void)data;(void)size;assert(0);return ESP_ERR_INVALID_ARG;
}
/* Managed worker handoff has a separate fixture with the real submit helper. */
esp_err_t esp32_mquickjs_wifi_twt_setup_submit_enter_native(const wifi_itwt_setup_config_t *config,uint32_t *identity) {
    assert(ioctl_task && config && identity && !*identity);return ESP_OK;
}
bool esp32_mquickjs_wifi_twt_setup_submit_driver_native(const wifi_itwt_setup_config_t *config,uint32_t identity) {
    (void)config;(void)identity;assert(0);return false;
}
void esp32_mquickjs_wifi_twt_setup_submit_complete_native(const wifi_itwt_setup_config_t *config,uint32_t identity,esp_err_t error) {
    (void)config;(void)identity;(void)error;assert(0);
}
void esp32_mquickjs_wifi_twt_setup_timer_snapshot(esp32_mquickjs_wifi_twt_setup_timer_snapshot_t *out) {*out=setup_snapshot;}
esp_err_t esp32_mquickjs_wifi_twt_setup_timer_error(void) {return setup_snapshot.fault;}
static unsigned cancels;
static int cancel_error;
esp_err_t esp32_mquickjs_wifi_twt_probe_timer_cancel_native(void) {
    assert(ioctl_task && !locked);++cancels;return cancel_error;
}
static esp32_mquickjs_wifi_twt_probe_timer_snapshot_t probe_timer_snapshot;
void esp32_mquickjs_wifi_twt_probe_timer_snapshot(esp32_mquickjs_wifi_twt_probe_timer_snapshot_t *out) {
    *out=probe_timer_snapshot;
}
static esp32_mquickjs_wifi_twt_tx_snapshot_t tx_snapshot;
void esp32_mquickjs_wifi_twt_tx_snapshot(esp32_mquickjs_wifi_twt_tx_snapshot_t *out) {
    assert(out);*out=tx_snapshot;
}
static unsigned probe_submits;
static bool accept_probe;
static int probe_submit_error;
/* Native submit wrapper has its own production TX/timer fixture. Here the
 * controlled boundary verifies the private message and output ownership. */
int __wrap_wifi_sta_itwt_send_probe_req_process(void *message) {
    assert(ioctl_task && message && ((uint32_t *)message)[3]==123);++probe_submits;
    if(accept_probe) {
        uint32_t identity=0;
        esp_err_t error=esp32_mquickjs_wifi_twt_probe_result_begin_native(&identity);
        if(error!=ESP_OK)return error;
        esp32_mquickjs_wifi_twt_probe_result_submitted_native(identity,probe_submit_error);
    }
    return probe_submit_error;
}
uint8_t setup_timer_param[372],btwt_setup_timer[1248];
int16_t s_itwt_id[8],s_tmp_itwt_id[8];
uint8_t s_itwt_flow_id_bitmap,s_itwt_suspend_flow_id_bitmap,s_itwt_resume_flow_id_bitmap;
uint32_t s_btwt_id_bitmap;
ETSTimer itwt_information_timer[8],itwt_probe_timer;
static unsigned individual_calls,broadcast_calls,queries;
static unsigned wake_releases;
void pm_wake_done(void) {assert(ioctl_task);++wake_releases;}
void esp32_mquickjs_wifi_twt_probe_wake_done_native(void) {pm_wake_done();}
static int forward_error,dispatch_error;
esp_err_t __real_wifi_sta_itwt_setup_process(void *message) {
    assert(ioctl_task && message);++individual_calls;return forward_error;
}
esp_err_t __real_wifi_sta_btwt_setup_process(void *message) {
    assert(ioctl_task && message);++broadcast_calls;return forward_error;
}
/* Saved-PHY branch is not exercised here; its production path has a separate
 * fixture. The TWT branch and native read are both the production functions. */
#define ACTION_SDK_SAVED_PHY_TYPE ((wifi_action_tx_t)(INT32_MAX - 2))
typedef struct { wifi_action_tx_req_t request; } action_sdk_phy_request_t;
static esp_err_t action_sdk_saved_phy_process(action_sdk_phy_request_t *request) {
    (void)request;assert(0);return -1;
}
esp_err_t __wrap_wifi_action_tx_process(void *message);
static esp_err_t esp_wifi_action_tx_req(wifi_action_tx_req_t *request) {
    assert(!ioctl_task && request);++queries;
    if(dispatch_error)return dispatch_error;
    uint8_t message[32]={0};memcpy(message+20,&request,sizeof(request));
    ioctl_task=true;int result=__wrap_wifi_action_tx_process(message);ioctl_task=false;return result;
}
'''
MAIN = r'''
static void empty_tables(void) {
    memset(setup_timer_param,0,sizeof(setup_timer_param));
    memset(btwt_setup_timer,0,sizeof(btwt_setup_timer));
    memset(itwt_information_timer,0,sizeof(itwt_information_timer));
    memset(&itwt_probe_timer,0,sizeof(itwt_probe_timer));
    for(unsigned i=0;i<8;++i)s_itwt_id[i]=s_tmp_itwt_id[i]=-1;
    s_itwt_flow_id_bitmap=s_itwt_suspend_flow_id_bitmap=s_itwt_resume_flow_id_bitmap=0;
    s_btwt_id_bitmap=0;forward_error=0;
}
static void pending(unsigned slot,unsigned flow,int16_t id) {
    assert(slot<8 && flow<8);setup_timer_param[368]|=1U<<slot;
    uint8_t *frame=setup_timer_param+17*slot;
    frame[3]=(uint8_t)((flow&1)<<7);frame[4]=(uint8_t)(flow>>1);
    memcpy(setup_timer_param+320+2*slot,&id,sizeof(id));
}
static void timer(void *bytes,unsigned offset) {
    uint32_t opaque=0xf1234567;memcpy((uint8_t *)bytes+offset+16,&opaque,4);
}
static wifi_itwt_setup_config_t config(void) {
    return (wifi_itwt_setup_config_t){.setup_cmd=TWT_REQUEST,.min_wake_dura=1,
        .wake_invl_mant=10256,.timeout_time_ms=100,.twt_id=123};
}
int main(void) {
    empty_tables();esp32_mquickjs_wifi_twt_sdk_snapshot_t snapshot,unchanged;
    memset(&unchanged,0xa5,sizeof(unchanged));snapshot=unchanged;
    assert(esp32_mquickjs_wifi_twt_sdk_snapshot(NULL)==ESP_ERR_INVALID_ARG && !queries);
    dispatch_error=71;
    assert(esp32_mquickjs_wifi_twt_sdk_snapshot(&snapshot)==71 && !memcmp(&snapshot,&unchanged,sizeof(snapshot)));
    dispatch_error=0;
    assert(esp32_mquickjs_wifi_twt_sdk_snapshot(&snapshot)==ESP_OK && !delegates);
    assert(!snapshot.individual_pending_mask && !snapshot.individual_flow_bitmap);
    for(unsigned i=0;i<8;++i)assert(snapshot.individual_pending_ids[i]==-1 && snapshot.individual_pending_flows[i]==255);
    s_itwt_flow_id_bitmap=0x82;s_itwt_suspend_flow_id_bitmap=0x80;s_itwt_resume_flow_id_bitmap=2;
    s_itwt_id[1]=19;s_tmp_itwt_id[7]=32767;s_btwt_id_bitmap=UINT32_C(0x80000002);
    assert(esp32_mquickjs_wifi_twt_sdk_broadcast_established_native(1));
    assert(esp32_mquickjs_wifi_twt_sdk_broadcast_established_native(31));
    assert(!esp32_mquickjs_wifi_twt_sdk_broadcast_established_native(3));
    assert(!esp32_mquickjs_wifi_twt_sdk_broadcast_established_native(32));
    for(unsigned i=0;i<8;++i){pending(i,7-i,(int16_t)(100+i));timer(setup_timer_param,144+20*i);}
    timer(&itwt_information_timer[7],0);timer(&itwt_probe_timer,0);
    for(unsigned i=0;i<32;++i)timer(btwt_setup_timer,20*i);
    assert(esp32_mquickjs_wifi_twt_sdk_snapshot(&snapshot)==ESP_OK);
    assert(snapshot.individual_pending_mask==255 && snapshot.individual_setup_timer_mask==255);
    assert(snapshot.individual_flow_bitmap==0x82 && snapshot.individual_suspend_bitmap==0x80 && snapshot.individual_resume_bitmap==2);
    assert(snapshot.individual_information_timer_mask==0x80 && snapshot.probe_timer_present);
    assert(snapshot.individual_ids[1]==19 && snapshot.individual_temporary_ids[7]==32767);
    assert(snapshot.broadcast_id_bitmap==UINT32_C(0x80000002) && snapshot.broadcast_setup_timer_mask==UINT32_MAX);
    for(unsigned i=0;i<8;++i)assert(snapshot.individual_pending_flows[i]==7-i && snapshot.individual_pending_ids[i]==(int)(100+i));
    action_sdk_twt_request_t foreign={.request={.ifx=WIFI_IF_STA,.type=ACTION_SDK_TWT_SNAPSHOT_TYPE,.rx_cb=other_receive}};
    assert(esp_wifi_action_tx_req(&foreign.request)==77 && delegates==1);

    /* Pending capacity is independent of the public established-flow bitmap.
     * The full table must never reach the original unchecked slot selector. */
    empty_tables();wifi_itwt_setup_config_t request=config(),*pointer=&request;
    uint8_t message[32]={0};memcpy(message+12,&pointer,sizeof(pointer));ioctl_task=true;
    assert(__wrap_wifi_sta_itwt_setup_process(NULL)==ESP_ERR_INVALID_ARG);
    for(unsigned i=0;i<8;++i)pending(i,i,(int16_t)(i+1));
    wifi_itwt_setup_config_t original=request;
    assert(__wrap_wifi_sta_itwt_setup_process(message)==ESP_ERR_WIFI_TWT_FULL && !individual_calls);
    assert(!memcmp(&request,&original,sizeof(request)));
    empty_tables();s_itwt_flow_id_bitmap=0xfe;pending(3,0,14);
    assert(__wrap_wifi_sta_itwt_setup_process(message)==ESP_ERR_WIFI_TWT_FULL && !individual_calls);
    empty_tables();s_itwt_flow_id_bitmap=3;pending(7,2,45);forward_error=83;
    assert(__wrap_wifi_sta_itwt_setup_process(message)==83 && individual_calls==1 && request.flow_id==3);
    assert(request.twt_id==123 && request.timeout_time_ms==100);
    /* All three ID stores are rechecked inside the mutation queue. */
    for(unsigned which=0;which<3;++which) {
        empty_tables();request=config();
        if(which==0)s_itwt_id[5]=123;
        if(which==1)s_tmp_itwt_id[5]=123;
        if(which==2)pending(5,4,123);
        assert(__wrap_wifi_sta_itwt_setup_process(message)==ESP_ERR_INVALID_ARG && individual_calls==1);
    }
    empty_tables();request=config();request.timeout_time_ms=99;
    assert(__wrap_wifi_sta_itwt_setup_process(message)==ESP_ERR_INVALID_ARG && individual_calls==1);
    request=config();request.flow_id=7;
    assert(__wrap_wifi_sta_itwt_setup_process(message)==ESP_OK && individual_calls==2 && request.flow_id==7);
    wifi_btwt_setup_config_t bt={.setup_cmd=TWT_REQUEST,.btwt_id=1,.timeout_time_ms=1},*bp=&bt;
    memcpy(message+12,&bp,sizeof(bp));
    unsigned ids[]={0,32,255};
    for(unsigned i=0;i<3;++i){bt.btwt_id=ids[i];assert(__wrap_wifi_sta_btwt_setup_process(message)==ESP_ERR_INVALID_ARG && !broadcast_calls);}
    bt.btwt_id=31;forward_error=84;
    assert(__wrap_wifi_sta_btwt_setup_process(message)==84 && broadcast_calls==1);
    bt.timeout_time_ms=0;assert(__wrap_wifi_sta_btwt_setup_process(message)==ESP_ERR_INVALID_ARG && broadcast_calls==1);
    /* Probe admission reads the same native task's association/pending state,
     * retaining the SDK timer-presence boundary independently of a TX count. */
    empty_tables();memset(g_ic,0,sizeof(g_ic));
    assert(esp32_mquickjs_wifi_twt_sdk_probe_admit_native()==ESP_ERR_WIFI_NOT_ASSOC);
    uint8_t broadcast_parameter[17]={216,10,12,1};
    esp32_mquickjs_wifi_twt_sdk_broadcast_tx_complete_native(17,9,broadcast_parameter,1);
    assert(broadcast_completions==1 && broadcast_binds==1 && broadcast_callback_status==1 &&
        !memcmp(broadcast_callback_parameter,broadcast_parameter,17));
    esp32_mquickjs_wifi_twt_sdk_broadcast_tx_complete_native(17,9,broadcast_parameter,77);
    assert(broadcast_completions==2 && broadcast_binds==1 && broadcast_callback_status==77);
    uint8_t station[256]={0},node[1200]={0};void *sp=station,*np=node;uint32_t state=5;
    memcpy(g_ic+16,&sp,sizeof(sp));
    assert(esp32_mquickjs_wifi_twt_sdk_probe_admit_native()==ESP_ERR_WIFI_NOT_ASSOC);
    memcpy(station+228,&np,sizeof(np));
    assert(esp32_mquickjs_wifi_twt_sdk_probe_admit_native()==ESP_ERR_WIFI_NOT_ASSOC);
    memcpy(station+152,&state,sizeof(state));
    assert(esp32_mquickjs_wifi_twt_sdk_probe_admit_native()==ESP_OK);
    ioctl_task=false; /* Public snapshot submits to the native task. */
    esp32_mquickjs_wifi_twt_broadcast_snapshot_t broadcasts, saved_broadcasts;
    memset(&saved_broadcasts,0xa5,sizeof(saved_broadcasts));broadcasts=saved_broadcasts;
    assert(esp32_mquickjs_wifi_twt_sdk_broadcast_snapshot(NULL)==ESP_ERR_INVALID_ARG);
    assert(!esp32_mquickjs_wifi_twt_sdk_broadcast_snapshot(&broadcasts));
    assert(!broadcasts.count && !broadcast_gets);
    broadcast_count=32;s_btwt_id_bitmap=UINT32_C(0x80000001);
    for(unsigned i=0;i<32;++i)broadcast_records[i]=(esp_wifi_btwt_info_t){.btwt_id_in_use=true,
        .btwt_info_id=31-i,.btwt_wake_duration=255,.btwt_wake_interval_exponent=31,
        .btwt_wake_interval_mantissa=65535,.btwt_info_persistence=255};
    assert(!esp32_mquickjs_wifi_twt_sdk_broadcast_snapshot(&broadcasts));
    assert(broadcasts.count==32 && broadcasts.joined_bitmap==UINT32_C(0x80000001));
    assert(broadcasts.schedules[0].btwt_info_id==31 && broadcasts.schedules[31].btwt_info_id==0);
    broadcasts=saved_broadcasts;broadcast_get_error=91;
    assert(esp32_mquickjs_wifi_twt_sdk_broadcast_snapshot(&broadcasts)==91);
    assert(!memcmp(&broadcasts,&saved_broadcasts,sizeof(broadcasts)));
    broadcast_get_error=0;broadcast_count=33;
    assert(esp32_mquickjs_wifi_twt_sdk_broadcast_snapshot(&broadcasts)==ESP_ERR_INVALID_SIZE);
    assert(!memcmp(&broadcasts,&saved_broadcasts,sizeof(broadcasts)));
    broadcast_count=32;broadcast_records[31].btwt_info_id=31;
    assert(esp32_mquickjs_wifi_twt_sdk_broadcast_snapshot(&broadcasts)==ESP_ERR_INVALID_STATE);
    assert(!memcmp(&broadcasts,&saved_broadcasts,sizeof(broadcasts)));
    memset(broadcast_records,0,sizeof(broadcast_records));broadcast_count=1;
    broadcast_records[31]=(esp_wifi_btwt_info_t){.btwt_id_in_use=true,.btwt_info_id=31};
    assert(!esp32_mquickjs_wifi_twt_sdk_broadcast_snapshot(&broadcasts));
    assert(broadcasts.count==1 && broadcasts.schedules[0].btwt_info_id==31);
    state=4;memcpy(station+152,&state,sizeof(state));broadcasts=saved_broadcasts;
    assert(esp32_mquickjs_wifi_twt_sdk_broadcast_snapshot(&broadcasts)==ESP_ERR_WIFI_NOT_ASSOC);
    assert(!memcmp(&broadcasts,&saved_broadcasts,sizeof(broadcasts)));
    state=5;memcpy(station+152,&state,sizeof(state));s_btwt_id_bitmap=0;
    assert(esp32_mquickjs_wifi_twt_sdk_broadcast_node_matches_native((uintptr_t)node));
    assert(!esp32_mquickjs_wifi_twt_sdk_broadcast_node_matches_native(0));
    assert(!esp32_mquickjs_wifi_twt_sdk_broadcast_node_matches_native((uintptr_t)node+1));
    state=4;memcpy(station+152,&state,sizeof(state));
    assert(!esp32_mquickjs_wifi_twt_sdk_broadcast_node_matches_native((uintptr_t)node));
    state=5;memcpy(station+152,&state,sizeof(state));

    /* Production broadcast capture/match: exact table address, parameter
     * bytes, association, representable interval and explicit PS policy. */
    esp32_mquickjs_wifi_btwt_timer_identity_t bt_native;
    uint8_t *bt_param=btwt_setup_timer+704+17*31;
    memset(bt_param,0,17);bt_param[0]=216;bt_param[1]=10;bt_param[2]=12;bt_param[10]=31<<3;
    ioctl_task=true;
    assert(esp32_mquickjs_wifi_twt_sdk_broadcast_timer_capture_native(31,30,bt_param,&bt_native));
    assert(!esp32_mquickjs_wifi_twt_sdk_broadcast_timer_capture_native(30,30,bt_param,&bt_native));
    assert(!esp32_mquickjs_wifi_twt_sdk_broadcast_timer_capture_native(32,30,bt_param,&bt_native));
    bt_param[3]=TWT_ACCEPT<<1;bt_param[7]=1;bt_param[8]=255;bt_param[9]=255;bt_param[4]=14<<2;
    for(unsigned offset=1180;offset<=1188;offset+=4){uint32_t table=0x123400;memcpy(node+offset,&table,4);}
    assert(esp32_mquickjs_wifi_twt_sdk_broadcast_timer_capture_native(31,31,bt_param,&bt_native));
    assert(esp32_mquickjs_wifi_twt_sdk_broadcast_timer_matches_native(31,31,&bt_native));
    bt_param[8]^=1;assert(!esp32_mquickjs_wifi_twt_sdk_broadcast_timer_matches_native(31,31,&bt_native));bt_param[8]^=1;
    bt_param[4]=16<<2;assert(!esp32_mquickjs_wifi_twt_sdk_broadcast_timer_capture_native(31,31,bt_param,&bt_native));
    bt_param[4]=14<<2;sleep_type=0;assert(!esp32_mquickjs_wifi_twt_sdk_broadcast_timer_capture_native(31,31,bt_param,&bt_native));
    sleep_type=1;bt_param[2]|=32;assert(!esp32_mquickjs_wifi_twt_sdk_broadcast_timer_capture_native(31,31,bt_param,&bt_native));
    bt_param[2]&=~32;node[1180]=node[1181]=node[1182]=node[1183]=0;
    assert(!esp32_mquickjs_wifi_twt_sdk_broadcast_timer_capture_native(31,31,bt_param,&bt_native));
    memset(btwt_setup_timer,0,sizeof(btwt_setup_timer));ioctl_task=true;
    wake_snapshot.held=true;assert(esp32_mquickjs_wifi_twt_sdk_probe_admit_native()==ESP_ERR_INVALID_STATE);
    wake_snapshot.held=false;wake_snapshot.acquiring=true;
    assert(esp32_mquickjs_wifi_twt_sdk_probe_admit_native()==ESP_ERR_INVALID_STATE);
    wake_snapshot.acquiring=false;wake_snapshot.releasing=true;
    assert(esp32_mquickjs_wifi_twt_sdk_probe_admit_native()==ESP_ERR_INVALID_STATE);
    wake_snapshot.releasing=false;wake_snapshot.fault=84;
    assert(esp32_mquickjs_wifi_twt_sdk_probe_admit_native()==84);wake_snapshot.fault=0;
    node[1056]=1;assert(esp32_mquickjs_wifi_twt_sdk_probe_admit_native()==ESP_ERR_INVALID_STATE);
    node[1056]=0;timer(&itwt_probe_timer,0);
    assert(esp32_mquickjs_wifi_twt_sdk_probe_admit_native()==ESP_ERR_INVALID_STATE);
    memset(&itwt_probe_timer,0,sizeof(itwt_probe_timer));
    assert(esp32_mquickjs_wifi_twt_sdk_probe_admit_native()==ESP_OK);
    uintptr_t captured_node=0;uint8_t captured_phase=9;
    node[1056]=1;node[1057]=1;
    esp32_mquickjs_wifi_twt_sdk_snapshot_native(&snapshot);
    assert(snapshot.probe_active && snapshot.probe_phase==1);
    assert(!esp32_mquickjs_wifi_twt_sdk_probe_capture_native((void *)1,&captured_node,&captured_phase));
    assert(esp32_mquickjs_wifi_twt_sdk_probe_capture_native(node+1057,&captured_node,&captured_phase));
    assert(captured_node==(uintptr_t)node && captured_phase==1);
    assert(esp32_mquickjs_wifi_twt_sdk_probe_matches_native(captured_node,1));
    assert(!esp32_mquickjs_wifi_twt_sdk_probe_matches_native(captured_node+1,1));
    assert(!esp32_mquickjs_wifi_twt_sdk_probe_matches_native(captured_node,0));
    assert(!esp32_mquickjs_wifi_twt_sdk_probe_abort_native(captured_node+1,1));
    assert(!esp32_mquickjs_wifi_twt_sdk_probe_abort_native(captured_node,0) && wake_releases==0);
    assert(esp32_mquickjs_wifi_twt_sdk_probe_abort_native(captured_node,1) && wake_releases==1 && !node[1056]);
    esp32_mquickjs_wifi_twt_sdk_snapshot_native(&snapshot);
    assert(!snapshot.probe_active && snapshot.probe_phase==UINT8_MAX);
    assert(!esp32_mquickjs_wifi_twt_sdk_probe_abort_native(captured_node,1) && wake_releases==1);
    node[1056]=0;assert(!esp32_mquickjs_wifi_twt_sdk_probe_matches_native(captured_node,1));
    probe_timer_snapshot.fault=ESP_ERR_NO_MEM;
    assert(esp32_mquickjs_wifi_twt_sdk_probe_admit_native()==ESP_ERR_NO_MEM);
    probe_timer_snapshot.fault=0;probe_timer_snapshot.post_error=89;
    assert(esp32_mquickjs_wifi_twt_sdk_probe_admit_native()==ESP_ERR_INVALID_STATE);
    ioctl_task=false;
    unsigned before_queries=queries;
    assert(esp32_mquickjs_wifi_twt_sdk_probe_cancel(0)==ESP_ERR_INVALID_ARG && queries==before_queries);
    assert(esp32_mquickjs_wifi_twt_sdk_probe_cancel(1)==ESP_ERR_INVALID_STATE && cancels==0);
    uint32_t identity=0;assert(esp32_mquickjs_wifi_twt_probe_result_begin_native(&identity)==ESP_OK);
    assert(esp32_mquickjs_wifi_twt_sdk_probe_cancel(identity)==ESP_ERR_INVALID_STATE && cancels==0);
    esp32_mquickjs_wifi_twt_probe_result_submitted_native(identity,0);
    assert(esp32_mquickjs_wifi_twt_sdk_probe_cancel(identity+1)==ESP_ERR_INVALID_STATE && cancels==0);
    dispatch_error=93;
    assert(esp32_mquickjs_wifi_twt_sdk_probe_cancel(identity)==93 && cancels==0 && !s_probe_result.cancel_requested);
    dispatch_error=0;cancel_error=94;
    assert(esp32_mquickjs_wifi_twt_sdk_probe_cancel(identity)==94 && cancels==1);
    assert(s_probe_result.cancel_requested && !s_probe_result.cancel_complete && s_probe_result.cancel_error==94);
    cancel_error=0;
    assert(esp32_mquickjs_wifi_twt_sdk_probe_cancel(identity)==ESP_OK && cancels==2);
    assert(s_probe_result.cancel_complete && s_probe_result.cancel_error==0);
    action_sdk_twt_probe_cancel_request_t cancellation={.request={
        .ifx=WIFI_IF_AP,.type=ACTION_SDK_TWT_PROBE_CANCEL_TYPE,.rx_cb=esp32_mquickjs_wifi_action_receive},.identity=identity};
    assert(esp_wifi_action_tx_req(&cancellation.request)==ESP_ERR_INVALID_ARG && cancels==2);
    cancellation.request.ifx=WIFI_IF_STA;cancellation.request.rx_cb=other_receive;
    unsigned before_delegates=delegates;
    assert(esp_wifi_action_tx_req(&cancellation.request)==77 && delegates==before_delegates+1 && cancels==2);
    assert(!ioctl_task);
    /* Capture and match the production pending-table identity; old node values
     * are compared only, and duplicate live dialogs are ambiguous. */
    ioctl_task=true;empty_tables();
    memcpy(g_ic+16,&sp,sizeof(sp));memcpy(station+228,&np,sizeof(np));
    state=5;memcpy(station+152,&state,sizeof(state));
    {
        uint8_t control=3;
        esp32_mquickjs_wifi_twt_information_identity_t saved;
        assert(!esp32_mquickjs_wifi_twt_sdk_information_capture_native(3,&control,&saved));
        s_itwt_flow_id_bitmap=8;s_itwt_id[3]=123;
        assert(esp32_mquickjs_wifi_twt_sdk_information_capture_native(3,&control,&saved));
        assert(saved.flows==8 && saved.request_ids[3]==123 && saved.node==(uintptr_t)node);
        assert(esp32_mquickjs_wifi_twt_sdk_information_matches_native(3,&saved));
        s_itwt_id[3]=124;assert(!esp32_mquickjs_wifi_twt_sdk_information_matches_native(3,&saved));
        s_itwt_id[3]=123;s_itwt_flow_id_bitmap=24;s_itwt_id[4]=125;
        assert(esp32_mquickjs_wifi_twt_sdk_information_matches_native(3,&saved)); /* Single flow. */
        control=0x83;assert(esp32_mquickjs_wifi_twt_sdk_information_capture_native(3,&control,&saved));
        assert(saved.flows==24);s_itwt_id[4]=126;
        assert(!esp32_mquickjs_wifi_twt_sdk_information_matches_native(3,&saved)); /* All flows. */
        s_itwt_id[4]=125;s_itwt_flow_id_bitmap=8;
        assert(!esp32_mquickjs_wifi_twt_sdk_information_matches_native(3,&saved));
        state=0;memcpy(station+152,&state,4);
        assert(!esp32_mquickjs_wifi_twt_sdk_information_capture_native(3,&control,&saved));
        state=5;memcpy(station+152,&state,4);empty_tables();
    }
    for(unsigned i=0;i<8;++i) {
        pending(i,i,(int16_t)(400+i));setup_timer_param[136+i]=(uint8_t)(10+i);
    }
    for(unsigned i=0;i<8;++i) {
        esp32_mquickjs_wifi_twt_setup_timer_identity_t saved={0};
        assert(esp32_mquickjs_wifi_twt_sdk_setup_timer_capture_native(i,26,setup_timer_param+136+i,&saved));
        assert(saved.request_id==(int)(400+i) && saved.flow==i && saved.dialog==10+i && saved.node==(uintptr_t)node);
        assert(esp32_mquickjs_wifi_twt_sdk_setup_timer_matches_native(i,26,&saved));
        assert(!esp32_mquickjs_wifi_twt_sdk_setup_timer_matches_native(i,27,&saved));
        assert(!esp32_mquickjs_wifi_twt_sdk_setup_tx_matches_native(i,&saved));
        s_tmp_itwt_id[i]=saved.request_id;
        assert(esp32_mquickjs_wifi_twt_sdk_setup_tx_matches_native(i,&saved));
        ++s_tmp_itwt_id[i];assert(!esp32_mquickjs_wifi_twt_sdk_setup_tx_matches_native(i,&saved));
        --s_tmp_itwt_id[i];
        uint32_t phase=1;memcpy(setup_timer_param+336+4*i,&phase,4);
        assert(!esp32_mquickjs_wifi_twt_sdk_setup_tx_matches_native(i,&saved));
        phase=2;memcpy(setup_timer_param+336+4*i,&phase,4);
        assert(esp32_mquickjs_wifi_twt_sdk_setup_timer_matches_native(i,27,&saved));
        assert(!esp32_mquickjs_wifi_twt_sdk_setup_timer_matches_native(i,26,&saved));
        setup_timer_param[136+i]++;
        assert(!esp32_mquickjs_wifi_twt_sdk_setup_timer_matches_native(i,27,&saved));
        setup_timer_param[136+i]--;phase=0;memcpy(setup_timer_param+336+4*i,&phase,4);
        ++saved.request_id;assert(!esp32_mquickjs_wifi_twt_sdk_setup_timer_matches_native(i,26,&saved));
    }
    esp32_mquickjs_wifi_twt_setup_timer_identity_t captured;
    setup_timer_param[137]=setup_timer_param[136];
    assert(!esp32_mquickjs_wifi_twt_sdk_setup_timer_capture_native(0,26,setup_timer_param+136,&captured));
    setup_timer_param[137]=11;state=0;memcpy(station+152,&state,4);
    assert(!esp32_mquickjs_wifi_twt_sdk_setup_timer_capture_native(0,26,setup_timer_param+136,&captured));
    state=5;memcpy(station+152,&state,4);
    assert(!esp32_mquickjs_wifi_twt_sdk_setup_timer_capture_native(8,26,setup_timer_param+144,&captured));
    setup_snapshot.fault=96;request=config();memcpy(message+12,&pointer,sizeof(pointer));
    unsigned before_setup=individual_calls;
    assert(__wrap_wifi_sta_itwt_setup_process(message)==96 && individual_calls==before_setup);
    setup_snapshot.fault=0;empty_tables();
    setup_result_error=97;unsigned before_reserve=setup_result_begins;
    assert(__wrap_wifi_sta_itwt_setup_process(message)==97 && individual_calls==before_setup);
    assert(setup_result_begins==before_reserve+1);
    setup_result_error=0;forward_error=98;
    unsigned before_submission=setup_result_submissions;
    assert(__wrap_wifi_sta_itwt_setup_process(message)==98 && individual_calls==before_setup+1);
    assert(setup_result_submissions==before_submission+1 && setup_result_submit_error==98);
    forward_error=0;empty_tables();ioctl_task=false;
    uint32_t managed=0;
    assert(esp32_mquickjs_wifi_twt_sdk_probe_submit(0,&managed)==ESP_ERR_INVALID_ARG && !probe_submits);
    assert(esp32_mquickjs_wifi_twt_sdk_probe_submit(60001,&managed)==ESP_ERR_INVALID_ARG && !probe_submits);
    probe_submit_error=95;
    assert(esp32_mquickjs_wifi_twt_sdk_probe_submit(123,&managed)==95 && managed==0 && probe_submits==1);
    accept_probe=true;
    assert(esp32_mquickjs_wifi_twt_sdk_probe_submit(123,&managed)==95 && managed==identity+1 && s_probe_result.owned);
    uint32_t blocked=0;
    assert(esp32_mquickjs_wifi_twt_sdk_probe_submit(123,&blocked)==ESP_ERR_INVALID_STATE && blocked==0 && probe_submits==2);
    assert(esp32_mquickjs_wifi_twt_probe_result_begin_native(&blocked)==ESP_ERR_INVALID_STATE);
    ioctl_task=true;
    assert(esp32_mquickjs_wifi_twt_sdk_probe_admit_native()==ESP_ERR_INVALID_STATE);
    ioctl_task=false;
    empty_tables();probe_timer_snapshot=(esp32_mquickjs_wifi_twt_probe_timer_snapshot_t){0};
    wake_snapshot=(esp32_mquickjs_wifi_twt_probe_wake_snapshot_t){0};
    esp32_mquickjs_wifi_twt_probe_cut_t cut={77,88},cut_before=cut;
    assert(esp32_mquickjs_wifi_twt_sdk_probe_quiescent(managed,&cut)==ESP_ERR_NOT_FINISHED);
    assert(!memcmp(&cut,&cut_before,sizeof(cut)));
    assert(esp32_mquickjs_wifi_twt_sdk_probe_cancel(managed)==ESP_OK);
    tx_snapshot.probe_buffer_present=true;
    assert(esp32_mquickjs_wifi_twt_sdk_probe_quiescent(managed,&cut)==ESP_ERR_NOT_FINISHED);
    tx_snapshot.probe_buffer_present=false;tx_snapshot.probe_calls=1;
    assert(esp32_mquickjs_wifi_twt_sdk_probe_quiescent(managed,&cut)==ESP_ERR_NOT_FINISHED);
    tx_snapshot.probe_calls=0;tx_snapshot.revision=100;probe_timer_snapshot.last_identity=8;
    assert(esp32_mquickjs_wifi_twt_sdk_probe_quiescent(managed,&cut)==ESP_OK && cut.tx_revision==100 && cut.timer_identity==8);
    uint32_t sequence=0;
    assert(esp32_mquickjs_wifi_twt_probe_result_post_fence(managed,&sequence)==ESP_OK);
    assert(esp32_mquickjs_wifi_twt_sdk_probe_release(managed,&cut,sequence)==ESP_ERR_NOT_FINISHED && s_probe_result.owned);
    esp32_mquickjs_wifi_twt_probe_event_fence_t event={managed,sequence};
    esp32_mquickjs_wifi_twt_probe_result_observe_fence(&event);
    ++tx_snapshot.revision;
    assert(esp32_mquickjs_wifi_twt_sdk_probe_release(managed,&cut,sequence)==ESP_ERR_NOT_FINISHED && s_probe_result.owned);
    --tx_snapshot.revision;tx_snapshot.fault=ESP32_MQUICKJS_WIFI_TWT_TX_CAPACITY;
    assert(esp32_mquickjs_wifi_twt_sdk_probe_release(managed,&cut,sequence)==ESP_ERR_INVALID_STATE && s_probe_result.owned);
    tx_snapshot.fault=0;
    assert(esp32_mquickjs_wifi_twt_sdk_probe_release(managed,&cut,sequence)==ESP_OK && !s_probe_result.owned);
    assert(esp32_mquickjs_wifi_twt_sdk_probe_release(managed,&cut,sequence)==ESP_ERR_INVALID_STATE);
    empty_tables();cancel_result=(esp32_mquickjs_wifi_twt_setup_result_t){.identity=101,.request_id=410,
        .flags=ESP32_MQUICKJS_WIFI_TWT_SETUP_SUBMITTING};
    assert(esp32_mquickjs_wifi_twt_sdk_setup_cancel(0)==ESP_ERR_INVALID_ARG);
    assert(esp32_mquickjs_wifi_twt_sdk_setup_cancel(100)==ESP_ERR_INVALID_STATE && !setup_cancels);
    assert(esp32_mquickjs_wifi_twt_sdk_setup_cancel(101)==ESP_ERR_INVALID_STATE && !setup_cancels);
    cancel_result.flags=ESP32_MQUICKJS_WIFI_TWT_SETUP_SUBMITTED;
    pending(2,3,410);pending(4,6,411);s_tmp_itwt_id[3]=410;s_tmp_itwt_id[6]=411;
    setup_cancel_error=92;
    assert(esp32_mquickjs_wifi_twt_sdk_setup_cancel(101)==92 && cancelled_pending==4);
    assert(setup_timer_param[368]==20 && s_tmp_itwt_id[3]==410 && s_tmp_itwt_id[6]==411);
    setup_cancel_error=0;
    assert(esp32_mquickjs_wifi_twt_sdk_setup_cancel(101)==ESP_OK && setup_timer_param[368]==16);
    assert(s_tmp_itwt_id[3]==-1 && s_tmp_itwt_id[6]==411 && cancel_result.identity==101);
    assert(esp32_mquickjs_wifi_twt_sdk_setup_cancel(101)==ESP_OK && cancelled_pending==0);
    unsigned cancelled=setup_cancels;
    s_itwt_id[3]=410;s_itwt_flow_id_bitmap=8;
    assert(esp32_mquickjs_wifi_twt_sdk_setup_cancel(101)==ESP_ERR_NOT_FINISHED && setup_cancels==cancelled);
    empty_tables();cancel_result.flags|=ESP32_MQUICKJS_WIFI_TWT_SETUP_SEEN;cancel_result.event.config.flow_id=5;
    s_itwt_flow_id_bitmap=32; /* SDK invalid-AP branch leaves bitmap without ID. */
    assert(esp32_mquickjs_wifi_twt_sdk_setup_cancel(101)==ESP_ERR_INVALID_STATE && setup_cancels==cancelled);
    empty_tables();cancel_result.flags=ESP32_MQUICKJS_WIFI_TWT_SETUP_SUBMITTED;
    pending(1,3,410);s_tmp_itwt_id[3]=411;
    assert(esp32_mquickjs_wifi_twt_sdk_setup_cancel(101)==ESP_ERR_INVALID_STATE && setup_cancels==cancelled);
    s_tmp_itwt_id[3]=410;pending(4,3,411);
    assert(esp32_mquickjs_wifi_twt_sdk_setup_cancel(101)==ESP_ERR_INVALID_STATE && setup_cancels==cancelled);
    setup_timer_param[368]&=(uint8_t)~(1U<<4);
    s_tmp_itwt_id[3]=410;pending(2,4,410);
    assert(esp32_mquickjs_wifi_twt_sdk_setup_cancel(101)==ESP_ERR_INVALID_STATE && setup_cancels==cancelled);
    empty_tables();pending(1,3,410);uint32_t dwell=2;memcpy(setup_timer_param+336+4,&dwell,4);
    assert(esp32_mquickjs_wifi_twt_sdk_setup_cancel(101)==ESP_OK && setup_timer_param[368]==0);
    memcpy(&dwell,setup_timer_param+336+4,4);assert(dwell==0);
    cancellation.request.type=ACTION_SDK_TWT_SETUP_CANCEL_TYPE;cancellation.request.rx_cb=esp32_mquickjs_wifi_action_receive;
    cancellation.request.ifx=WIFI_IF_AP;cancellation.identity=101;cancelled=setup_cancels;
    assert(esp_wifi_action_tx_req(&cancellation.request)==ESP_ERR_INVALID_ARG && setup_cancels==cancelled);
    cancellation.request.ifx=WIFI_IF_STA;cancellation.request.rx_cb=other_receive;
    before_delegates=delegates;
    assert(esp_wifi_action_tx_req(&cancellation.request)==77 && delegates==before_delegates+1 && setup_cancels==cancelled);
    empty_tables();cancel_result.cancelled_flows=8;
    cancel_result.flags=ESP32_MQUICKJS_WIFI_TWT_SETUP_CANCELLED;cancel_result.revision=5;
    tx_snapshot=(esp32_mquickjs_wifi_twt_tx_snapshot_t){.revision=17};setup_snapshot.revision=3;
    esp32_mquickjs_wifi_twt_setup_cut_t setup_cut={.tx_revision=111,.timer_revision=222};
    assert(esp32_mquickjs_wifi_twt_sdk_setup_quiescent(0,&setup_cut)==ESP_ERR_INVALID_ARG);
    assert(esp32_mquickjs_wifi_twt_sdk_setup_quiescent(100,&setup_cut)==ESP_ERR_INVALID_STATE && setup_cut.tx_revision==111);
    cancel_result.flags=0;
    assert(esp32_mquickjs_wifi_twt_sdk_setup_quiescent(101,&setup_cut)==ESP_ERR_NOT_FINISHED);
    cancel_result.flags=ESP32_MQUICKJS_WIFI_TWT_SETUP_CANCELLED;cancel_result.observation_calls=1;
    assert(esp32_mquickjs_wifi_twt_sdk_setup_quiescent(101,&setup_cut)==ESP_ERR_NOT_FINISHED);
    cancel_result.observation_calls=0;s_itwt_id[4]=410;
    assert(esp32_mquickjs_wifi_twt_sdk_setup_quiescent(101,&setup_cut)==ESP_ERR_NOT_FINISHED);
    empty_tables();s_itwt_flow_id_bitmap=8;
    assert(esp32_mquickjs_wifi_twt_sdk_setup_quiescent(101,&setup_cut)==ESP_ERR_INVALID_STATE);
    empty_tables();s_itwt_suspend_flow_id_bitmap=8;
    assert(esp32_mquickjs_wifi_twt_sdk_setup_quiescent(101,&setup_cut)==ESP_ERR_INVALID_STATE);
    empty_tables();pending(2,4,410);
    assert(esp32_mquickjs_wifi_twt_sdk_setup_quiescent(101,&setup_cut)==ESP_ERR_NOT_FINISHED);
    empty_tables();pending(2,3,411);
    assert(esp32_mquickjs_wifi_twt_sdk_setup_quiescent(101,&setup_cut)==ESP_ERR_INVALID_STATE);
    empty_tables();tx_snapshot.tracked=1;
    assert(esp32_mquickjs_wifi_twt_sdk_setup_quiescent(101,&setup_cut)==ESP_ERR_NOT_FINISHED);
    tx_snapshot.tracked=0;tx_snapshot.fault=ESP32_MQUICKJS_WIFI_TWT_TX_CAPACITY;
    assert(esp32_mquickjs_wifi_twt_sdk_setup_quiescent(101,&setup_cut)==ESP_ERR_INVALID_STATE);
    tx_snapshot.fault=0;setup_timer_query_error=ESP_ERR_NOT_FINISHED;
    assert(esp32_mquickjs_wifi_twt_sdk_setup_quiescent(101,&setup_cut)==ESP_ERR_NOT_FINISHED && setup_cut.tx_revision==111);
    setup_timer_query_error=0;
    assert(esp32_mquickjs_wifi_twt_sdk_setup_quiescent(101,&setup_cut)==0 && setup_cut.tx_revision==17 && setup_cut.timer_revision==3);
    assert(setup_cut.information_revision==5 && !setup_cut.teardown_revision);
    ++information_revision;
    assert(esp32_mquickjs_wifi_twt_sdk_setup_release(101,&setup_cut,7)==ESP_ERR_NOT_FINISHED && !setup_releases);
    --information_revision;
    ++setup_snapshot.revision;
    assert(esp32_mquickjs_wifi_twt_sdk_setup_release(101,&setup_cut,7)==ESP_ERR_NOT_FINISHED && !setup_releases);
    --setup_snapshot.revision;++tx_snapshot.revision;
    assert(esp32_mquickjs_wifi_twt_sdk_setup_release(101,&setup_cut,7)==ESP_ERR_NOT_FINISHED && !setup_releases);
    --tx_snapshot.revision;
    assert(esp32_mquickjs_wifi_twt_sdk_setup_release(101,&setup_cut,7)==ESP_ERR_NOT_FINISHED && setup_releases==1);
    setup_release_ready=true;
    assert(esp32_mquickjs_wifi_twt_sdk_setup_release(101,&setup_cut,7)==0 && setup_releases==2);
    esp32_mquickjs_wifi_btwt_cut_t btcut={111,222};
    assert(!esp32_mquickjs_wifi_twt_sdk_broadcast_teardown(3,777) && btwt_teardown_calls==1);
    btwt_owner=false;assert(esp32_mquickjs_wifi_twt_sdk_broadcast_teardown(3,777)==ESP_ERR_INVALID_STATE && btwt_teardown_calls==1);
    btwt_owner=true;btwt_control_error=91;
    assert(esp32_mquickjs_wifi_twt_sdk_broadcast_teardown(3,777)==91 && btwt_teardown_calls==1);btwt_control_error=0;
    assert(esp32_mquickjs_wifi_twt_sdk_broadcast_teardown(32,777)==ESP_ERR_INVALID_ARG);
    esp32_mquickjs_wifi_twt_sdk_broadcast_teardown_complete_native(17,3,1);assert(btwt_teardown_completions==1);
    assert(!esp32_mquickjs_wifi_twt_sdk_broadcast_cancel(3,777) && btwt_control_calls==1);
    assert(!esp32_mquickjs_wifi_twt_sdk_broadcast_quiescent(3,777,&btcut) && btcut.tx_revision==17 && btcut.timer_revision==23 && btcut.teardown_revision==31);
    btwt_control_error=91;btcut=(esp32_mquickjs_wifi_btwt_cut_t){111,222};
    assert(esp32_mquickjs_wifi_twt_sdk_broadcast_quiescent(3,777,&btcut)==91 && btcut.tx_revision==111 && btcut.timer_revision==222);
    assert(esp32_mquickjs_wifi_twt_sdk_broadcast_cancel(32,777)==ESP_ERR_INVALID_ARG && btwt_control_calls==3);
    btwt_control_error=0;
    action_sdk_btwt_control_t btcontrol={.request={.ifx=WIFI_IF_AP,.type=ACTION_SDK_BTWT_CONTROL_TYPE,
        .rx_cb=esp32_mquickjs_wifi_action_receive},.command=BTWT_CANCEL,.slot=3,.identity=777};
    assert(esp_wifi_action_tx_req(&btcontrol.request)==ESP_ERR_INVALID_ARG && btwt_control_calls==3);
    btcontrol.request.ifx=WIFI_IF_STA;btcontrol.command=99;
    assert(esp_wifi_action_tx_req(&btcontrol.request)==ESP_ERR_INVALID_ARG && btwt_control_calls==3);
    btcut=(esp32_mquickjs_wifi_btwt_cut_t){17,23,31};
    assert(!esp32_mquickjs_wifi_twt_sdk_broadcast_release(3,777,&btcut,9) && btwt_control_calls==4);
    assert(esp32_mquickjs_wifi_twt_sdk_broadcast_release(3,777,&btcut,0)==ESP_ERR_INVALID_ARG && btwt_control_calls==4);
    action_sdk_twt_setup_control_t control={.request={.ifx=WIFI_IF_AP,.type=ACTION_SDK_TWT_SETUP_CONTROL_TYPE,
        .rx_cb=esp32_mquickjs_wifi_action_receive},.command=TWT_SETUP_QUIESCENT,.identity=101};
    assert(esp_wifi_action_tx_req(&control.request)==ESP_ERR_INVALID_ARG);
    control.request.ifx=WIFI_IF_STA;control.command=99;
    assert(esp_wifi_action_tx_req(&control.request)==ESP_ERR_INVALID_ARG);
    empty_tables();s_itwt_id[3]=410;s_itwt_flow_id_bitmap=8;
    cancel_result.flags=ESP32_MQUICKJS_WIFI_TWT_SETUP_SEEN;
    assert(esp32_mquickjs_wifi_twt_sdk_setup_teardown(0,3)==ESP_ERR_INVALID_ARG);
    assert(esp32_mquickjs_wifi_twt_sdk_setup_teardown(101,8)==ESP_ERR_INVALID_ARG);
    assert(esp32_mquickjs_wifi_twt_sdk_setup_teardown(100,3)==ESP_ERR_INVALID_STATE && !teardown_calls);
    uint8_t *saved_station;memcpy(&saved_station,g_ic+16,sizeof(saved_station));
    uint8_t *none=NULL;memcpy(g_ic+16,&none,sizeof(none));
    assert(esp32_mquickjs_wifi_twt_sdk_setup_teardown(101,3)==ESP_ERR_WIFI_NOT_ASSOC);
    memcpy(g_ic+16,&saved_station,sizeof(saved_station));
    uint32_t associated_state=5;memcpy(saved_station+152,&associated_state,4);
    s_itwt_id[3]=411;
    assert(esp32_mquickjs_wifi_twt_sdk_setup_teardown(101,3)==ESP_ERR_INVALID_STATE);
    s_itwt_id[3]=410;pending(1,3,411);
    assert(esp32_mquickjs_wifi_twt_sdk_setup_teardown(101,3)==ESP_ERR_INVALID_STATE);
    setup_timer_param[368]=0;s_itwt_id[4]=410;
    assert(esp32_mquickjs_wifi_twt_sdk_setup_teardown(101,3)==ESP_ERR_INVALID_STATE);
    s_itwt_id[4]=-1;teardown_begin_error=81;
    assert(esp32_mquickjs_wifi_twt_sdk_setup_teardown(101,3)==81 && !teardown_calls);
    assert(teardown_tx_begins==1 && teardown_tx_abandons==1 && !teardown_tx_ends);
    teardown_begin_error=0;teardown_driver_error=82;
    assert(esp32_mquickjs_wifi_twt_sdk_setup_teardown(101,3)==82 && teardown_calls==1 && teardown_finishes==1);
    assert(cancel_result.teardown_error==82 && (cancel_result.flags&ESP32_MQUICKJS_WIFI_TWT_TEARDOWN_ATTEMPTED));
    assert(teardown_tx_ends==1);cancel_result.teardown_flow=3;
    ioctl_task=true;
    assert(esp32_mquickjs_wifi_twt_sdk_teardown_tx_matches_native(101,(uintptr_t)node,3));
    s_itwt_id[3]=411;assert(!esp32_mquickjs_wifi_twt_sdk_teardown_tx_matches_native(101,(uintptr_t)node,3));
    s_itwt_id[3]=410;assert(!esp32_mquickjs_wifi_twt_sdk_teardown_tx_matches_native(101,(uintptr_t)node+1,3));
    ioctl_task=false;uint32_t td_revision=111;
    assert(esp32_mquickjs_wifi_twt_sdk_teardown_tx_quiescent(101,&td_revision)==0 && td_revision==17);
    assert(esp32_mquickjs_wifi_twt_sdk_teardown_tx_release(101,16)==ESP_ERR_NOT_FINISHED);
    assert(esp32_mquickjs_wifi_twt_sdk_teardown_tx_release(101,17)==0);
    empty_tables();cancel_result.flags|=ESP32_MQUICKJS_WIFI_TWT_SETUP_CANCELLED;
    assert(esp32_mquickjs_wifi_twt_sdk_setup_quiescent(101,&setup_cut)==ESP_ERR_NOT_FINISHED);
    cancel_result.flags|=ESP32_MQUICKJS_WIFI_TWT_TEARDOWN_SEEN;
    cancel_result.teardown_status=1;
    information_query_error=84;
    assert(esp32_mquickjs_wifi_twt_sdk_setup_quiescent(101,&setup_cut)==84);
    information_query_error=0;
    assert(esp32_mquickjs_wifi_twt_sdk_setup_quiescent(101,&setup_cut)==0 && setup_cut.teardown_revision==17);
    ++setup_cut.teardown_revision;
    assert(esp32_mquickjs_wifi_twt_sdk_setup_release(101,&setup_cut,7)==ESP_ERR_NOT_FINISHED);
    --setup_cut.teardown_revision;
    cancel_result.flags|=ESP32_MQUICKJS_WIFI_TWT_SETUP_AMBIGUOUS;
    assert(esp32_mquickjs_wifi_twt_sdk_setup_quiescent(101,&setup_cut)==ESP_ERR_NOT_FINISHED);
    /* A real connection-close indication relaxes only the missing/failed RF
     * teardown result; native TX/timer/query failures still retain the owner. */
    cancel_result.flags|=ESP32_MQUICKJS_WIFI_TWT_SETUP_NATIVE_CLOSED;
    cancel_result.flags&=~ESP32_MQUICKJS_WIFI_TWT_TEARDOWN_SEEN;
    cancel_result.teardown_error=82;
    assert(!esp32_mquickjs_wifi_twt_sdk_setup_cancel(101));
    information_query_error=85;
    assert(esp32_mquickjs_wifi_twt_sdk_setup_quiescent(101,&setup_cut)==85);
    information_query_error=0;tx_snapshot.tracked=1;
    assert(esp32_mquickjs_wifi_twt_sdk_setup_quiescent(101,&setup_cut)==ESP_ERR_NOT_FINISHED);
    tx_snapshot.tracked=0;tx_snapshot.fault=ESP32_MQUICKJS_WIFI_TWT_TX_CAPACITY;
    assert(esp32_mquickjs_wifi_twt_sdk_setup_quiescent(101,&setup_cut)==ESP_ERR_INVALID_STATE);
    tx_snapshot.fault=ESP32_MQUICKJS_WIFI_TWT_TX_OK;
    assert(!esp32_mquickjs_wifi_twt_sdk_setup_quiescent(101,&setup_cut));
    assert(cancel_result.teardown_error==82 && !(cancel_result.flags&ESP32_MQUICKJS_WIFI_TWT_TEARDOWN_SEEN));
    assert(esp32_mquickjs_wifi_twt_sdk_setup_teardown(101,3)==ESP_ERR_INVALID_STATE);
    /* Actual native admission + private queue dispatch for explicit resume. */
    empty_tables();information_query_error=0;
    memcpy(g_ic+16,&sp,sizeof(sp));memcpy(station+228,&np,sizeof(np));state=5;memcpy(station+152,&state,4);
    node[1176]=0;s_itwt_id[3]=123;s_itwt_flow_id_bitmap=8;s_itwt_suspend_flow_id_bitmap=8;
    cancel_result=(esp32_mquickjs_wifi_twt_setup_result_t){.identity=111,.request_id=123,
        .flags=ESP32_MQUICKJS_WIFI_TWT_SETUP_SEEN,.event={.status=1,.config={.setup_cmd=TWT_ACCEPT}}};
    uint32_t info_id=0;
    sleep_type=0;assert(esp32_mquickjs_wifi_twt_sdk_information_submit(111,0,true,&info_id)==ESP_ERR_INVALID_STATE && !information_begins);
    sleep_type=1;information_replace_error=91;
    assert(esp32_mquickjs_wifi_twt_sdk_information_submit(111,0,true,&info_id)==91 && !information_begins);
    information_replace_error=0;information_tx_error=ESP_ERR_NOT_FINISHED;
    assert(esp32_mquickjs_wifi_twt_sdk_information_submit(111,0,true,&info_id)==ESP_ERR_NOT_FINISHED && !information_begins);
    information_tx_error=0;
    assert(!esp32_mquickjs_wifi_twt_sdk_information_submit(111,0,true,&info_id) && info_id==77 && information_begins==1 && information_resumes==1 && information_submits==1);
    assert(s_itwt_suspend_flow_id_bitmap==8); /* Admission itself never flips a bitmap. */
    info_id=0;
    assert(esp32_mquickjs_wifi_twt_sdk_information_submit(111,120,false,&info_id)==ESP_ERR_INVALID_STATE && !info_id);
    assert(esp32_mquickjs_wifi_twt_sdk_information_submit(111,1,true,&info_id)==ESP_ERR_INVALID_ARG);
    s_itwt_suspend_flow_id_bitmap=0;
    assert(esp32_mquickjs_wifi_twt_sdk_information_submit(111,0,true,&info_id)==ESP_ERR_INVALID_STATE);
    assert(!esp32_mquickjs_wifi_twt_sdk_information_submit(111,120,false,&info_id) && info_id==77 && information_begins==2);
    return 0;
}
'''

# Result storage and frame transmission are SDK boundaries in this fixture;
# actual result/TX/timer scheduling have their own production-source fixtures.
BOUNDARIES += r'''
static int sleep_type=1,information_replace_error,information_tx_error,information_send_error;
static unsigned information_begins,information_submits,information_resumes;
static bool information_resume;
int pm_get_sleep_type(void){assert(ioctl_task);return sleep_type;}
esp_err_t esp32_mquickjs_wifi_twt_information_begin_native(uint32_t id,
    const esp32_mquickjs_wifi_twt_information_identity_t *native,uint32_t ms,bool resume,uint32_t *out){
    assert(ioctl_task && !locked && id==cancel_result.identity && native->node && native->flows==8);
    assert(native->request_ids[3]==123 && ms==(resume?0U:120U));
    information_resume=resume;*out=77;++information_begins;return 0;
}
void esp32_mquickjs_wifi_twt_information_submitted_native(uint32_t id,esp_err_t error){
    assert(ioctl_task && id==77 && error==information_send_error);++information_submits;
}
esp_err_t esp32_mquickjs_wifi_twt_information_reap_native(uint32_t id){assert(ioctl_task);(void)id;return 0;}
esp_err_t esp32_mquickjs_wifi_twt_tx_information_quiescent_native(uint32_t id){assert(ioctl_task && !id);return information_tx_error;}
esp_err_t esp32_mquickjs_wifi_twt_information_timer_replaceable_native(int16_t id,unsigned flow){
    assert(ioctl_task && id==123 && flow==3);return information_replace_error;
}
int wifi_sta_itwt_suspend_process(void *message){
    assert(ioctl_task && !information_resume && ((uint8_t *)message)[8]==3 && ((uint8_t *)message)[9]==3);
    uint32_t ms;memcpy(&ms,(uint8_t *)message+12,4);assert(ms==120);return information_send_error;
}
int ieee80211_itwt_information(void *node,uint32_t flow,uint32_t size,uint32_t all,uint32_t ms){
    assert(ioctl_task && information_resume && node && flow==3 && size==3 && !all && !ms);
    ++information_resumes;return information_send_error;
}
esp_err_t esp32_mquickjs_wifi_twt_information_post(const void *data,size_t size){(void)data;(void)size;assert(0);return ESP_ERR_INVALID_STATE;}
'''
