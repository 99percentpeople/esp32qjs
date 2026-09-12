"""Deferred production information producer rollback, using the real TX ledger.

No fixture import, compilation or execution before Wi-Fi staged validation.
Production producer/TX/callback/recycler code covers PM rollback and identity
retention. This is not SDK execution or evidence of native queue/RF ordering.
"""
import unittest
from test_wifi_driver_phy import COMPONENT
from test_wifi_rx_target import unit
from test_wifi_twt_tx import PRELUDE, MAIN as TX_MAIN
from test_wireless_control_regression import compile_run


class WiFiTwtInformationSubmit(unittest.TestCase):
    def test_no_output_raw_error_reentrancy_and_early_completion(self):
        code = '#define TEST_REAL_INFORMATION_SUBMIT 1\n' + PRELUDE
        for name in ('probe_result', 'tx'):
            code += unit(COMPONENT / f'internal/esp32_mquickjs_wifi_twt_{name}.h')
        code += unit(COMPONENT / 'src/modules/wifi_twt/esp32_mquickjs_wifi_twt_probe_result.c')
        tx = unit(COMPONENT / 'src/modules/wifi_twt/esp32_mquickjs_wifi_twt_tx.c')
        code += tx.replace('((const uint8_t *)buffer)[61]', '((const uint8_t *)buffer)[64]')
        compile_run(self, code + TX_MAIN.split('int main(void) {')[0] + MAIN)


