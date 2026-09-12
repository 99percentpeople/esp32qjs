"""Deferred broadcast setup through the real management TX ledger/wrappers.

Original output/callback/recycle, association and timer availability are injected.
No fixture import, compilation or execution before Wi-Fi staged validation.
"""
import unittest
from test_wifi_driver_phy import COMPONENT
from test_wifi_rx_target import unit
from test_wifi_twt_tx import PRELUDE, MAIN as TX_MAIN
from test_wireless_control_regression import compile_run


class WiFiTwtBroadcastTx(unittest.TestCase):
    def test_exact_body_close_duplicate_callback_and_storage_lifetime(self):
        code = PRELUDE
        code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_twt_probe_result.h')
        code += unit(COMPONENT / 'src/modules/wifi_twt/esp32_mquickjs_wifi_twt_probe_result.c')
        code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_twt_tx.h')
        tx = unit(COMPONENT / 'src/modules/wifi_twt/esp32_mquickjs_wifi_twt_tx.c')
        code += tx.replace('((const uint8_t *)buffer)[61]', '((const uint8_t *)buffer)[64]')
        compile_run(self, code + TX_MAIN.split('int main(void) {')[0] + MAIN)


MAIN = r'''
static void prepare_broadcast(buffer_t *b,uint8_t id) {
    prepare_setup(b,false);b->eb[64]=1;uint32_t length=15;memcpy(b->eb+24,&length,4);uint8_t *body=b->frame+24;
    memset(body,0,20);body[0]=22;body[1]=6;body[2]=9;
    body[3]=216;body[4]=10;body[5]=12;body[6]=1;body[10]=1;body[11]=1;body[13]=id<<3;
}
int main(void) {
    buffer_t a,b;
    cold_boot();prepare_broadcast(&a,3);prepare_broadcast(&b,4);assert(!send(&a) && !send(&b));
    uint32_t request=broadcast_request[3],revision=999;
    assert(esp32_mquickjs_wifi_twt_tx_broadcast_quiescent_native(3,request,&revision)==ESP_ERR_NOT_FINISHED && revision==999);
    current_task=(void *)32;assert(esp32_mquickjs_wifi_twt_tx_broadcast_cancel_request_native(3,request)==ESP_ERR_INVALID_STATE);
    current_task=(void *)31;assert(!esp32_mquickjs_wifi_twt_tx_broadcast_cancel_request_native(3,request));
    __wrap_he_twt_setup_txcb(a.eb);assert(!setup_callbacks);
    __wrap_he_twt_setup_txcb(b.eb);assert(setup_callbacks==1);
    assert(esp32_mquickjs_wifi_twt_tx_broadcast_quiescent_native(3,request,&revision)==ESP_ERR_NOT_FINISHED);
    release(&a);assert(!esp32_mquickjs_wifi_twt_tx_broadcast_quiescent_native(3,request,&revision) && revision!=999);
    release(&b);check(0,0,0);
    cold_boot();broadcast_held_mask=1U<<3;
    assert(esp32_mquickjs_wifi_twt_tx_broadcast_admit_native(3)==ESP_ERR_INVALID_STATE);
    assert(!esp32_mquickjs_wifi_twt_tx_broadcast_admit_native(4));cold_boot();
    wifi_btwt_setup_config_t config={.setup_cmd=0,.btwt_id=3,.timeout_time_ms=500};
    assert(!__wrap_ieee80211_btwt_setup((void *)17,&config) && broadcast_builds==1);
    prepare_broadcast(&a,3);assert(!send(&a));check(1,0,0);
    assert(a.frame[26]==1);
    assert(allocations==2 && read_snapshot().reserved_bytes==TWT_TX_CAPACITY*sizeof(twt_tx_entry_t)+sizeof(twt_tx_storage_t));
    assert(__wrap_ieee80211_btwt_setup((void *)17,&config)==ESP_ERR_INVALID_STATE && broadcast_builds==1);
    assert(esp32_mquickjs_wifi_twt_tx_broadcast_admit_native(3)==ESP_ERR_INVALID_STATE);
    assert(!esp32_mquickjs_wifi_twt_tx_broadcast_admit_native(4));
    current_task=(void *)32;__wrap_he_twt_setup_txcb(a.eb);assert(!setup_callbacks);
    current_task=(void *)31;__wrap_he_twt_setup_txcb(a.eb);assert(setup_callbacks==1);
    __wrap_he_twt_setup_txcb(a.eb);assert(setup_callbacks==1);release(&a);check(0,0,0);
    assert(!esp32_mquickjs_wifi_twt_tx_broadcast_admit_native(3));
    cold_boot();prepare_broadcast(&a,3);early_setup_callback=true;assert(!send(&a) && setup_callbacks==1);
    release(&a);check(0,0,0);
    cold_boot();prepare_broadcast(&a,3);assert(!send(&a));setup_callback_recycle=true;
    __wrap_he_twt_setup_txcb(a.eb);assert(setup_callbacks==1);check(0,0,0);
    cold_boot();prepare_broadcast(&a,3);assert(!send(&a));
    esp32_mquickjs_wifi_twt_tx_broadcast_cancel_native();
    __wrap_he_twt_setup_txcb(a.eb);assert(!setup_callbacks);release(&a);
    /* Reused node/address still cannot revive the cancelled old TX. */
    prepare_broadcast(&a,3);assert(!send(&a));assert(a.frame[26]==2);
    __wrap_he_twt_setup_txcb(a.eb);assert(setup_callbacks==1);release(&a);
    cold_boot();prepare_broadcast(&a,3);assert(!send(&a));a.frame[26]++;
    __wrap_he_twt_setup_txcb(a.eb);assert(!setup_callbacks);release(&a);
    cold_boot();prepare_broadcast(&a,3);assert(!send(&a));a.frame[37]^=8;
    __wrap_he_twt_setup_txcb(a.eb);assert(!setup_callbacks);release(&a);
    cold_boot();prepare_broadcast(&a,3);assert(!send(&a));broadcast_node_valid=false;
    __wrap_he_twt_setup_txcb(a.eb);assert(!setup_callbacks);release(&a);
    cold_boot();prepare_broadcast(&a,3);assert(!send(&a));broadcast_timer_error=ESP_ERR_INVALID_STATE;
    __wrap_he_twt_setup_txcb(a.eb);assert(!setup_callbacks);release(&a);
    cold_boot();prepare_broadcast(&a,3);broadcast_begin_error=ESP_ERR_NO_MEM;
    assert(send(&a)==ESP_ERR_NO_MEM && !output_calls && recycle_calls==1);check(0,0,0);
    cold_boot();prepare_broadcast(&a,3);fail_allocation_at=2;
    assert(send(&a)==ESP_ERR_NO_MEM && !output_calls && recycle_calls==1);check(0,0,0);
    cold_boot();prepare_broadcast(&a,3);prepare_broadcast(&b,3);assert(!send(&a));
    assert(send(&b)==ESP_ERR_INVALID_STATE && output_calls==1);release(&a);check(0,0,0);
    cold_boot();prepare_broadcast(&a,3);uint32_t length=12;memcpy(a.eb+24,&length,4);
    assert(send(&a)==ESP_ERR_INVALID_STATE && !output_calls);check(0,0,0);
    cold_boot();prepare_broadcast(&a,3);uint32_t protection=1;memcpy(a.metadata,&protection,4);
    memmove(a.frame+32,a.frame+24,15);a.frame[1]=0x40;a.frame[32]=23;a.frame[33]=4;
    memset(a.frame+47,0xa5,17); /* Bytes outside the transmitted IE are ignored. */
    assert(!send(&a));__wrap_he_twt_setup_txcb(a.eb);assert(setup_callbacks==1);release(&a);check(0,0,0);
    cold_boot();prepare_broadcast(&a,3);s_twt_tx.snapshot.revision=UINT32_MAX;
    assert(send(&a)==ESP_ERR_INVALID_STATE && !output_calls);check(0,0,0);
    cold_boot();config.btwt_id=32;assert(__wrap_ieee80211_btwt_setup((void *)17,&config)==ESP_ERR_INVALID_ARG && !broadcast_builds);
    config.btwt_id=3;broadcast_timer_error=81;
    assert(__wrap_ieee80211_btwt_setup((void *)17,&config)==81 && !broadcast_builds);
    broadcast_timer_error=0;broadcast_node_valid=false;
    assert(__wrap_ieee80211_btwt_setup((void *)17,&config)==ESP_ERR_INVALID_STATE && !broadcast_builds);
    cold_boot();
    for(unsigned dialog=1;dialog<=255;++dialog) {
        prepare_broadcast(&a,3);assert(!send(&a) && a.frame[26]==dialog);
        /* Connection close revokes execution, but never returns a wire token. */
        esp32_mquickjs_wifi_twt_tx_broadcast_cancel_native();release(&a);
    }
    assert(!read_snapshot().fault && read_snapshot().broadcast_dialog_exhausted_mask==(1U<<3));
    assert(esp32_mquickjs_wifi_twt_tx_broadcast_admit_native(3)==ESP_ERR_NO_MEM);
    unsigned outputs=output_calls;prepare_broadcast(&a,3);
    assert(send(&a)==ESP_ERR_NO_MEM && output_calls==outputs && a.frame[26]==9);
    assert(!read_snapshot().fault && s_twt_tx.information->broadcast_dialogs[3]==255);
    config.btwt_id=3;assert(__wrap_ieee80211_btwt_setup((void *)17,&config)==ESP_ERR_NO_MEM && !broadcast_builds);
    prepare_broadcast(&b,4);assert(!send(&b) && b.frame[26]==1);release(&b);
    assert(!read_snapshot().fault && read_snapshot().broadcast_dialog_exhausted_mask==(1U<<3));
    cold_boot();prepare_broadcast(&a,3);native_result=-71;
    assert(send(&a)==-71 && a.frame[26]==1);release(&a);native_result=0;
    prepare_broadcast(&a,3);assert(!send(&a) && a.frame[26]==2);release(&a);
    cold_boot();assert(!locked);return 0;
}
'''
