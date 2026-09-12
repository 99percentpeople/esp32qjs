"""Deferred actual broadcast timer adapter with deterministic native scheduling.

Reuses the timer API boundary declarations; includes the production adapter.
Native capture/match is injected here and exercised separately in the SDK
fixture. No fixture import/compile/run until the Wi-Fi validation stage.
"""
import unittest
from test_wifi_twt_setup_timer import PRELUDE
from test_wifi_twt_broadcast_event import broadcast_types, BOUNDARIES
from test_wifi_driver_phy import COMPONENT
from test_wifi_rx_target import unit
from test_wireless_control_regression import compile_run


class WiFiTwtBroadcastTimer(unittest.TestCase):
    def test_numeric_identity_copy_close_and_error_suffixes(self):
        code = PRELUDE + broadcast_types() + BOUNDARIES
        code += "void esp32_mquickjs_wifi_twt_tx_broadcast_cancel_native(void);\nbool esp32_mquickjs_wifi_twt_sdk_broadcast_node_matches_native(uintptr_t);\n"
        code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_twt_broadcast_event.h')
        code += unit(COMPONENT / 'src/modules/wifi_twt/esp32_mquickjs_wifi_twt_broadcast_event.c')
        code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_twt_teardown_tx.h')
        code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_twt_tx.h')
        code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_twt_broadcast_timer.h')
        code += "bool esp32_mquickjs_wifi_twt_sdk_broadcast_established_native(unsigned);\nesp_err_t esp32_mquickjs_wifi_twt_tx_broadcast_cancel_request_native(unsigned, uint32_t);\nesp_err_t esp32_mquickjs_wifi_twt_tx_broadcast_quiescent_native(unsigned, uint32_t, uint32_t *);\n"
        code += "void esp32_mquickjs_wifi_twt_sdk_broadcast_tx_complete_native(uintptr_t, uint8_t, const uint8_t *, uint8_t);\n"
        code += unit(COMPONENT / 'src/modules/wifi_twt/esp32_mquickjs_wifi_twt_broadcast_timer.c')
        code += unit(COMPONENT / 'src/modules/wifi_twt/esp32_mquickjs_wifi_twt_broadcast_rx.c')
        compile_run(self, code + MAIN)


