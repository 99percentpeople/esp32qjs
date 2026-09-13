
int main(void) {
    JSContext ctx=0;esp32_mquickjs_runtime_t runtime=0;
    s_wifi_state.cleanup_stage="scan-unregister";s_wifi_state.cleanup_error=-4;cleanup_fails=true;
    assert(!esp32_mquickjs_init_wifi_runtime(&ctx,&runtime));
    assert(cleanup_calls==1 && errors==1 && !s_wifi_runtime && !attached && !pollers);
    s_wifi_state.runtime_cleanup_pending=true;s_wifi_state.connect_draining=true;
    assert(!esp32_mquickjs_init_wifi_runtime(&ctx,&runtime));
    assert(connect_cancels==1 && scan_cancels==1 && !attached && !pollers);
    cleanup_fails=false;
    assert(esp32_mquickjs_init_wifi_runtime(&ctx,&runtime));
    assert(cleanup_calls==3 && s_wifi_runtime==&runtime && attached==1 && pollers==1);
    assert(connect_cancels==2 && scan_cancels==2 && !s_wifi_state.runtime_cleanup_pending);
}
