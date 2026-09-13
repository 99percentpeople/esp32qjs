
#define api(name) esp32_mquickjs_wifi_twt_fence_##name
typedef esp32_mquickjs_wifi_twt_fence_t fence_t;
typedef esp32_mquickjs_wifi_twt_token_t token_t;
static token_t token={.identity=UINT64_C(0x123456789abcdef0),.generation=17};
static void reset_driver(void) {
    assert(!timer_native.live && !timer_native.running);
    timer_native=(struct timer_native){0};
    creates=starts=stops=deletes=native_calls=0;
    create_error=start_error=stop_error=delete_error=native_error=0;
    early_callback=hold_callback_exit=false;
}
static void failed_start(void) {
    fence_t f={0};reset_driver();
    assert(api(begin)(NULL,&token,1)==ESP_ERR_INVALID_ARG);
    assert(api(begin)(&f,NULL,1)==ESP_ERR_INVALID_ARG);
    assert(api(begin)(&f,&token,UINT32_MAX)==ESP_ERR_INVALID_ARG && !creates);
    token_t bad=token;bad.generation=0;
    assert(api(begin)(&f,&bad,1)==ESP_ERR_INVALID_ARG && !creates);
    create_error=71;
    assert(api(begin)(&f,&token,1)==71 && f.start_error==71 && !f.timer);
    assert(!strcmp(f.stage,"twt-timer-create"));
    assert(api(begin)(&f,&token,1)==ESP_ERR_INVALID_STATE && creates==1);
    assert(api(poll)(&f,&token,1)==71 && !stops && !deletes && !native_calls);
    assert(api(clear)(&f,&token,1)==ESP_OK && !f.token.identity);
    reset_driver();start_error=72;
    assert(api(begin)(&f,&token,2)==72 && creates==1 && starts==1 && f.timer);
    assert(!strcmp(f.stage,"twt-timer-start"));
    stop_error=73;
    assert(api(clear)(&f,&token,2)==73 && f.start_error==72 && f.cleanup_error==73 && f.timer);
    assert(f.clearing && !strcmp(f.stage,"twt-timer-stop") && !deletes);
    assert(api(poll)(&f,&token,2)==ESP_ERR_INVALID_STATE);
    stop_error=0;delete_error=74;
    assert(api(clear)(&f,&token,2)==74 && f.stopped && stops==2 && deletes==1);
    assert(f.start_error==72 && !strcmp(f.stage,"twt-timer-delete"));
    delete_error=0;
    assert(api(clear)(&f,&token,2)==ESP_OK && stops==2 && deletes==2 && !f.timer && !native_calls);
    assert(api(clear)(&f,&token,2)==ESP_ERR_INVALID_STATE && deletes==2);
}
static void ordering_and_retry(void) {
    for(unsigned early=0;early<2;++early) {
        fence_t f={0};reset_driver();early_callback=early;hold_callback_exit=true;
        assert(api(begin)(&f,&token,3)==ESP_OK && f.started && !f.ready);
        token_t wrong=token;wrong.identity++;
        assert(api(poll)(&f,&wrong,3)==ESP_ERR_INVALID_STATE);
        assert(api(clear)(&f,&wrong,3)==ESP_ERR_INVALID_STATE);
        wrong=token;wrong.generation++;
        assert(api(poll)(&f,&wrong,3)==ESP_ERR_INVALID_STATE);
        assert(api(poll)(&f,&token,4)==ESP_ERR_INVALID_STATE && !stops && !native_calls);
        if(!early) {
            assert(api(poll)(&f,&token,3)==ESP_ERR_NOT_FINISHED && !stops && !deletes && !native_calls);
            fire();
        }
        assert(atomic_load(&f.reached) && timer_native.running);
        assert(api(poll)(&f,&token,3)==ESP_ERR_TIMEOUT && !deletes && !native_calls && f.timer);
        timer_native.running=false;delete_error=81;
        assert(api(poll)(&f,&token,3)==81 && f.stopped && stops==2 && deletes==1 && !native_calls);
        delete_error=0;native_error=82;
        assert(api(poll)(&f,&token,3)==82 && !f.timer && stops==2 && deletes==2 && native_calls==1);
        assert(!f.ready && !strcmp(f.stage,"twt-native-fence"));
        native_error=0;
        assert(api(poll)(&f,&token,3)==ESP_OK && f.ready && !f.stage && !f.cleanup_error);
        assert(stops==2 && deletes==2 && native_calls==2);
        assert(api(poll)(&f,&token,3)==ESP_OK && native_calls==2);
        assert(api(begin)(&f,&token,4)==ESP_ERR_INVALID_STATE && creates==1);
        assert(api(clear)(&f,&token,4)==ESP_ERR_INVALID_STATE && f.ready);
        assert(api(clear)(&f,&token,3)==ESP_OK && !f.token.identity && !atomic_load(&f.reached));
        assert(stops==2 && deletes==2 && native_calls==2);
        /* A fresh revision never consumes the previous callback's reached bit. */
        early_callback=false;hold_callback_exit=false;
        assert(api(begin)(&f,&token,4)==ESP_OK && creates==2);
        assert(api(poll)(&f,&token,4)==ESP_ERR_NOT_FINISHED);
        assert(api(poll)(&f,&token,3)==ESP_ERR_INVALID_STATE);
        assert(api(clear)(&f,&token,4)==ESP_OK && native_calls==2);
    }
}
static void close_and_late_callback(void) {
    fence_t f={0};reset_driver();
    assert(api(begin)(&f,&token,5)==ESP_OK);
    assert(api(clear)(&f,&token,5)==ESP_OK && !atomic_load(&f.reached) && !native_calls);
    assert(!timer_native.live && !timer_native.armed);
    assert(api(begin)(&f,&token,6)==ESP_OK);
    hold_callback_exit=true;fire();
    assert(api(clear)(&f,&token,6)==ESP_ERR_TIMEOUT && f.clearing && f.timer);
    assert(api(begin)(&f,&token,7)==ESP_ERR_INVALID_STATE);
    assert(api(poll)(&f,&token,6)==ESP_ERR_INVALID_STATE && !native_calls);
    timer_native.running=false;
    assert(api(clear)(&f,&token,6)==ESP_OK && !timer_native.live && !f.token.identity);
}
int main(void) {
    failed_start();ordering_and_retry();close_and_late_callback();return 0;
}