MAIN = r'''
uint8_t btwt_setup_timer[1248];
static struct timer_record {esp_timer_create_args_t args;bool live,active;} timers[128];
static esp32_mquickjs_wifi_btwt_timer_identity_t current[32];
static unsigned creates,stops,deletes,starts,posts,processes,closes;
static int create_error,stop_error,delete_error,start_error,post_error;
static bool association=true,immediate,omit_event,conflict_event;
static bool native_finished;
static unsigned observations,rx_calls,individual_rx_calls;
static esp_err_t observation_error,tx_cancel_error,tx_quiet_error;
#ifndef TEST_REAL_BTWT_TEARDOWN_RETIRE
static esp32_mquickjs_wifi_twt_teardown_tx_snapshot_t teardown_state;
static esp_err_t teardown_quiet_error;
void esp32_mquickjs_wifi_twt_teardown_tx_snapshot(esp32_mquickjs_wifi_twt_teardown_tx_snapshot_t *out){assert(!locked);*out=teardown_state;}
esp_err_t esp32_mquickjs_wifi_twt_teardown_tx_quiescent_native(uint32_t identity,uint32_t *out) {
    assert(!locked && identity==teardown_state.identity);if(teardown_quiet_error)return teardown_quiet_error;
    *out=teardown_state.revision;return 0;
}
bool esp32_mquickjs_wifi_twt_teardown_tx_release_native(uint32_t identity,uint32_t revision) {
    assert(!locked && identity==teardown_state.identity);
    if(teardown_quiet_error || revision!=teardown_state.revision)return false;
    memset(&teardown_state,0,sizeof(teardown_state));return true;
}
#endif
static uint32_t joined_mask,tx_revision=17;static unsigned tx_cancels;
static esp32_mquickjs_wifi_btwt_event_fence_t saved_fence;
static unsigned fence_posts;static esp_err_t fence_error;static bool early_fence;
static void *queued;static unsigned queued_phase;
static uint8_t received[17];
static ETSTimer *legacy(unsigned slot){assert(slot<32);return (ETSTimer *)(btwt_setup_timer+20*slot);}
static void reset(void) {
    heap_caps_free(s_btwt_timer.entries);memset(&s_btwt_timer,0,sizeof(s_btwt_timer));
    memset(btwt_setup_timer,0,sizeof(btwt_setup_timer));memset(timers,0,sizeof(timers));
#ifndef TEST_REAL_BTWT_TEARDOWN_RETIRE
    memset(&teardown_state,0,sizeof(teardown_state));teardown_quiet_error=0;
#endif
    creates=stops=deletes=starts=posts=processes=closes=allocations=0;
    create_error=stop_error=delete_error=start_error=post_error=0;
    association=true;allocation_error=immediate=omit_event=conflict_event=native_finished=false;queued=NULL;
    observations=rx_calls=individual_rx_calls=tx_cancels=0;observation_error=tx_cancel_error=tx_quiet_error=0;joined_mask=0;tx_revision=17;fence_posts=0;fence_error=0;early_fence=false;memset(&saved_fence,0,sizeof(saved_fence));assert(!s_btwt_event_scope);
    for(unsigned i=0;i<32;++i){current[i].node=0x123000;memset(current[i].parameter,i,17);current[i].parameter[10]=i<<3;}
}
bool esp32_mquickjs_wifi_twt_sdk_broadcast_node_matches_native(uintptr_t node){return association && node==0x123000;}
int __real_he_recv_action_twt_setup(void *node,const void *header,const uint8_t *body,const uint8_t *end) {
    (void)node;(void)header;assert(!locked && end-body>=20 && body[4]==15);++individual_rx_calls;return 92;
}
int ieee80211_process_btwt_setup_action(void *node,const uint8_t *body) {
    assert(!locked && node==(void *)0x123000);++rx_calls;
    for(unsigned i=15;i<20;++i)assert(!body[i]);
    unsigned slot=body[13]>>3;assert(!legacy(slot)->timer_arg);
    if(((body[6]>>1)&7U)!=TWT_ACCEPT) {
        wifi_event_sta_btwt_setup_t event={.status=BTWT_SETUP_INTERNAL_ERR,.setup_cmd=TWT_REJECT,.btwt_id=slot};
        assert(!esp32_mquickjs_wifi_btwt_event_post(WIFI_EVENT_BTWT_SETUP,&event,sizeof(event)));
        native_finished=true;return -1;
    }
    memcpy(current[slot].parameter,body+3,17);
    assert(esp32_mquickjs_wifi_btwt_timer_setfn(legacy(slot),(void *)btwt_setup_dwell_timeout_fn,btwt_setup_timer+704+17*slot));
    assert(esp32_mquickjs_wifi_btwt_timer_arm(legacy(slot),12345,false));return 0;
}
bool esp32_mquickjs_wifi_twt_sdk_broadcast_timer_capture_native(unsigned slot,uint8_t phase,const void *arg,
    esp32_mquickjs_wifi_btwt_timer_identity_t *out) {
    assert(!locked);if(!association || slot>=32 || (phase!=30 && phase!=31) || arg!=btwt_setup_timer+704+17*slot)return false;
    *out=current[slot];return true;
}
bool esp32_mquickjs_wifi_twt_sdk_broadcast_timer_matches_native(unsigned slot,uint8_t phase,
    const esp32_mquickjs_wifi_btwt_timer_identity_t *id) {
    assert(!locked && slot<32 && (phase==30 || phase==31));
    return association && id->node==current[slot].node && !memcmp(id->parameter,current[slot].parameter,17);
}
void btwt_setup_timeout_fn(void *arg){(void)arg;assert(0);}
void btwt_setup_dwell_timeout_fn(void *arg){(void)arg;assert(0);}
void __real_btwt_setup_timeout_fn_process(void *arg) {
    assert(!locked && ((uintptr_t)arg<(uintptr_t)btwt_setup_timer || (uintptr_t)arg>=(uintptr_t)btwt_setup_timer+sizeof(btwt_setup_timer)));
    ++processes;memcpy(received,arg,17);
    native_finished=false;
    unsigned slot=received[10]>>3;
    if(!omit_event){
        wifi_event_sta_btwt_setup_t event={.status=BTWT_SETUP_TIMEOUT,.btwt_id=slot};
        assert(!esp32_mquickjs_wifi_btwt_event_post(WIFI_EVENT_BTWT_SETUP,&event,sizeof(event)));
        assert(!observations); /* SDK continues mutating after posting. */
        if(conflict_event){event.status=BTWT_SETUP_TXFAIL;
            assert(esp32_mquickjs_wifi_btwt_event_post(WIFI_EVENT_BTWT_SETUP,&event,sizeof(event))==ESP_ERR_INVALID_STATE);}
    }
    native_finished=true;
}
void __real_btwt_setup_dwell_timeout_fn_process(void *arg){__real_btwt_setup_timeout_fn_process(arg);}
void esp32_mquickjs_wifi_twt_tx_broadcast_cancel_native(void) {assert(!locked);}
void esp32_mquickjs_wifi_twt_tx_snapshot(esp32_mquickjs_wifi_twt_tx_snapshot_t *out){assert(!locked);*out=(esp32_mquickjs_wifi_twt_tx_snapshot_t){.revision=tx_revision};}
bool esp32_mquickjs_wifi_twt_sdk_broadcast_established_native(unsigned slot){assert(!locked && slot<32);return (joined_mask&(1U<<slot))!=0;}
esp_err_t esp32_mquickjs_wifi_twt_tx_broadcast_cancel_request_native(unsigned slot,uint32_t identity) {
    assert(!locked && slot<32 && identity);++tx_cancels;return tx_cancel_error;
}
esp_err_t esp32_mquickjs_wifi_twt_tx_broadcast_quiescent_native(unsigned slot,uint32_t identity,uint32_t *revision) {
    assert(!locked && slot<32 && identity && revision);if(tx_quiet_error)return tx_quiet_error;*revision=tx_revision;return 0;
}
/* Other production ledgers are exercised by their dedicated fixtures. */
void esp32_mquickjs_wifi_twt_setup_results_connection_closed_native(void) {assert(!locked);}
void esp32_mquickjs_wifi_twt_setup_timers_connection_closed_native(void) {assert(!locked);}
void esp32_mquickjs_wifi_twt_information_timers_connection_closed_native(void) {assert(!locked);}
void __real_ieee80211_close_all_twt_sessions(void) {
    assert(!locked);++closes;
    if(s_btwt_timer.entries)for(unsigned i=0;i<32;++i)assert(!s_btwt_timer.entries[i].active);
}
static esp_err_t esp_event_post(const char *base,int32_t id,const void *data,size_t size,unsigned wait) {
    if(base==ESP32QJS_WIFI_RADIO_CONTROL_EVENT) {
        assert(!locked && id==ESP32_MQUICKJS_WIFI_BTWT_FENCE_EVENT && size==sizeof(saved_fence) && !wait);
        saved_fence=*(const esp32_mquickjs_wifi_btwt_event_fence_t *)data;++fence_posts;
        if(early_fence)esp32_mquickjs_wifi_btwt_setup_observe_fence(&saved_fence);
        assert(esp32_mquickjs_wifi_btwt_setup_release_native(saved_fence.slot,saved_fence.identity,&saved_fence.cut,saved_fence.sequence)==ESP_ERR_NOT_FINISHED);
        return fence_error;
    }
    assert(!locked && native_finished && base==WIFI_EVENT && id==WIFI_EVENT_BTWT_SETUP &&
        size==sizeof(wifi_event_sta_btwt_setup_t) && wait==0);
    const wifi_event_sta_btwt_setup_t *event=data;
    esp32_mquickjs_wifi_btwt_timer_result_t result;
    unsigned slot=event->btwt_id;uint32_t identity=s_btwt_timer.entries[slot].identity;
    bool found=identity ? esp32_mquickjs_wifi_btwt_timer_result(slot,identity,&result) :
        esp32_mquickjs_wifi_btwt_setup_result(slot,s_btwt_timer.entries[slot].result.identity,&result);
    assert(found && result.complete && result.seen && result.publishing);
    assert(!s_btwt_timer.entries[slot].busy);
    if(identity)assert(esp32_mquickjs_wifi_btwt_timer_cancel_native(slot,identity)==ESP_ERR_NOT_FINISHED);

    ++observations;return observation_error;
}
esp_err_t esp_timer_create(const esp_timer_create_args_t *args,esp_timer_handle_t *out) {
    assert(!locked && creates<128 && args->dispatch_method==ESP_TIMER_TASK);++creates;
    if(create_error)return create_error;
    struct timer_record *t=&timers[creates-1];t->args=*args;t->live=true;*out=t;return ESP_OK;
}
esp_err_t esp_timer_stop(esp_timer_handle_t h) {
    assert(!locked);struct timer_record *t=h;assert(t->live);++stops;
    if(stop_error)return stop_error;if(!t->active)return ESP_ERR_INVALID_STATE;t->active=false;return ESP_OK;
}
esp_err_t esp_timer_delete(esp_timer_handle_t h) {
    assert(!locked);struct timer_record *t=h;assert(t->live && !t->active);++deletes;
    if(delete_error)return delete_error;t->live=false;return ESP_OK;
}
esp_err_t esp_timer_start_once(esp_timer_handle_t h,uint64_t us) {
    assert(!locked && us==12345);struct timer_record *t=h;assert(t->live);++starts;
    if(start_error)return start_error;t->active=true;return ESP_OK;
}
static void deliver(void) {
    assert(queued);void *arg=queued;unsigned phase=queued_phase;queued=NULL;
    if(phase==30)__wrap_btwt_setup_timeout_fn_process(arg);else __wrap_btwt_setup_dwell_timeout_fn_process(arg);
}
int ieee80211_timer_process(int signal,int operation,void *arg) {
    assert(!locked && signal==7 && (operation==30 || operation==31) && arg);++posts;
    queued=arg;queued_phase=operation;if(immediate)deliver();
    return post_error;
}
static uint32_t install(unsigned slot,unsigned phase) {
    assert(esp32_mquickjs_wifi_btwt_timer_setfn(legacy(slot),
        phase==30?(void *)btwt_setup_timeout_fn:(void *)btwt_setup_dwell_timeout_fn,btwt_setup_timer+704+17*slot));
    return s_btwt_timer.entries?s_btwt_timer.entries[slot].identity:0;
}
static void arm(unsigned slot){assert(esp32_mquickjs_wifi_btwt_timer_arm(legacy(slot),12345,false));}
static void fire(struct timer_record *t){t->active=false;t->args.callback(t->args.arg);}
void esp32_mquickjs_wifi_twt_sdk_broadcast_tx_complete_native(uintptr_t node,uint8_t dialog,const uint8_t *parameter,uint8_t status) {
    unsigned slot=parameter[10]>>3;assert(!locked && node==current[slot].node);
    native_finished=false;
    esp32_mquickjs_wifi_btwt_timer_result_t result;
    uint32_t request=s_btwt_timer.entries[slot].result.identity;
    assert(esp32_mquickjs_wifi_btwt_setup_result(slot,request,&result) && result.tx_busy && !result.complete);
    if(status==1) {
        install(slot,30);arm(slot);
        assert(esp32_mquickjs_wifi_btwt_setup_result(slot,request,&result) && result.tx_busy && !result.complete);
        esp32_mquickjs_wifi_btwt_timer_bind_response_native(node,dialog,parameter);
    } else if(!omit_event) {
        wifi_event_sta_btwt_setup_t event={.status=BTWT_SETUP_TXFAIL,.btwt_id=slot,.reason=status};
        assert(!esp32_mquickjs_wifi_btwt_event_post(WIFI_EVENT_BTWT_SETUP,&event,sizeof(event)));
        assert(!observations);
    }
    native_finished=true;
}
int main(void) {
    reset();uint8_t owner_param[17]={0};owner_param[10]=3<<3;
    assert(!esp32_mquickjs_wifi_btwt_setup_begin_native(3,501,0x123000,owner_param));
    assert(!esp32_mquickjs_wifi_btwt_setup_hold_native(3,501));
    uintptr_t owner_node=111;
    assert(!esp32_mquickjs_wifi_btwt_setup_owner_native(3,501,&owner_node) && owner_node==111);
    s_btwt_timer.entries[3].result.complete=true;s_btwt_timer.entries[3].result.seen=true;
    s_btwt_timer.entries[3].result.event.status=BTWT_SETUP_SUCCESS;joined_mask=1U<<3;
    assert(esp32_mquickjs_wifi_btwt_setup_owner_native(3,501,&owner_node) && owner_node==0x123000);
    assert(!esp32_mquickjs_wifi_btwt_setup_owner_native(3,500,&owner_node));
    teardown_state=(esp32_mquickjs_wifi_twt_teardown_tx_snapshot_t){.broadcast=true,.identity=501,.flow=3};
    teardown_quiet_error=ESP_ERR_INVALID_STATE;joined_mask=0;
    assert(esp32_mquickjs_wifi_btwt_setup_cancel_native(3,501)==ESP_ERR_INVALID_STATE);
    memset(&teardown_state,0,sizeof(teardown_state));teardown_quiet_error=0;joined_mask=1U<<3;
    __wrap_ieee80211_close_all_twt_sessions();joined_mask=1U<<3; /* node address reused */
    assert(!esp32_mquickjs_wifi_btwt_setup_owner_native(3,501,&owner_node));

    reset();ETSTimer foreign={0};assert(!esp32_mquickjs_wifi_btwt_timer_setfn(&foreign,NULL,NULL));
    assert(!esp32_mquickjs_wifi_btwt_timer_disarm(btwt_setup_timer+1));
    uint32_t first=install(31,30);assert(first && allocations==1);arm(31);
    assert(esp32_mquickjs_wifi_btwt_timer_available_native(31)==ESP_ERR_INVALID_STATE);
    struct timer_record *old=legacy(31)->timer_arg;fire(old);assert(posts==1 && !processes);
    assert(!esp32_mquickjs_wifi_btwt_timer_cancel_native(31,first));
    uint32_t second=install(31,31);assert(second>first);arm(31);deliver();assert(!processes);
    old->args.callback(old->args.arg);assert(posts==1);
    fire(legacy(31)->timer_arg);deliver();assert(processes==1 && !memcmp(received,current[31].parameter,17));
    assert(!legacy(31)->timer_arg && !esp32_mquickjs_wifi_btwt_timer_error());
    assert(!esp32_mquickjs_wifi_btwt_timer_available_native(31));
    assert(esp32_mquickjs_wifi_btwt_timer_cancel_native(31,first)==ESP_ERR_INVALID_STATE);
    reset();first=install(1,30);arm(1);old=legacy(1)->timer_arg;fire(old);
    __wrap_ieee80211_close_all_twt_sessions();assert(closes==1 && !legacy(1)->timer_arg);
    install(1,30);arm(1);deliver();assert(!processes);
    __wrap_ieee80211_close_all_twt_sessions();
    reset();install(2,31);arm(2);fire(legacy(2)->timer_arg);current[2].parameter[3]^=1;deliver();
    assert(!processes && esp32_mquickjs_wifi_btwt_timer_error()==ESP_ERR_INVALID_STATE && !legacy(2)->timer_arg);
    reset();first=install(4,30);arm(4);stop_error=81;
    assert(esp32_mquickjs_wifi_btwt_timer_cancel_native(4,first)==81 && !deletes && legacy(4)->timer_arg);
    stop_error=0;delete_error=82;
    assert(esp32_mquickjs_wifi_btwt_timer_cancel_native(4,first)==82 && deletes==1);
    unsigned stop_count=stops;delete_error=0;
    assert(!esp32_mquickjs_wifi_btwt_timer_cancel_native(4,first) && stops==stop_count && deletes==2);
    assert(esp32_mquickjs_wifi_btwt_timer_error()==81); /* First fault remains diagnostic. */
    reset();allocation_error=true;assert(!install(1,30) && !creates && !native_live);
    assert(esp32_mquickjs_wifi_btwt_timer_error()==ESP_ERR_NO_MEM);
    reset();create_error=83;install(1,30);assert(!legacy(1)->timer_arg && esp32_mquickjs_wifi_btwt_timer_error()==83);
    reset();install(1,30);start_error=84;arm(1);assert(esp32_mquickjs_wifi_btwt_timer_error()==84);
    __wrap_ieee80211_close_all_twt_sessions();assert(!legacy(1)->timer_arg);
    reset();install(1,30);arm(1);post_error=85;fire(legacy(1)->timer_arg);deliver();
    assert(!processes && esp32_mquickjs_wifi_btwt_timer_error()==85);__wrap_ieee80211_close_all_twt_sessions();
    reset();install(1,30);arm(1);immediate=true;post_error=86;fire(legacy(1)->timer_arg);
    assert(processes==1 && !esp32_mquickjs_wifi_btwt_timer_error()); /* Already consumed before late post error. */
    reset();first=install(3,31);arm(3);observation_error=87;fire(legacy(3)->timer_arg);deliver();
    esp32_mquickjs_wifi_btwt_timer_result_t result,unchanged;
    assert(esp32_mquickjs_wifi_btwt_timer_result(3,first,&result));
    assert(result.complete && result.seen && !result.publishing && !result.native_error && result.observation_error==87);
    assert(!legacy(3)->timer_arg && !esp32_mquickjs_wifi_btwt_timer_error() && observations==1);
    unchanged=result;assert(!esp32_mquickjs_wifi_btwt_timer_result(3,first+1,&result) && !memcmp(&result,&unchanged,sizeof(result)));
    second=install(3,30);assert(second>first && !esp32_mquickjs_wifi_btwt_timer_result(3,first,&result));
    assert(esp32_mquickjs_wifi_btwt_timer_result(3,second,&result) && !result.complete);
    __wrap_ieee80211_close_all_twt_sessions();
    reset();first=install(3,30);arm(3);omit_event=true;fire(legacy(3)->timer_arg);deliver();
    assert(esp32_mquickjs_wifi_btwt_timer_result(3,first,&result) && result.complete && !result.seen && result.native_error==ESP_ERR_INVALID_STATE);
    assert(!observations && !legacy(3)->timer_arg);
    reset();first=install(3,30);arm(3);conflict_event=true;fire(legacy(3)->timer_arg);deliver();
    assert(esp32_mquickjs_wifi_btwt_timer_result(3,first,&result) && result.ambiguous && result.native_error==ESP_ERR_INVALID_STATE);
    reset();first=install(3,31);arm(3);delete_error=88;observation_error=89;fire(legacy(3)->timer_arg);deliver();
    assert(esp32_mquickjs_wifi_btwt_timer_result(3,first,&result) && result.native_error==88 && result.observation_error==89);
    assert(legacy(3)->timer_arg);stop_count=stops;delete_error=0;
    assert(!esp32_mquickjs_wifi_btwt_timer_cancel_native(3,first) && stops==stop_count && !legacy(3)->timer_arg);
    reset();for(unsigned i=0;i<32;++i){install(i,30);arm(i);}
    esp32_mquickjs_wifi_btwt_timer_snapshot_t snapshot;esp32_mquickjs_wifi_btwt_timer_snapshot(&snapshot);
    assert(snapshot.active_mask==UINT32_MAX && allocations==1 && snapshot.reserved_bytes==32*sizeof(btwt_timer_entry_t));
    __wrap_ieee80211_close_all_twt_sessions();assert(deletes==32);
    reset();s_btwt_timer.snapshot.last_identity=UINT32_MAX;install(1,30);
    assert(!creates && esp32_mquickjs_wifi_btwt_timer_error()==ESP_ERR_NO_MEM);
    reset();uint8_t response[25]={22,6,9,216,10,12,TWT_ACCEPT<<1};response[13]=3<<3;response[10]=1;response[11]=1;
    for(unsigned length=0;length<15;++length)
        assert(__wrap_he_recv_action_twt_setup((void *)0x123000,NULL,response,response+length)==-1);
    assert(!rx_calls && !creates && !stops);
    first=install(3,30);arm(3);
    esp32_mquickjs_wifi_btwt_timer_bind_response_native(0x123000,9,current[3].parameter);
    assert(s_btwt_timer.entries[3].dialog==9);
    response[2]=10;assert(__wrap_he_recv_action_twt_setup((void *)0x123000,NULL,response,response+15)==-1);
    response[2]=9;assert(__wrap_he_recv_action_twt_setup((void *)0x123001,NULL,response,response+15)==-1);
    assert(!rx_calls && legacy(3)->timer_arg);
    memset(response+15,0xa5,10);
    assert(!__wrap_he_recv_action_twt_setup((void *)0x123000,NULL,response,response+25));
    second=s_btwt_timer.entries[3].identity;assert(second>first && rx_calls==1 && s_btwt_timer.entries[3].phase==31);
    assert(__wrap_he_recv_action_twt_setup((void *)0x123000,NULL,response,response+15)==-1 && rx_calls==1);
    __wrap_btwt_setup_timeout_fn_process((void *)(uintptr_t)first);assert(!processes);
    __wrap_ieee80211_close_all_twt_sessions();
    reset();first=install(3,30);arm(3);
    esp32_mquickjs_wifi_btwt_timer_bind_response_native(0x123000,9,current[3].parameter);
    response[6]=TWT_REJECT<<1;observation_error=91;
    assert(__wrap_he_recv_action_twt_setup((void *)0x123000,NULL,response,response+15)==-1);
    assert(rx_calls==1 && !legacy(3)->timer_arg && observations==1);
    assert(esp32_mquickjs_wifi_btwt_timer_result(3,first,&result) && result.complete && result.seen &&
        !result.native_error && result.observation_error==91 && result.event.status==BTWT_SETUP_INTERNAL_ERR);
    reset();first=install(3,30);arm(3);
    esp32_mquickjs_wifi_btwt_timer_bind_response_native(0x123000,9,current[3].parameter);
    stop_error=93;response[6]=TWT_ACCEPT<<1;
    assert(__wrap_he_recv_action_twt_setup((void *)0x123000,NULL,response,response+15)==-1 && !rx_calls && !deletes);
    assert(esp32_mquickjs_wifi_btwt_timer_result(3,first,&result) && result.complete && result.native_error==93);
    stop_error=0;assert(!esp32_mquickjs_wifi_btwt_timer_cancel_native(3,first));
    reset();response[4]=15;response[5]=0;
    assert(__wrap_he_recv_action_twt_setup((void *)17,NULL,response,response+19)==-1);
    assert(__wrap_he_recv_action_twt_setup((void *)17,NULL,response,response+20)==92 && individual_rx_calls==1);
    /* Stable request exists before TX and survives response -> dwell. */
    reset();assert(!esp32_mquickjs_wifi_btwt_setup_begin_native(3,100,current[3].node,current[3].parameter));
    assert(allocations==1 && !creates);
    assert(esp32_mquickjs_wifi_btwt_setup_result(3,100,&result) && !result.complete && !result.timer_identity);
    esp32_mquickjs_wifi_btwt_setup_tx_complete_native(3,100,current[3].node,9,current[3].parameter,1);
    assert(!observations && esp32_mquickjs_wifi_btwt_setup_result(3,100,&result) && !result.complete);
    first=result.timer_identity;assert(first && first!=100);
    response[4]=10;response[5]=12;response[6]=TWT_ACCEPT<<1;
    assert(!__wrap_he_recv_action_twt_setup((void *)0x123000,NULL,response,response+15));
    assert(esp32_mquickjs_wifi_btwt_setup_result(3,100,&result) && result.identity==100 && result.timer_identity>first && !result.complete);
    old=legacy(3)->timer_arg;fire(old);deliver();
    assert(esp32_mquickjs_wifi_btwt_setup_result(3,100,&result) && result.complete && result.seen && result.identity==100);
    assert(!esp32_mquickjs_wifi_btwt_setup_begin_native(3,101,current[3].node,current[3].parameter));
    result.identity=987;assert(!esp32_mquickjs_wifi_btwt_setup_result(3,100,&result) && result.identity==987);
    __wrap_btwt_setup_timeout_fn_process((void *)(uintptr_t)first);
    assert(esp32_mquickjs_wifi_btwt_setup_result(3,101,&result) && !result.complete);
    __wrap_ieee80211_close_all_twt_sessions();
    assert(esp32_mquickjs_wifi_btwt_setup_result(3,101,&result) && result.complete && result.native_error==ESP_ERR_INVALID_STATE);
    /* TXFAIL is retained before a saturated observation queue. */
    reset();assert(!esp32_mquickjs_wifi_btwt_setup_begin_native(3,200,current[3].node,current[3].parameter));
    observation_error=97;
    esp32_mquickjs_wifi_btwt_setup_tx_complete_native(3,200,current[3].node,9,current[3].parameter,77);
    assert(esp32_mquickjs_wifi_btwt_setup_result(3,200,&result) && result.complete && result.seen &&
        !result.native_error && result.event.reason==77 && result.observation_error==97 && !result.publishing && !creates);
    esp32_mquickjs_wifi_btwt_setup_submitted_native(3,200,98);
    assert(esp32_mquickjs_wifi_btwt_setup_result(3,200,&result) && result.submit_error==98 && result.event.reason==77);
    esp32_mquickjs_wifi_btwt_setup_tx_complete_native(3,200,current[3].node,9,current[3].parameter,77);
    assert(observations==1);
    /* Output error does not delete the callback's already-created timer. */
    reset();assert(!esp32_mquickjs_wifi_btwt_setup_begin_native(3,300,current[3].node,current[3].parameter));
    esp32_mquickjs_wifi_btwt_setup_tx_complete_native(3,300,current[3].node,9,current[3].parameter,1);
    esp32_mquickjs_wifi_btwt_setup_submitted_native(3,300,99);
    assert(esp32_mquickjs_wifi_btwt_setup_result(3,300,&result) && result.complete && result.submit_error==99 && legacy(3)->timer_arg);
    __wrap_ieee80211_close_all_twt_sessions();assert(!legacy(3)->timer_arg);
    reset();assert(!esp32_mquickjs_wifi_btwt_setup_begin_native(3,350,current[3].node,current[3].parameter));
    omit_event=true;
    esp32_mquickjs_wifi_btwt_setup_tx_complete_native(3,350,current[3].node,9,current[3].parameter,77);
    assert(esp32_mquickjs_wifi_btwt_setup_result(3,350,&result) && result.complete && !result.seen && result.native_error==ESP_ERR_INVALID_STATE);
    reset();allocation_error=true;
    assert(esp32_mquickjs_wifi_btwt_setup_begin_native(3,400,current[3].node,current[3].parameter)==ESP_ERR_NO_MEM && !creates);
    reset();assert(!esp32_mquickjs_wifi_btwt_setup_begin_native(3,400,current[3].node,current[3].parameter));
    create_error=83;
    esp32_mquickjs_wifi_btwt_setup_tx_complete_native(3,400,current[3].node,9,current[3].parameter,1);
    assert(esp32_mquickjs_wifi_btwt_setup_result(3,400,&result) && result.complete && result.native_error==83 && !result.seen);
    reset();assert(!esp32_mquickjs_wifi_btwt_setup_begin_native(3,500,current[3].node,current[3].parameter));
    esp32_mquickjs_wifi_btwt_setup_tx_complete_native(3,500,current[3].node,9,current[3].parameter,1);
    post_error=85;fire(legacy(3)->timer_arg);
    assert(esp32_mquickjs_wifi_btwt_setup_result(3,500,&result) && result.complete && result.native_error==85 && !result.seen);
    __wrap_ieee80211_close_all_twt_sessions();
    reset();assert(!esp32_mquickjs_wifi_btwt_setup_begin_native(3,900,current[3].node,current[3].parameter));
    assert(!esp32_mquickjs_wifi_btwt_setup_hold_native(3,900));
    esp32_mquickjs_wifi_btwt_setup_tx_complete_native(3,900,current[3].node,9,current[3].parameter,1);
    first=s_btwt_timer.entries[3].identity;old=legacy(3)->timer_arg;
    assert(!esp32_mquickjs_wifi_btwt_setup_begin_native(4,901,current[4].node,current[4].parameter));
    assert(!esp32_mquickjs_wifi_btwt_setup_hold_native(4,901));
    assert(esp32_mquickjs_wifi_btwt_setup_cancel_native(3,899)==ESP_ERR_INVALID_STATE && !tx_cancels && !deletes);
    joined_mask=1U<<3;
    assert(esp32_mquickjs_wifi_btwt_setup_cancel_native(3,900)==ESP_ERR_INVALID_STATE && !tx_cancels && !deletes);
    joined_mask=0;s_btwt_timer.entries[3].result.tx_busy=true;
    assert(esp32_mquickjs_wifi_btwt_setup_cancel_native(3,900)==ESP_ERR_NOT_FINISHED && !tx_cancels);
    s_btwt_timer.entries[3].result.tx_busy=false;stop_error=81;
    assert(esp32_mquickjs_wifi_btwt_setup_cancel_native(3,900)==81 && !deletes && legacy(3)->timer_arg);
    assert(esp32_mquickjs_wifi_btwt_setup_result(3,900,&result) && result.held && result.cancel_requested && !result.cancelled);
    old->args.callback(old->args.arg);assert(!posts); /* Revoked before failed stop. */
    stop_error=0;delete_error=82;
    assert(esp32_mquickjs_wifi_btwt_setup_cancel_native(3,900)==82 && legacy(3)->timer_arg);
    stop_count=stops;delete_error=0;
    assert(!esp32_mquickjs_wifi_btwt_setup_cancel_native(3,900) && !legacy(3)->timer_arg && stops==stop_count);
    assert(esp32_mquickjs_wifi_btwt_setup_result(3,900,&result) && result.cancelled && result.held);
    unsigned cancellations=tx_cancels;
    assert(!esp32_mquickjs_wifi_btwt_setup_cancel_native(3,900) && tx_cancels==cancellations);
    assert(esp32_mquickjs_wifi_btwt_setup_result(4,901,&result) && !result.cancel_requested && result.held);
    esp32_mquickjs_wifi_btwt_cut_t cut={123,456};tx_quiet_error=ESP_ERR_NOT_FINISHED;
    assert(esp32_mquickjs_wifi_btwt_setup_quiescent_native(3,900,&cut)==ESP_ERR_NOT_FINISHED && cut.tx_revision==123 && cut.timer_revision==456);
    tx_quiet_error=0;assert(!esp32_mquickjs_wifi_btwt_setup_quiescent_native(3,900,&cut) && cut.tx_revision==17 && cut.timer_revision);
    __wrap_btwt_setup_timeout_fn_process((void *)(uintptr_t)first);assert(!processes);
    assert(esp32_mquickjs_wifi_btwt_setup_held(3));
    reset();assert(!esp32_mquickjs_wifi_btwt_setup_begin_native(3,1000,current[3].node,current[3].parameter));
    assert(!esp32_mquickjs_wifi_btwt_setup_hold_native(3,1000));
    assert(!esp32_mquickjs_wifi_btwt_setup_cancel_native(3,1000));
    assert(!esp32_mquickjs_wifi_btwt_setup_quiescent_native(3,1000,&cut));
    uint32_t sequence=777;fence_error=91;early_fence=true;
    assert(esp32_mquickjs_wifi_btwt_setup_post_fence(3,1000,&cut,&sequence)==91 && sequence==777);
    assert(esp32_mquickjs_wifi_btwt_setup_result(3,1000,&result) && result.held && !result.fence_seen && !result.fence_pending);
    fence_error=0;early_fence=false;
    assert(!esp32_mquickjs_wifi_btwt_setup_post_fence(3,1000,&cut,&sequence));
    esp32_mquickjs_wifi_btwt_event_fence_t old_fence=saved_fence;
    assert(esp32_mquickjs_wifi_btwt_setup_release_native(3,1000,&cut,sequence)==ESP_ERR_NOT_FINISHED);
    assert(!esp32_mquickjs_wifi_btwt_setup_post_fence(3,1000,&cut,&sequence) && sequence>old_fence.sequence);
    esp32_mquickjs_wifi_btwt_setup_observe_fence(&old_fence);
    assert(esp32_mquickjs_wifi_btwt_setup_release_native(3,1000,&cut,sequence)==ESP_ERR_NOT_FINISHED);
    esp32_mquickjs_wifi_btwt_setup_observe_fence(&saved_fence);++tx_revision;
    assert(esp32_mquickjs_wifi_btwt_setup_release_native(3,1000,&cut,sequence)==ESP_ERR_NOT_FINISHED);
    assert(!esp32_mquickjs_wifi_btwt_setup_quiescent_native(3,1000,&cut));early_fence=true;
    assert(!esp32_mquickjs_wifi_btwt_setup_post_fence(3,1000,&cut,&sequence));
    assert(!esp32_mquickjs_wifi_btwt_setup_release_native(3,1000,&cut,sequence) && !esp32_mquickjs_wifi_btwt_setup_held(3));
    assert(esp32_mquickjs_wifi_btwt_setup_release_native(3,1000,&cut,sequence)==ESP_ERR_INVALID_STATE);
    assert(!esp32_mquickjs_wifi_btwt_setup_begin_native(3,1001,current[3].node,current[3].parameter));
    assert(!esp32_mquickjs_wifi_btwt_setup_hold_native(3,1001));
    esp32_mquickjs_wifi_btwt_setup_observe_fence(&saved_fence);
    assert(esp32_mquickjs_wifi_btwt_setup_result(3,1001,&result) && !result.fence_seen && result.held);
    assert(!esp32_mquickjs_wifi_btwt_setup_cancel_native(3,1001));
    assert(!esp32_mquickjs_wifi_btwt_setup_quiescent_native(3,1001,&cut));
    s_btwt_timer.entries[3].result.fence_sequence=UINT32_MAX;
    assert(esp32_mquickjs_wifi_btwt_setup_post_fence(3,1001,&cut,&sequence)==ESP_ERR_NO_MEM);
    reset();assert(!native_live && !locked);return 0;
}
'''
