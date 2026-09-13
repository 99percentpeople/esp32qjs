
static void reset_action(void){
    reset_vendor();s_radio.next_operation_identity=1;s_radio.effective_mode=WIFI_MODE_STA;
    memset(&s_action,0,sizeof(s_action));s_action.lane.next_identity=1;
    action_calls=cancel_calls=fence_calls=posts=0;sdk_error=cancel_error=post_error=fence_error=0;
    connected=early=late_in_post=queued=false;sdk_id=255;
    quiescent_error=ESP_ERR_TIMEOUT;quiescent_calls=0;quiescent_event_race=false;
}
static void deliver(void){assert(queued);wifi_radio_operation_lock();wifi_radio_lifecycle_fence(NULL,7,2,&marker);wifi_radio_operation_unlock();queued=false;}
static void event(unsigned status){wifi_radio_operation_lock();action_event(status);wifi_radio_operation_unlock();}
static unsigned action_owners(void){return s_radio.clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_ACTION];}
int main(void){
    size_t size=sizeof(wifi_action_tx_req_t)+1;wifi_action_tx_req_t *request=calloc(1,size);assert(request);
    *request=(wifi_action_tx_req_t){.ifx=WIFI_IF_STA,.type=WIFI_OFFCHAN_TX_REQ,.channel=6,.wait_time_ms=100,
        .rx_cb=esp32_mquickjs_wifi_action_receive,.data_len=1};
    esp32_mquickjs_wifi_action_token_t token={0},stale;esp32_mquickjs_wifi_action_lane_t state;
    reset_action();request->data_len=UINT32_MAX;
    assert(esp32_mquickjs_wifi_radio_action_send(request,size,&token)==ESP_ERR_INVALID_ARG && !action_owners() && !action_calls);
    request->data_len=1;request->wait_time_ms=0;
    assert(esp32_mquickjs_wifi_radio_action_send(request,size,&token)==ESP_ERR_INVALID_ARG && !action_owners());request->wait_time_ms=100;
    s_radio.started=false;assert(esp32_mquickjs_wifi_radio_action_send(request,size,&token)==ESP_ERR_INVALID_STATE && !action_owners());s_radio.started=true;
    connected=true;request->channel=1;
    assert(esp32_mquickjs_wifi_radio_action_send(request,size,&token)==ESP_ERR_INVALID_STATE && !action_owners());
    connected=false;s_radio.effective_mode=WIFI_MODE_APSTA;
    assert(esp32_mquickjs_wifi_radio_action_send(request,size,&token)==ESP_ERR_INVALID_STATE && !action_owners());
    s_radio.effective_mode=WIFI_MODE_STA;request->channel=6;early=true;
    assert(esp32_mquickjs_wifi_radio_action_send(request,size,&token)==0 && token.identity && action_owners()==1 && invalidations==1);
    stale=token;wifi_radio_operation_lock();wifi_radio_release_locked(&s_action.lease);wifi_radio_operation_unlock();assert(action_owners()==1);
    esp32_mquickjs_wifi_radio_operation_t operation=s_radio.operation;esp32_mquickjs_wifi_radio_end_operation(&operation);assert(s_radio.operation.identity);
    assert(esp32_mquickjs_wifi_radio_action_retire(&token,&state)==ESP_ERR_TIMEOUT && fence_calls==1 && queued && action_owners()==1);
    deliver();assert(esp32_mquickjs_wifi_radio_action_retire(&token,&state)==0 && !action_owners() && !token.identity && !s_radio.operation.identity);
    assert(esp32_mquickjs_wifi_radio_action_cancel(&stale)==ESP_ERR_INVALID_STATE);
    assert(state.tx_status==0 && state.terminal_status==2);

    reset_action();request->op_id=0;
    assert(esp32_mquickjs_wifi_radio_action_send(request,size,&token)==0);event(0);
    assert(esp32_mquickjs_wifi_radio_action_retire(&token,&state)==ESP_ERR_TIMEOUT && !posts && !fence_calls && action_owners()==1);
    event(2);post_error=66;
    assert(esp32_mquickjs_wifi_radio_action_retire(&token,&state)==66 && !s_action.fence_posted && action_owners()==1);
    post_error=0;late_in_post=true;
    assert(esp32_mquickjs_wifi_radio_action_retire(&token,&state)==ESP_ERR_TIMEOUT);deliver();
    assert(!s_action.lane.event_fenced && !s_action.fence_posted);
    assert(esp32_mquickjs_wifi_radio_action_retire(&token,&state)==ESP_ERR_TIMEOUT && fence_calls==2);deliver();
    assert(esp32_mquickjs_wifi_radio_action_retire(&token,&state)==0 && !action_owners());

    reset_action();request->op_id=0;sdk_error=77;
    assert(esp32_mquickjs_wifi_radio_action_send(request,size,&token)==77 && token.identity && action_owners()==1);
    cancel_error=88;assert(esp32_mquickjs_wifi_radio_action_cancel(&token)==88 && cancel_calls==1 && action_owners()==1);
    cancel_error=0;assert(esp32_mquickjs_wifi_radio_action_cancel(&token)==0 && cancel_calls==2);
    assert(esp32_mquickjs_wifi_radio_action_cancel(&token)==0 && cancel_calls==2);
    assert(esp32_mquickjs_wifi_radio_action_retire(&token,&state)==ESP_ERR_TIMEOUT && !posts && action_owners()==1);
    event(3);assert(esp32_mquickjs_wifi_radio_action_retire(&token,&state)==ESP_ERR_TIMEOUT);deliver();
    assert(esp32_mquickjs_wifi_radio_action_retire(&token,&state)==0 && state.submit_error==77);

    reset_action();wifi_roc_req_t roc={.ifx=WIFI_IF_STA,.type=WIFI_ROC_REQ,.channel=1,.wait_time_ms=20,.rx_cb=esp32_mquickjs_wifi_action_receive};
    sdk_id=0;assert(esp32_mquickjs_wifi_radio_action_roc(&roc,&token)==0 && roc.op_id==0 && action_owners()==1);
    wifi_event_roc_done_t done={.context=(uint32_t)(uintptr_t)esp32_mquickjs_wifi_action_receive,.channel=1,.op_id=0,.status=WIFI_ROC_DONE};
    wifi_radio_operation_lock();wifi_radio_action_event(WIFI_EVENT_ROC_DONE,&done);wifi_radio_operation_unlock();
    assert(esp32_mquickjs_wifi_radio_action_retire(&token,&state)==ESP_ERR_TIMEOUT);deliver();
    assert(esp32_mquickjs_wifi_radio_action_retire(&token,&state)==0 && !action_owners());
    /* No terminal event: SDK queue proof alone still cannot release Radio.
     * An unknown matching event during the query invalidates its revision. */
    reset_action();request->op_id=0;sdk_error=77;
    assert(esp32_mquickjs_wifi_radio_action_send(request,size,&token)==77 && token.identity);
    assert(esp32_mquickjs_wifi_radio_action_retire(&token,&state)==ESP_ERR_TIMEOUT && !queued);
    quiescent_error=0;quiescent_event_race=true;
    assert(esp32_mquickjs_wifi_radio_action_retire(&token,&state)==ESP_ERR_TIMEOUT && !queued && action_owners()==1);
    assert(state.ambiguous && !state.sdk_quiescent);
    post_error=66;
    assert(esp32_mquickjs_wifi_radio_action_retire(&token,&state)==66 && action_owners()==1);
    assert(state.sdk_quiescent && !state.terminal && !state.event_fenced);
    unsigned cancels=cancel_calls;
    assert(esp32_mquickjs_wifi_radio_action_cancel(&token)==0 && cancel_calls==cancels);
    post_error=0;assert(esp32_mquickjs_wifi_radio_action_retire(&token,&state)==ESP_ERR_TIMEOUT && queued);
    deliver();assert(esp32_mquickjs_wifi_radio_action_retire(&token,&state)==0 && !action_owners());
    assert(state.sdk_quiescent && state.ambiguous && !state.terminal && state.terminal_status==-1);
    free(request);return 0;
}