MAIN = r'''
static unsigned wake_ups,wake_dones,information_calls;
static bool acquire=true,emit,nest,foreign,early_complete;
static bool same_request=true,information_recycle;
static bool malformed,callback_reuse,managed_operation;
static int expected_output;
static unsigned information_callbacks;
static buffer_t information_buffer;
static int submit(void);
void pm_twt_wake_up(void) {assert(!locked);++wake_ups;}
void pm_twt_wake_done(void) {assert(!locked && wake_dones<wake_ups);++wake_dones;}
bool esp32_mquickjs_wifi_twt_sdk_information_capture_native(unsigned slot,const void *arg,
    esp32_mquickjs_wifi_twt_information_identity_t *out) {
    assert(!locked && slot==3);memset(out,0,sizeof(*out));out->node=17;out->control=*(const uint8_t *)arg;
    out->flows=8;out->request_ids[3]=123;return true;
}
bool esp32_mquickjs_wifi_twt_sdk_information_matches_native(unsigned slot,
    const esp32_mquickjs_wifi_twt_information_identity_t *id) {
    assert(!locked && slot==3 && id->node==17 && id->request_ids[3]==123);return same_request;
}
void __real_he_twt_information_txcb(void *buffer) {
    assert(!locked);++information_callbacks;pm_twt_wake_done();
    if(managed_operation) {
        wifi_event_sta_itwt_suspend_t event={.status=0,.flow_id_bitmap=8};
        assert(!__wrap_wifi_event_post(31,&event,sizeof(event)));
        esp32_mquickjs_wifi_twt_information_result_t result;
        assert(esp32_mquickjs_wifi_twt_information_snapshot(&result) && !result.complete);
    }
    if(information_recycle) {
        information_recycle=false;__wrap_esf_buf_recycle(buffer);memset(buffer,0xa5,72);
    }
    if(callback_reuse) {
        callback_reuse=false;__wrap_esf_buf_recycle(buffer);
        assert(submit()==-71); /* Reallocate while the old callback still owns its slot. */
        assert(read_snapshot().tracked==2);
    }
}
int __real_ieee80211_itwt_information(void *node,uint32_t flow,uint32_t size,uint32_t all,uint32_t timeout) {
    assert(!locked && node==(void *)17 && flow==3 && size==3 && all==(managed_operation?0U:1U) && timeout==120);++information_calls;
    if(acquire)esp32_mquickjs_wifi_twt_information_wake_up_native();
    if(nest) {
        nest=false;assert(__wrap_ieee80211_itwt_information(node,flow,size,all,timeout)==-71);
        assert(s_twt_tx.information_submit && s_twt_tx.information_submit->task==current_task);
    }
    if(foreign) {
        foreign=false;TaskHandle_t saved=current_task;current_task=(void *)99;
        assert(__wrap_ieee80211_itwt_information(node,flow,size,all,timeout)==ESP_ERR_INVALID_STATE);
        current_task=saved;
    }
    if(emit) {
        prepare_information(&information_buffer,true,managed_operation?0x63:0xe3);
        if(malformed){uint32_t length=3;memcpy(information_buffer.eb+24,&length,4);}
        assert(__wrap_ht_action_output(node,information_buffer.eb)==expected_output);
        if(early_complete)__wrap_he_twt_information_txcb(information_buffer.eb);
    } else {
        wifi_event_sta_itwt_suspend_t event={.status=ESP_ERR_NO_MEM};
        assert(__wrap_wifi_event_post(31,&event,sizeof(event))==information_post_error);
    }
    return -71;
}
static int submit(void) {return __wrap_ieee80211_itwt_information((void *)17,3,3,managed_operation?0U:1U,120);}
static void complete_inside_output(void *buffer) {
    output_hook=NULL;assert(read_snapshot().output_calls==1);
    __wrap_he_twt_information_txcb(buffer);
    assert(read_snapshot().tracked==1); /* Output still pins the recycled slot. */
}
int main(void) {
    information_post_error=0x107;
    assert(submit()==-71 && wake_ups==1 && wake_dones==1 && !s_twt_tx.information_submit);
    assert(information_posts==1);information_post_error=0;
    acquire=false;assert(submit()==-71 && wake_ups==1 && wake_dones==1);acquire=true;
    nest=true;assert(submit()==-71 && wake_ups==3 && wake_dones==3 && !s_twt_tx.information_submit);
    foreign=true;unsigned calls=information_calls;
    assert(submit()==-71 && information_calls==calls+1 && wake_ups==wake_dones);
    /* Even an output error does not prove that native TX/PM has ended. */
    emit=true;expected_output=native_result=-72;unsigned done=wake_dones;
    assert(submit()==-71 && wake_dones==done && wake_ups==done+1);
    __wrap_he_twt_information_txcb(information_buffer.eb);release(&information_buffer);cold_boot();
    expected_output=native_result=0;early_complete=true;done=wake_dones;
    assert(submit()==-71 && wake_dones==done+1 && wake_ups==wake_dones);
    release(&information_buffer);cold_boot();
    /* Retained record repays only its own PM after native recycle. Recycler
     * itself never invokes PM; duplicate callback cannot consume a reference. */
    early_complete=false;done=wake_dones;assert(submit()==-71);
    release(&information_buffer);assert(wake_dones==done && read_snapshot().tracked==1);
    __wrap_he_twt_information_txcb(information_buffer.eb);assert(wake_dones==done);
    esp32_mquickjs_wifi_twt_tx_cleanup_native();assert(wake_dones==done+1 && !read_snapshot().tracked);
    esp32_mquickjs_wifi_twt_tx_cleanup_native();assert(wake_dones==done+1);cold_boot();
    same_request=false;assert(submit()==-71);unsigned completed=information_callbacks;
    __wrap_he_twt_information_txcb(information_buffer.eb);
    __wrap_he_twt_information_txcb(information_buffer.eb);
    assert(information_callbacks==completed && wake_ups==wake_dones);release(&information_buffer);cold_boot();
    same_request=true;early_complete=true;information_recycle=true;
    assert(submit()==-71 && wake_ups==wake_dones && !read_snapshot().tracked);cold_boot();
    early_complete=false;information_recycle=true;output_hook=complete_inside_output;
    assert(submit()==-71 && wake_ups==wake_dones && !read_snapshot().tracked);cold_boot();
    /* A foreign task cannot run the native callback or repay native PM. */
    assert(submit()==-71);done=wake_dones;completed=information_callbacks;
    current_task=(void *)99;__wrap_he_twt_information_txcb(information_buffer.eb);
    release(&information_buffer);esp32_mquickjs_wifi_twt_tx_cleanup_native();
    assert(wake_dones==done && information_callbacks==completed && read_snapshot().tracked==1);
    current_task=(void *)31;esp32_mquickjs_wifi_twt_tx_cleanup_native();
    assert(wake_ups==wake_dones);cold_boot();
    /* An old pinned callback never clears a new TX at the same EB address. */
    assert(submit()==-71);callback_reuse=true;
    __wrap_he_twt_information_txcb(information_buffer.eb);
    assert(read_snapshot().tracked==1 && wake_ups==wake_dones+1);
    __wrap_he_twt_information_txcb(information_buffer.eb);release(&information_buffer);
    assert(wake_ups==wake_dones);cold_boot();
    /* The optional identity allocation fails after the base ledger succeeds.
     * No output is emitted; the producer repays PM once and retains diagnosis. */
    fail_allocation_at=2;expected_output=ESP_ERR_NO_MEM;done=wake_dones;
    assert(submit()==-71 && wake_dones==done+1 && wake_ups==wake_dones);
    assert(!output_calls && recycle_calls==1 && !read_snapshot().tracked && allocations==2);
    assert(read_snapshot().fault==ESP32_MQUICKJS_WIFI_TWT_TX_NO_MEMORY);
    assert(!s_twt_tx.information && read_snapshot().reserved_bytes==TWT_TX_CAPACITY*sizeof(twt_tx_entry_t));
    fail_allocation_at=0;assert(submit()==-71 && allocations==2 && wake_ups==wake_dones);cold_boot();
    malformed=true;expected_output=ESP_ERR_INVALID_STATE;
    assert(submit()==-71 && !output_calls && !read_snapshot().tracked && wake_ups==wake_dones);
    assert(read_snapshot().fault==ESP32_MQUICKJS_WIFI_TWT_TX_INFORMATION_IDENTITY);
    assert(!s_twt_tx.information);malformed=false;expected_output=0;cold_boot();
    in_isr=true;calls=information_calls;
    assert(submit()==ESP_ERR_INVALID_STATE && information_calls==calls);in_isr=false;
    /* Real information owner + real TX/output/callback/recycler path. */
    managed_operation=true;uint8_t control=0x63;
    esp32_mquickjs_wifi_twt_information_identity_t owned;
    assert(esp32_mquickjs_wifi_twt_sdk_information_capture_native(3,&control,&owned));
    uint32_t operation=0;
    assert(!esp32_mquickjs_wifi_twt_information_begin_native(71,&owned,120,false,&operation));
    unsigned posted=information_posts;information_post_error=0x107;
    assert(submit()==-71);
    esp32_mquickjs_wifi_twt_information_submitted_native(operation,-71);
    esp32_mquickjs_wifi_twt_information_result_t result;
    assert(esp32_mquickjs_wifi_twt_information_read(operation,&result) && result.tx_identity && !result.complete);
    esp32_mquickjs_wifi_twt_information_abandon(operation);
    assert(esp32_mquickjs_wifi_twt_information_reap_native(0)==ESP_ERR_NOT_FINISHED);
    __wrap_he_twt_information_txcb(information_buffer.eb);
    assert(esp32_mquickjs_wifi_twt_information_read(operation,&result) && result.complete &&
        !result.native_error && result.submit_error==-71 && result.observation_error==0x107);
    assert(information_posts==posted+1);
    __wrap_he_twt_information_txcb(information_buffer.eb);assert(information_posts==posted+1);
    assert(esp32_mquickjs_wifi_twt_information_reap_native(0)==ESP_ERR_NOT_FINISHED);
    release(&information_buffer);assert(!esp32_mquickjs_wifi_twt_information_reap_native(0));
    assert(!esp32_mquickjs_wifi_twt_information_pending());cold_boot();free(s_information.operation);
    assert(!locked && !s_twt_tx.information_submit);return 0;
}
'''
