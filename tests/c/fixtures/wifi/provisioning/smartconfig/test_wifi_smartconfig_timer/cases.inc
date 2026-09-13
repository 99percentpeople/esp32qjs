
static ETSTimer native[ESP32_MQUICKJS_SMARTCONFIG_TIMERS],foreign;
static ETSTimer *addresses[ESP32_MQUICKJS_SMARTCONFIG_TIMERS];
static esp32_mquickjs_wifi_smartconfig_token_t owner={.identity=1,.radio_generation=7};
static int called;
static bool close_inside_callback;
static void native_callback(void *arg) {
    assert(arg==&called && !locked);called++;
    if(close_inside_callback){
        assert(esp32_mquickjs_wifi_smartconfig_timers_close(&owner)==ESP_OK);
        __wrap_ets_timer_done(&native[0]);
        assert(esp32_mquickjs_wifi_smartconfig_timers_release(&owner)==ESP_ERR_INVALID_STATE);
    }
}
static void release_during_cleanup(void) {
    assert(esp32_mquickjs_wifi_smartconfig_timers_release(&owner)==ESP_ERR_INVALID_STATE);
}
static void clear_owner(void) {
    assert(esp32_mquickjs_wifi_smartconfig_timers_close(&owner)==ESP_OK);
    assert(esp32_mquickjs_wifi_smartconfig_timers_cleanup(&owner)==ESP_OK);
    assert(esp32_mquickjs_wifi_smartconfig_timers_release(&owner)==ESP_OK);
    assert(allocs==frees);owner.identity++;
}
int main(void) {
    for(unsigned i=0;i<ESP32_MQUICKJS_SMARTCONFIG_TIMERS;i++)addresses[i]=&native[i];
    __wrap_ets_timer_setfn(&foreign,native_callback,&called);__wrap_ets_timer_disarm(&foreign);
    __wrap_ets_timer_done(&foreign);__wrap_ets_timer_arm(&foreign,2,false);__wrap_ets_timer_arm_us(&foreign,3,true);
    assert(forwards==5);
    alloc_error=true;assert(esp32_mquickjs_wifi_smartconfig_timers_begin(&owner,addresses)==ESP_ERR_NO_MEM);
    alloc_error=false;
    addresses[1]=addresses[0];assert(esp32_mquickjs_wifi_smartconfig_timers_begin(&owner,addresses)==ESP_ERR_INVALID_ARG);
    addresses[1]=&native[1];native[2].timer_arg=&foreign;
    assert(esp32_mquickjs_wifi_smartconfig_timers_begin(&owner,addresses)==ESP_ERR_INVALID_STATE);
    native[2].timer_arg=NULL;
    assert(esp32_mquickjs_wifi_smartconfig_timers_begin(&owner,addresses)==ESP_OK);
    esp32_mquickjs_wifi_smartconfig_token_t stale=owner;stale.radio_generation++;
    assert(esp32_mquickjs_wifi_smartconfig_timers_close(&stale)==ESP_ERR_INVALID_STATE);
    create_error=ESP_ERR_NO_MEM;__wrap_ets_timer_setfn(&native[0],native_callback,&called);
    assert(!native[0].timer_arg && s_sc_timers->error==ESP_ERR_NO_MEM);
    create_error=0;__wrap_ets_timer_setfn(&native[0],native_callback,&called);
    esp_timer_handle_t first=native[0].timer_arg;assert(first);
    void *old_arg=first->args.arg;
    __wrap_ets_timer_arm(&native[0],UINT32_MAX,true);
    assert(latest_us==UINT64_C(4294967295000) && latest_repeat);
    first->args.callback(first->args.arg);assert(called==1);
    __wrap_ets_timer_disarm(&native[0]);first->args.callback(first->args.arg);assert(called==1);
    start_error=ESP_FAIL;__wrap_ets_timer_arm_us(&native[0],20,false);assert(!first->armed);
    start_error=0;__wrap_ets_timer_arm_us(&native[0],20,false);assert(first->armed);
    blocking_error=ESP_ERR_TIMEOUT;__wrap_ets_timer_done(&native[0]);
    assert(native[0].timer_arg==first && first->alive && !s_sc_timers->entries[0].enabled);
    blocking_error=0;delete_error=ESP_FAIL;__wrap_ets_timer_done(&native[0]);
    assert(native[0].timer_arg==first && first->alive);
    int stopped_count=stops,started_count=starts;
    __wrap_ets_timer_arm(&native[0],1,false);assert(starts==started_count);
    __wrap_ets_timer_disarm(&native[0]);assert(stops==stopped_count);
    delete_error=0;__wrap_ets_timer_done(&native[0]);assert(!native[0].timer_arg && !first->alive && stops==stopped_count);
    __wrap_ets_timer_setfn(&native[0],native_callback,&called);
    esp_timer_handle_t second=native[0].timer_arg;assert(second && second->args.arg!=old_arg);
    __wrap_ets_timer_arm(&native[0],1,false);
    first->args.callback(old_arg);assert(called==1);
    close_inside_callback=true;second->args.callback(second->args.arg);assert(called==2);
    assert(!second->alive && !native[0].timer_arg);
    __wrap_ets_timer_setfn(&native[0],native_callback,&called);assert(!native[0].timer_arg);
    clear_owner();close_inside_callback=false;
    assert(esp32_mquickjs_wifi_smartconfig_timers_begin(&owner,addresses)==ESP_OK);
    for(unsigned i=0;i<ESP32_MQUICKJS_SMARTCONFIG_TIMERS;i++){
        __wrap_ets_timer_setfn(&native[i],native_callback,&called);__wrap_ets_timer_arm(&native[i],1,false);
    }
    assert(esp32_mquickjs_wifi_smartconfig_timers_release(&owner)==ESP_ERR_INVALID_STATE);
    assert(esp32_mquickjs_wifi_smartconfig_timers_close(&owner)==ESP_OK);
    delete_error=ESP_FAIL;assert(esp32_mquickjs_wifi_smartconfig_timers_cleanup(&owner)==ESP_FAIL);
    assert(esp32_mquickjs_wifi_smartconfig_timers_release(&owner)==ESP_ERR_INVALID_STATE);
    delete_error=0;during_cleanup=release_during_cleanup;
    assert(esp32_mquickjs_wifi_smartconfig_timers_cleanup(&owner)==ESP_OK);
    during_cleanup=NULL;
    esp32_mquickjs_wifi_smartconfig_timer_status_t status;
    assert(esp32_mquickjs_wifi_smartconfig_timers_status(&owner,&status)==ESP_OK);
    assert(!status.handles && !status.callbacks && !status.busy && !status.cleanup_error && status.error==ESP_FAIL);
    clear_owner();
    assert(esp32_mquickjs_wifi_smartconfig_timers_begin(&owner,addresses)==ESP_OK);
    s_sc_timer_identity=UINT32_MAX;
    __wrap_ets_timer_setfn(&native[0],native_callback,&called);
    assert(!native[0].timer_arg && s_sc_timers->error==ESP_ERR_NO_MEM);
    clear_owner();assert(forwards==5);
    return 0;
}
