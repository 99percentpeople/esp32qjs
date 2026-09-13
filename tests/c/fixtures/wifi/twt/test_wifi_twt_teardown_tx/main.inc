
static unsigned wake_ups,wake_dones,teardown_callbacks,broadcast_callbacks,broadcast_posts;
static int broadcast_post_error;
static bool broadcast_owner=true;
static esp_err_t td_observe(const void *data,size_t size) {
    assert(size==sizeof(wifi_event_sta_btwt_teardown_t) && data);
    assert(s_teardown_tx.state.callback_busy && s_teardown_tx.state.completion_seen);
    ++broadcast_posts;return broadcast_post_error;
}
bool esp32_mquickjs_wifi_btwt_setup_owner_native(unsigned slot,uint32_t identity,uintptr_t *node) {
    assert(!locked && slot==3 && identity);if(!broadcast_owner)return false;*node=17;return true;
}
void esp32_mquickjs_wifi_twt_sdk_broadcast_teardown_complete_native(uintptr_t node,uint8_t slot,uint8_t status) {
    assert(!locked && node==17 && slot==3);++broadcast_callbacks;
    wifi_event_sta_btwt_teardown_t event={.btwt_id=slot,.status=status==1?BTWT_TEARDOWN_SUCCESS:BTWT_TEARDOWN_FAIL};
    assert(!esp32_mquickjs_wifi_btwt_event_post(34,&event,sizeof(event)));
}
static bool agreement_matches=true,td_callback_recycle;
static uint32_t expected_identity;
static void (*td_native_callback_hook)(void *);
void pm_twt_wake_up(void) {assert(!locked);++wake_ups;}
void pm_twt_wake_done(void) {assert(!locked && wake_dones<wake_ups);++wake_dones;}
bool esp32_mquickjs_wifi_twt_sdk_teardown_tx_matches_native(uint32_t identity,uintptr_t node,uint8_t flow) {
    assert(!locked && identity==expected_identity && node==17 && flow==3);
    assert(s_teardown_tx.state.callback_busy);return agreement_matches;
}
void __real_he_twt_teardown_txcb(void *buffer) {
    assert(!locked);++teardown_callbacks;
    esp32_mquickjs_wifi_twt_teardown_wake_done_native();
    if(td_native_callback_hook)td_native_callback_hook(buffer);
    if(td_callback_recycle) {
        td_callback_recycle=false;__wrap_esf_buf_recycle(buffer);memset(buffer,0xa5,72);
    }
}
static void prepare_teardown(buffer_t *b) {
    prepare(b,1U<<18);b->eb[44]=1;b->eb[65]=0;b->metadata[57]=3;
}
static void begin_td(uint32_t identity) {
    expected_identity=identity;
    assert(esp32_mquickjs_wifi_twt_teardown_tx_begin_native(identity,17,3)==0);
    esp32_mquickjs_wifi_twt_teardown_wake_up_native();
}
static void release_after_external_proof(uint32_t identity) {
    uint32_t revision=0;assert(esp32_mquickjs_wifi_twt_teardown_tx_quiescent_native(identity,&revision)==0);
    assert(!esp32_mquickjs_wifi_twt_teardown_tx_release_native(identity,revision-1));
    assert(esp32_mquickjs_wifi_twt_teardown_tx_release_native(identity,revision));
    assert(!s_teardown_tx.state.identity && wake_ups==wake_dones);
}
static void early_teardown(void *buffer) {
    td_output_hook=NULL;__wrap_he_twt_teardown_txcb(buffer);
}
static void reused_unmanaged(void *buffer) {
    /* A new native allocation reuses the address while the old callback is
     * still pinned. The shared ledger, not the retained address, classifies it. */
    td_native_callback_hook=NULL;__wrap_esf_buf_recycle(buffer);
    buffer_t *b=(buffer_t *)buffer;prepare_teardown(b);
    esp32_mquickjs_wifi_twt_teardown_wake_up_native();
    assert(send(b)==0);__wrap_he_twt_teardown_txcb(buffer);release(b);
}
static void prepare_broadcast_teardown(buffer_t *b,bool protected_frame) {
    prepare_setup(b,protected_frame);uint32_t mask=1U<<18,length=3;
    memcpy(b->metadata+20,&mask,4);memcpy(b->eb+24,&length,4);
    b->eb[44]=0;b->eb[65]=1;b->metadata[19]=1;
    uint8_t *body=b->frame+(protected_frame?32:24);
    body[1]=protected_frame?5:7;body[2]=0x63;
}
static void broadcast_cases(void) {
    buffer_t b;uint8_t slot=99;uint32_t revision=0;
    assert(esp32_mquickjs_wifi_twt_teardown_tx_begin_broadcast_native(11,17,32)==ESP_ERR_INVALID_ARG);
    for(unsigned protected_frame=0;protected_frame<2;++protected_frame) {
        prepare_broadcast_teardown(&b,protected_frame);
        assert(esp32_mquickjs_wifi_twt_tx_broadcast_teardown_fields(b.eb,false,&slot) && slot==3);
        assert(!esp32_mquickjs_wifi_twt_teardown_tx_begin_broadcast_native(11,17,3));
        assert(esp32_mquickjs_wifi_twt_teardown_tx_begin_native(12,17,3)==ESP_ERR_INVALID_STATE);
        esp32_mquickjs_wifi_twt_teardown_wake_up_native();
        broadcast_post_error=83;td_output_hook=early_teardown;
        assert(!send(&b) && s_teardown_tx.state.completion_seen && s_teardown_tx.state.completion_status==1);
        assert(s_teardown_tx.state.observation_error==83 && !s_teardown_tx.state.completion_error);
        assert(esp32_mquickjs_wifi_twt_teardown_tx_holds_broadcast(3,11));
        assert(!esp32_mquickjs_wifi_twt_teardown_tx_end_native(11,0));
        unsigned calls=broadcast_callbacks;__wrap_he_twt_teardown_txcb(b.eb);assert(calls==broadcast_callbacks);
        assert(esp32_mquickjs_wifi_twt_teardown_tx_quiescent_native(11,&revision)==ESP_ERR_NOT_FINISHED);
        release(&b);release_after_external_proof(11);cold_boot();
    }
    /* Association revocation consumes no new control result, but returns the
     * operation's PM reference and keeps diagnostic completion failure. */
    prepare_broadcast_teardown(&b,false);broadcast_owner=false;
    assert(!esp32_mquickjs_wifi_twt_teardown_tx_begin_broadcast_native(12,17,3));
    esp32_mquickjs_wifi_twt_teardown_wake_up_native();assert(!send(&b));
    unsigned calls=broadcast_callbacks;__wrap_he_twt_teardown_txcb(b.eb);
    assert(calls==broadcast_callbacks && s_teardown_tx.state.completion_error==ESP_ERR_INVALID_STATE);
    assert(!esp32_mquickjs_wifi_twt_teardown_tx_end_native(12,0));release(&b);release_after_external_proof(12);cold_boot();
    broadcast_owner=true;
    prepare_broadcast_teardown(&b,false);b.frame[26]|=0x80;slot=99;
    assert(!esp32_mquickjs_wifi_twt_tx_broadcast_teardown_fields(b.eb,false,&slot) && slot==99);
    uint32_t length=2;memcpy(b.eb+24,&length,4);
    assert(!esp32_mquickjs_wifi_twt_tx_broadcast_teardown_fields(b.eb,false,&slot));
}
int main(void) {
    broadcast_cases();
    buffer_t a;prepare_teardown(&a);uint32_t revision=111;
    assert(esp32_mquickjs_wifi_twt_teardown_tx_begin_native(0,17,3)==ESP_ERR_INVALID_ARG);
    assert(esp32_mquickjs_wifi_twt_teardown_tx_begin_native(1,17,3)==0);
    assert(esp32_mquickjs_wifi_twt_teardown_tx_begin_native(2,17,3)==ESP_ERR_INVALID_STATE);
    assert(!esp32_mquickjs_wifi_twt_teardown_tx_abandon_native(2));
    assert(esp32_mquickjs_wifi_twt_teardown_tx_abandon_native(1));
    begin_td(2);assert(!esp32_mquickjs_wifi_twt_teardown_tx_abandon_native(2));
    assert(send(&a)==0);assert(esp32_mquickjs_wifi_twt_teardown_tx_end_native(2,0)==0);
    /* The same EB address with another identity cannot advance retirement. */
    uint32_t unchanged=s_teardown_tx.state.revision;
    esp32_mquickjs_wifi_twt_teardown_tx_output_returned(a.eb,1);
    esp32_mquickjs_wifi_twt_teardown_tx_recycle(a.eb,1,true);
    esp32_mquickjs_wifi_twt_teardown_tx_recycle(a.eb,1,false);
    assert(s_teardown_tx.state.revision==unchanged && !s_teardown_tx.state.recycled);
    assert(esp32_mquickjs_wifi_twt_teardown_tx_quiescent_native(2,&revision)==ESP_ERR_NOT_FINISHED && revision==111);
    __wrap_he_twt_teardown_txcb(a.eb);assert(teardown_callbacks==1 && wake_ups==wake_dones);
    __wrap_he_twt_teardown_txcb(a.eb);assert(teardown_callbacks==1);
    release(&a);release_after_external_proof(2);cold_boot();
    /* Native allocation failure before output still owes the acquired PM ref. */
    begin_td(3);assert(esp32_mquickjs_wifi_twt_teardown_tx_end_native(3,ESP_ERR_NO_MEM)==0);
    assert(s_teardown_tx.state.submit_error==ESP_ERR_NO_MEM);release_after_external_proof(3);
    /* Early callback can recycle and poison EB; wrapper uses retained state. */
    prepare_teardown(&a);begin_td(4);td_callback_recycle=true;td_output_hook=early_teardown;
    assert(send(&a)==0);assert(s_teardown_tx.state.recycled && s_teardown_tx.state.output_returned);
    assert(esp32_mquickjs_wifi_twt_teardown_tx_end_native(4,0)==0);release_after_external_proof(4);
    /* Recycle without callback does not run PM on the recycler path. */
    prepare_teardown(&a);begin_td(5);assert(send(&a)==0);
    assert(esp32_mquickjs_wifi_twt_teardown_tx_end_native(5,0)==0);
    unsigned done=wake_dones;release(&a);assert(wake_dones==done && s_teardown_tx.state.wake_held);
    __wrap_he_twt_teardown_txcb(a.eb);assert(wake_dones==done);
    release_after_external_proof(5);
    /* New native node/request rejects old control mutation but balances PM. */
    prepare_teardown(&a);begin_td(6);assert(send(&a)==0);agreement_matches=false;
    unsigned callbacks=teardown_callbacks;__wrap_he_twt_teardown_txcb(a.eb);
    assert(teardown_callbacks==callbacks && !s_teardown_tx.state.wake_held);
    agreement_matches=true;assert(esp32_mquickjs_wifi_twt_teardown_tx_end_native(6,0)==0);
    release(&a);release_after_external_proof(6);
    /* Unmanaged reuse while the managed callback is active keeps distinct PM. */
    prepare_teardown(&a);begin_td(7);assert(send(&a)==0);
    assert(esp32_mquickjs_wifi_twt_teardown_tx_end_native(7,0)==0);
    td_native_callback_hook=reused_unmanaged;__wrap_he_twt_teardown_txcb(a.eb);
    assert(!s_teardown_tx.passthrough_depth && !s_teardown_tx.state.callback_busy);
    release_after_external_proof(7);cold_boot();
    /* Driver error does not prove TX/recycler completion. */
    prepare_teardown(&a);begin_td(8);native_result=-77;assert(send(&a)==-77);
    assert(esp32_mquickjs_wifi_twt_teardown_tx_end_native(8,-77)==0);
    assert(esp32_mquickjs_wifi_twt_teardown_tx_quiescent_native(8,&revision)==ESP_ERR_NOT_FINISHED);
    release(&a);release_after_external_proof(8);cold_boot();
    /* Failed ledger admission returns the EB, balances PM at native return,
     * and keeps a diagnostic fault instead of asserting successful retirement. */
    prepare_teardown(&a);begin_td(9);fail_allocate=true;
    assert(send(&a)==ESP_ERR_NO_MEM && output_calls==0 && recycle_calls==1);
    assert(esp32_mquickjs_wifi_twt_teardown_tx_end_native(9,ESP_ERR_NO_MEM)==ESP_ERR_NO_MEM);
    assert(wake_ups==wake_dones && s_teardown_tx.state.fault==ESP_ERR_NO_MEM);
    assert(esp32_mquickjs_wifi_twt_teardown_tx_quiescent_native(9,&revision)==ESP_ERR_NO_MEM);
    /* Test-only cold boot, not runtime recovery of a faulted owner. */
    memset(&s_teardown_tx,0,sizeof(s_teardown_tx));cold_boot();assert(!locked);return 0;
}
