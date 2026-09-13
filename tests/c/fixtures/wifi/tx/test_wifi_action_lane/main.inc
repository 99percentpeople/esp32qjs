
#define SEND ESP32_MQUICKJS_WIFI_ACTION_SEND
#define ROC ESP32_MQUICKJS_WIFI_ACTION_ROC
static esp32_mquickjs_wifi_action_lane_t lane={.next_identity=1};
static esp32_mquickjs_wifi_action_token_t token;
static void reserve(unsigned kind) {
    assert(esp32_mquickjs_wifi_action_reserve(&lane,7,kind,0x1234,WIFI_IF_STA,6,&token)==ESP_OK);
}
static bool observe(unsigned id,unsigned status) {
    return esp32_mquickjs_wifi_action_observe(&lane,lane.kind,0x1234,WIFI_IF_STA,6,id,status);
}
static void fences(void) {
    uint32_t revision;assert(esp32_mquickjs_wifi_action_fence_revision(&lane,&token,&revision));
    assert(!esp32_mquickjs_wifi_action_event_fenced(&lane,&token,revision));
    assert(!esp32_mquickjs_wifi_action_sdk_fenced(&lane,&token,revision+1));
    assert(esp32_mquickjs_wifi_action_sdk_fenced(&lane,&token,revision));
    assert(esp32_mquickjs_wifi_action_event_fenced(&lane,&token,revision));
}
int main(void) {
    assert(esp32_mquickjs_wifi_action_reserve(NULL,7,SEND,1,0,6,&token)==ESP_ERR_INVALID_ARG);
    reserve(SEND);esp32_mquickjs_wifi_action_token_t stale=token,other={0};
    assert(esp32_mquickjs_wifi_action_reserve(&lane,7,SEND,1,0,6,&other)==ESP_ERR_INVALID_STATE);
    assert(!observe(1,WIFI_ACTION_TX_DONE)); /* No native dispatch yet. */
    assert(esp32_mquickjs_wifi_action_request_cancel(&lane,&token));
    assert(!esp32_mquickjs_wifi_action_begin_submit(&lane,&token));
    assert(esp32_mquickjs_wifi_action_release(&lane,&token));
    reserve(SEND);assert(!esp32_mquickjs_wifi_action_release(&lane,&stale));
    assert(esp32_mquickjs_wifi_action_begin_submit(&lane,&token));
    assert(!esp32_mquickjs_wifi_action_release(&lane,&token));
    assert(!esp32_mquickjs_wifi_action_terminated(&lane,&token));
    assert(!esp32_mquickjs_wifi_action_observe(&lane,SEND,99,0,6,0,0));
    assert(!esp32_mquickjs_wifi_action_observe(&lane,SEND,0x1234,1,6,0,0));
    assert(!esp32_mquickjs_wifi_action_observe(&lane,SEND,0x1234,0,7,0,0));
    assert(!esp32_mquickjs_wifi_action_observe(&lane,ROC,0x1234,0,6,0,0));
    assert(observe(0,WIFI_ACTION_TX_DONE) && observe(0,WIFI_ACTION_TX_DONE));
    assert(lane.early_count==1 && !lane.terminal);
    assert(esp32_mquickjs_wifi_action_submitted(&lane,&token,0,0));
    assert(lane.tx_status==WIFI_ACTION_TX_DONE && !lane.terminal && !lane.early_count);
    assert(!esp32_mquickjs_wifi_action_release(&lane,&token));
    assert(!observe(255,99) && !lane.ambiguous); /* Wrong SDK identity cannot poison current slot. */
    assert(observe(0,WIFI_ACTION_TX_DURATION_COMPLETED));fences();
    uint32_t previous=lane.revision;
    assert(observe(0,WIFI_ACTION_TX_DURATION_COMPLETED));
    assert(!lane.sdk_fenced && !lane.event_fenced);
    assert(!esp32_mquickjs_wifi_action_sdk_fenced(&lane,&token,previous));
    fences();assert(esp32_mquickjs_wifi_action_release(&lane,&token));
    assert(!esp32_mquickjs_wifi_action_release(&lane,&token));

    reserve(SEND);assert(esp32_mquickjs_wifi_action_begin_submit(&lane,&token));
    assert(observe(3,WIFI_ACTION_TX_DONE) && observe(3,WIFI_ACTION_TX_DURATION_COMPLETED));
    assert(esp32_mquickjs_wifi_action_submitted(&lane,&token,0,3));
    assert(lane.terminal && lane.tx_status==0);fences();assert(esp32_mquickjs_wifi_action_release(&lane,&token));

    reserve(SEND);assert(esp32_mquickjs_wifi_action_begin_submit(&lane,&token));
    assert(esp32_mquickjs_wifi_action_request_cancel(&lane,&token));
    assert(!esp32_mquickjs_wifi_action_begin_cancel(&lane,&token));
    assert(esp32_mquickjs_wifi_action_submitted(&lane,&token,77,255));
    assert(lane.submit_error==77 && !esp32_mquickjs_wifi_action_release(&lane,&token));
    assert(observe(255,WIFI_ACTION_TX_FAILED));assert(!lane.terminal);
    assert(esp32_mquickjs_wifi_action_begin_cancel(&lane,&token));
    assert(esp32_mquickjs_wifi_action_cancelled(&lane,&token,88));
    assert(lane.cancel_error==88 && !lane.cancel_written);
    assert(esp32_mquickjs_wifi_action_begin_cancel(&lane,&token));
    assert(observe(255,WIFI_ACTION_TX_OP_CANCELLED));
    uint32_t revision;
    assert(!esp32_mquickjs_wifi_action_fence_revision(&lane,&token,&revision));
    assert(!esp32_mquickjs_wifi_action_release(&lane,&token));
    assert(esp32_mquickjs_wifi_action_cancelled(&lane,&token,0));
    assert(lane.cancel_written && !esp32_mquickjs_wifi_action_begin_cancel(&lane,&token));
    fences();assert(esp32_mquickjs_wifi_action_release(&lane,&token));

    reserve(ROC);assert(esp32_mquickjs_wifi_action_begin_submit(&lane,&token));
    assert(esp32_mquickjs_wifi_action_submitted(&lane,&token,0,1));
    assert(esp32_mquickjs_wifi_action_request_cancel(&lane,&token));
    assert(esp32_mquickjs_wifi_action_begin_cancel(&lane,&token));
    assert(esp32_mquickjs_wifi_action_cancelled(&lane,&token,0));
    assert(!esp32_mquickjs_wifi_action_begin_cancel(&lane,&token));
    assert(!esp32_mquickjs_wifi_action_release(&lane,&token)); /* Cancel acceptance is not completion. */
    assert(observe(1,WIFI_ROC_FAIL));fences();assert(esp32_mquickjs_wifi_action_release(&lane,&token));

    reserve(ROC);assert(esp32_mquickjs_wifi_action_begin_submit(&lane,&token));
    for(unsigned i=0;i<5;++i)assert(observe(i,0));
    assert(lane.early_count==4 && lane.ambiguous);
    assert(esp32_mquickjs_wifi_action_submitted(&lane,&token,0,4));assert(observe(4,0));
    assert(!esp32_mquickjs_wifi_action_fence_revision(&lane,&token,&revision));
    assert(!esp32_mquickjs_wifi_action_release(&lane,&token));
    assert(esp32_mquickjs_wifi_action_terminated(&lane,&token));
    assert(!observe(4,0));assert(esp32_mquickjs_wifi_action_release(&lane,&token));

    reserve(SEND);assert(esp32_mquickjs_wifi_action_begin_submit(&lane,&token));
    assert(esp32_mquickjs_wifi_action_submitted(&lane,&token,0,7));
    assert(observe(7,2) && observe(7,3) && lane.ambiguous);
    assert(!esp32_mquickjs_wifi_action_release(&lane,&token));
    assert(esp32_mquickjs_wifi_action_terminated(&lane,&token));assert(esp32_mquickjs_wifi_action_release(&lane,&token));
    reserve(SEND);assert(esp32_mquickjs_wifi_action_begin_submit(&lane,&token));
    assert(esp32_mquickjs_wifi_action_submitted(&lane,&token,0,7));lane.revision=UINT32_MAX;
    assert(observe(7,2) && lane.ambiguous && lane.revision==UINT32_MAX);
    assert(esp32_mquickjs_wifi_action_terminated(&lane,&token));assert(esp32_mquickjs_wifi_action_release(&lane,&token));
    reserve(ROC);assert(esp32_mquickjs_wifi_action_begin_submit(&lane,&token));
    assert(esp32_mquickjs_wifi_action_submitted(&lane,&token,77,4));
    revision=lane.revision;
    assert(!esp32_mquickjs_wifi_action_quiescent(&lane,&token,revision+1));
    assert(esp32_mquickjs_wifi_action_quiescent(&lane,&token,revision));
    assert(!lane.terminal && lane.terminal_status==-1 && lane.sdk_quiescent);
    assert(!esp32_mquickjs_wifi_action_release(&lane,&token));
    assert(observe(4,99) && lane.ambiguous && !lane.sdk_quiescent);
    assert(!esp32_mquickjs_wifi_action_event_fenced(&lane,&token,revision));
    assert(!esp32_mquickjs_wifi_action_quiescent(&lane,&token,revision));
    assert(esp32_mquickjs_wifi_action_quiescent(&lane,&token,lane.revision));
    assert(!esp32_mquickjs_wifi_action_begin_cancel(&lane,&token));
    assert(esp32_mquickjs_wifi_action_event_fenced(&lane,&token,lane.revision));
    assert(esp32_mquickjs_wifi_action_release(&lane,&token));
    reserve(ROC);assert(esp32_mquickjs_wifi_action_begin_submit(&lane,&token));
    assert(esp32_mquickjs_wifi_action_submitted(&lane,&token,0,1));lane.revision=UINT32_MAX;
    assert(!esp32_mquickjs_wifi_action_quiescent(&lane,&token,UINT32_MAX));
    assert(esp32_mquickjs_wifi_action_terminated(&lane,&token));assert(esp32_mquickjs_wifi_action_release(&lane,&token));
    lane.next_identity=UINT32_MAX;reserve(SEND);assert(token.identity==UINT32_MAX);
    assert(esp32_mquickjs_wifi_action_release(&lane,&token));
    assert(esp32_mquickjs_wifi_action_reserve(&lane,8,SEND,1,0,6,&token)==ESP_ERR_NO_MEM);
    return 0;
}
