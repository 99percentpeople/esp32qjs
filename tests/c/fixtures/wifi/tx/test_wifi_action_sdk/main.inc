
static void owner(wifi_action_rx_cb_t callback,unsigned interface,unsigned channel,unsigned secondary,unsigned id) {
    memset(g_offchan_ctx,0,sizeof(g_offchan_ctx));
    uint32_t context=(uint32_t)(uintptr_t)callback;
    memcpy(g_offchan_ctx+4,&context,4);memcpy(g_offchan_ctx+8,&interface,4);
    memcpy(g_offchan_ctx+20,&secondary,4);g_offchan_ctx[2]=channel;g_offchan_ctx[12]=id;
}
int main(void) {
    uint8_t message[32]={0};ioctl_task=true;
    wifi_action_tx_req_t action={.type=WIFI_OFFCHAN_TX_CANCEL,.rx_cb=esp32_mquickjs_wifi_action_receive,
        .ifx=WIFI_IF_STA,.channel=6,.sec_channel=WIFI_SECOND_CHAN_ABOVE,.op_id=255};
    wifi_roc_req_t roc={.type=WIFI_ROC_CANCEL,.rx_cb=esp32_mquickjs_wifi_action_receive,
        .ifx=WIFI_IF_AP,.channel=6,.sec_channel=WIFI_SECOND_CHAN_NONE,.op_id=0};
    assert(__wrap_wifi_action_tx_process(NULL)==ESP_ERR_INVALID_ARG && !delegates);
    assert(__wrap_wifi_roc_process(message)==ESP_ERR_INVALID_ARG && !delegates);
    for(unsigned kind=0;kind<2;++kind){
        void *request=kind?(void *)&roc:(void *)&action;memcpy(message+20,&request,sizeof(request));
        for(unsigned mismatch=0;mismatch<6;++mismatch){
            owner(esp32_mquickjs_wifi_action_receive,kind?WIFI_IF_AP:WIFI_IF_STA,6,
                kind?WIFI_SECOND_CHAN_NONE:WIFI_SECOND_CHAN_ABOVE,kind?0:255);
            if(mismatch==0)g_offchan_ctx[4]^=1;
            if(mismatch==1)g_offchan_ctx[8]^=1;
            if(mismatch==2)g_offchan_ctx[2]^=1;
            if(mismatch==3)g_offchan_ctx[20]^=1;
            if(mismatch==4)g_offchan_ctx[12]^=1;
            if(mismatch==5)memset(g_offchan_ctx,0,sizeof(g_offchan_ctx));
            unsigned before=delegates;
            assert((kind?__wrap_wifi_roc_process(message):__wrap_wifi_action_tx_process(message))==ESP_ERR_INVALID_STATE);
            assert(delegates==before);
        }
        owner(esp32_mquickjs_wifi_action_receive,kind?WIFI_IF_AP:WIFI_IF_STA,6,
            kind?WIFI_SECOND_CHAN_NONE:WIFI_SECOND_CHAN_ABOVE,kind?0:255);
        unsigned before=delegates;
        assert((kind?__wrap_wifi_roc_process(message):__wrap_wifi_action_tx_process(message))==(kind?88:77));
        assert(delegates==before+1);
        /* Existing native modules and normal submit remain delegated. */
        memset(g_offchan_ctx,0,sizeof(g_offchan_ctx));
        if(kind)roc.rx_cb=other_receive;else action.rx_cb=other_receive;
        assert((kind?__wrap_wifi_roc_process(message):__wrap_wifi_action_tx_process(message))==(kind?88:77));
        if(kind){roc.rx_cb=esp32_mquickjs_wifi_action_receive;roc.type=WIFI_ROC_REQ;}
        else{action.rx_cb=esp32_mquickjs_wifi_action_receive;action.type=WIFI_OFFCHAN_TX_REQ;}
        assert((kind?__wrap_wifi_roc_process(message):__wrap_wifi_action_tx_process(message))==(kind?88:77));
    }
    /* Private fence is acknowledged in this exact ioctl handler without
     * inspecting owner or delegating a driver mutation. */
    void *request=&action;memcpy(message+20,&request,sizeof(request));
    action.rx_cb=esp32_mquickjs_wifi_action_receive;action.type=ACTION_SDK_FENCE_TYPE;
    unsigned before=delegates;assert(__wrap_wifi_action_tx_process(message)==ESP_OK && delegates==before);
    action.type=ACTION_SDK_QUIESCENT_TYPE;memset(g_offchan_ctx,0,sizeof(g_offchan_ctx));
    assert(__wrap_wifi_action_tx_process(message)==ESP_OK && delegates==before);
    /* Every byte matters, including pending identity/channel and flags that
     * are not part of the two in-progress booleans. Foreign state also blocks. */
    for(unsigned i=0;i<sizeof(g_offchan_ctx);++i) {
        g_offchan_ctx[i]=1;
        assert(__wrap_wifi_action_tx_process(message)==ESP_ERR_TIMEOUT && delegates==before);
        g_offchan_ctx[i]=0;
    }
    action.rx_cb=other_receive;assert(__wrap_wifi_action_tx_process(message)==77 && delegates==before+1);
    return 0;
}
