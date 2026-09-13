
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
