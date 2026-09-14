int main(int argc,char **argv) {
    assert(argc==2); int raw=atoi(argv[1]); int total=1;
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(192*1024);JSContext *ctx=JS_NewContext(heap,192*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef ref; JSValue *result=JS_PushGCRef(ctx,&ref);
        esp32_mquickjs_future_driver_state_t state={.generation=7,.operation=ESPNOW_OPERATION_SEND,.payload_length=3};
        memset(&s_espnow_session,0,sizeof(s_espnow_session));
        s_espnow_session.generation=7;s_espnow_session.active_queued_slot=ESP32_MQUICKJS_ESPNOW_TX_SLOT_NONE;
        s_espnow_session.active_send=&state;
        esp_now_send_info_t info={.des_addr=state.address};
        if(raw>=0) espnow_send_callback(&info,raw);
        else if(raw==-2) state.err=ESP_ERR_INVALID_STATE;
        assert(s_espnow_session.send_successes==(raw==0));
        assert(s_espnow_session.send_failures==(raw==1));
        assert(s_espnow_session.send_unknowns==(raw>1));
        assert(!s_espnow_session.callbacks_active);
        if(raw>=0) assert(state.tx_completed && state.tx_status==raw && state.completed);
        calls=0;fail_at=nth;inject=true;collect=true;
        *result=espnow_send_finish(ctx,&state);bool ok=!JS_IsException(*result);
        if(!nth){total=calls;assert(ok==(raw>=0));}
        inject=false;collect=false;
        if(ok) {
            assert(!JS_IsException(JS_SetPropertyStr(ctx,JS_GetGlobalObject(ctx),"tx",*result)));
            const char *check=raw==0 ? "tx.completion.status === 'success' && tx.completion.native.code === 0 && tx.completion.native.name === 'ESP_NOW_SEND_SUCCESS'" :
                raw==1 ? "tx.completion.status === 'failed' && tx.completion.native.code === 1 && tx.completion.native.name === 'ESP_NOW_SEND_FAIL'" :
                "tx.completion.status === 'unknown' && tx.completion.native.code === 99 && tx.completion.native.name === null";
            assert(JS_Eval(ctx,check,strlen(check),"completion",JS_EVAL_RETVAL)==JS_TRUE);
            check="tx.completion.native.domain === 'esp_now_send_status_t' && tx.macDelivered === undefined && tx.completedAtUs === 123456";
            assert(JS_Eval(ctx,check,strlen(check),"shape",JS_EVAL_RETVAL)==JS_TRUE);
        } else { assert(JS_HasException(ctx));*result=JS_GetException(ctx); }
        JS_PopGCRef(ctx,&ref);JS_FreeContext(ctx);assert(!root_count && !native_live);free(heap);
    }
    /* Exercise the actual queue callback and settlement: submit rejection is
     * neither a MAC callback nor WIFI_SEND_FAIL. Unknown codes remain intact. */
    for(int i=0;i<4;i++) {
        int status=i==2?99:i;
        memset(&s_espnow_session,0,sizeof(s_espnow_session));
        espnow_tx_packet_t packet={0};esp_now_send_info_t info={.des_addr=packet.address};
        s_espnow_session.tx_packets=&packet;s_espnow_session.tx_queue_capacity=1;
        if(i<3) espnow_send_callback(&info,status);
        else {s_espnow_session.queued_send_error=259;s_espnow_session.queued_send_completed=true;}
        espnow_complete_queued_send(&s_espnow_session);
        assert(s_espnow_session.tx_settled_packets==1);
        assert(s_espnow_session.tx_completed_packets==(i<3));
        assert(s_espnow_session.tx_failed_packets==(i==1));
        assert(s_espnow_session.tx_unknown_packets==(i==2));
        assert(s_espnow_session.tx_succeeded_packets==(i==0));
        assert(s_espnow_session.tx_submit_rejected_packets==(i==3));
        assert(s_espnow_session.tx_last_error==(i==3?259:0));
        assert(s_espnow_session.tx_has_completion==(i<3));
        if(i<3) assert(s_espnow_session.tx_last_status==status);
    }
    return 0;
}
