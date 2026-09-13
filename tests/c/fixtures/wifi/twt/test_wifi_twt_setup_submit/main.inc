
static esp32_mquickjs_wifi_twt_setup_result_t read_result(uint32_t identity) {
    esp32_mquickjs_wifi_twt_setup_result_t result;
    assert(esp32_mquickjs_wifi_twt_setup_result_read(identity,&result)==ESP_OK);return result;
}
static wifi_itwt_setup_config_t config(void) {
    return (wifi_itwt_setup_config_t){.setup_cmd=TWT_REQUEST,.flow_id=0,.wake_invl_expn=10,
        .min_wake_dura=1,.wake_invl_mant=100,.twt_id=10,.timeout_time_ms=5000};
}
static void cold_boot(void) {
    assert(!locked);heap_caps_free(s_setup_results.entries);memset(&s_setup_results,0,sizeof(s_setup_results));
    s_setup_results.snapshot.last_request_id=-1;memset(&s_setup_submit,0,sizeof(s_setup_submit));
    sdk_calls=native_calls=snapshots=posts=allocations=0;tx_fault=ESP32_MQUICKJS_WIFI_TWT_TX_OK;
    ioctl_task=early_event=reenter=duplicate_enter=allocation_error=false;
    preflight_error=native_error=timer_error=late_timer_error=post_error=early_return=0;
}
static esp_err_t esp_event_post(const char *base,int32_t id,const void *data,size_t size,unsigned ticks) {
    assert(ioctl_task && !locked && base==WIFI_EVENT && id==28 && data && size==32 && !ticks);++posts;
    esp32_mquickjs_wifi_twt_setup_result_t result=read_result(s_setup_submit.result.identity);
    assert((result.flags&ESP32_MQUICKJS_WIFI_TWT_SETUP_SEEN) && (result.flags&ESP32_MQUICKJS_WIFI_TWT_SETUP_SUBMITTING));
    assert(s_setup_submit.result.driver_called);return post_error;
}
esp_err_t __real_wifi_sta_itwt_setup_process(void *message) {
    assert(ioctl_task && !locked);++native_calls;
    wifi_itwt_setup_config_t *value;memcpy(&value,(uint8_t *)message+12,sizeof(value));
    assert(value==&s_setup_submit.result.config && value!=caller_input && value->flow_id==1);
    assert(s_setup_submit.result.driver_called && !s_setup_submit.result.native_completed);
    if(early_event) {
        wifi_event_sta_itwt_setup_t event={.config=*value,.status=1,.reason=255,.target_wake_time=UINT64_C(0x123456789abc)};
        event.config.setup_cmd=TWT_ACCEPT;event.config.flow_id=5;
        assert(esp32_mquickjs_wifi_twt_setup_result_post(&event,sizeof(event))==post_error);
    }
    if(duplicate_enter)assert(__wrap_wifi_sta_itwt_setup_process(message)==ESP_ERR_INVALID_STATE && native_calls==1);
    timer_error=late_timer_error;return native_error;
}
esp_err_t esp_wifi_sta_itwt_setup(wifi_itwt_setup_config_t *value) {
    assert(!ioctl_task && !locked && value==&s_setup_submit.result.config && value!=caller_input);++sdk_calls;
    assert(s_setup_submit.occupied && s_setup_submit.result.identity);
    if(reenter) {
        wifi_itwt_setup_config_t other=*value;other.twt_id++;
        esp32_mquickjs_wifi_twt_setup_dispatch_t second={0};
        assert(esp32_mquickjs_wifi_twt_sdk_individual_submit(&other,&second)==ESP_ERR_INVALID_STATE && !second.identity && sdk_calls==1);
    }
    if(preflight_error)return preflight_error;
    if(early_return) {
        if(early_return==2) {
            ioctl_task=true;uint32_t identity=0;
            assert(esp32_mquickjs_wifi_twt_setup_submit_enter_native(value,&identity)==0);
            assert(esp32_mquickjs_wifi_twt_setup_submit_driver_native(value,identity));ioctl_task=false;
        }
        value->flow_id=7; /* must NOT be read as safe writeback */
        return early_return==2?83:ESP_OK;
    }
    uint8_t message[24]={0};memcpy(message+12,&value,sizeof(value));
    ioctl_task=true;esp_err_t error=__wrap_wifi_sta_itwt_setup_process(message);ioctl_task=false;
    assert(s_setup_submit.result.native_entered && s_setup_submit.result.native_completed);
    assert(read_result(s_setup_submit.result.identity).flags&ESP32_MQUICKJS_WIFI_TWT_SETUP_SUBMITTING);
    return error;
}
int main(void) {
    cold_boot();wifi_itwt_setup_config_t input=config();caller_input=&input;
    esp32_mquickjs_wifi_twt_setup_dispatch_t result={0};
    assert(esp32_mquickjs_wifi_twt_sdk_individual_submit(NULL,&result)==ESP_ERR_INVALID_ARG);
    input.min_wake_dura=0;assert(esp32_mquickjs_wifi_twt_sdk_individual_submit(&input,&result)==ESP_ERR_INVALID_ARG && !allocations && !sdk_calls);
    input=config();allocation_error=true;
    assert(esp32_mquickjs_wifi_twt_sdk_individual_submit(&input,&result)==ESP_ERR_NO_MEM && !result.identity && !s_setup_submit.occupied && !sdk_calls);
    allocation_error=false;preflight_error=81;
    assert(esp32_mquickjs_wifi_twt_sdk_individual_submit(&input,&result)==81 && result.identity==1 && result.sdk_error==81);
    assert(!result.native_entered && !result.driver_called && !result.native_completed && !result.handoff_error && !s_setup_submit.occupied);
    assert(read_result(result.identity).submit_error==81 && !(read_result(result.identity).flags&ESP32_MQUICKJS_WIFI_TWT_SETUP_SUBMITTING));
    result=(esp32_mquickjs_wifi_twt_setup_dispatch_t){0};unsigned before_calls=sdk_calls;
    assert(esp32_mquickjs_wifi_twt_sdk_individual_submit(&input,&result)==ESP_ERR_INVALID_ARG && !result.identity && sdk_calls==before_calls);
    cold_boot();input=config();result=(esp32_mquickjs_wifi_twt_setup_dispatch_t){0};
    reenter=duplicate_enter=early_event=true;post_error=82;
    assert(esp32_mquickjs_wifi_twt_sdk_individual_submit(&input,&result)==0);
    assert(sdk_calls==1 && native_calls==1 && posts==1 && result.identity==1);
    assert(result.native_entered && result.driver_called && result.native_completed && !result.handoff_error && !s_setup_submit.occupied);
    assert(input.flow_id==0 && result.config.flow_id==1); /* requested, writeback and AP result separate */
    esp32_mquickjs_wifi_twt_setup_result_t saved=read_result(result.identity);
    assert(saved.event.config.flow_id==5 && saved.observation_error==82 && !saved.event.reason);
    assert((saved.flags&ESP32_MQUICKJS_WIFI_TWT_SETUP_SUBMITTED) && !(saved.flags&ESP32_MQUICKJS_WIFI_TWT_SETUP_SUBMITTING));
    cold_boot();input=config();result=(esp32_mquickjs_wifi_twt_setup_dispatch_t){0};timer_error=84;
    assert(esp32_mquickjs_wifi_twt_sdk_individual_submit(&input,&result)==84 && result.native_entered && result.native_completed && !result.driver_called && !native_calls);
    assert(read_result(result.identity).submit_error==84);
    cold_boot();input=config();result=(esp32_mquickjs_wifi_twt_setup_dispatch_t){0};native_error=85;late_timer_error=86;
    assert(esp32_mquickjs_wifi_twt_sdk_individual_submit(&input,&result)==86 && native_calls==1 && result.driver_called);
    assert(result.sdk_error==86 && result.driver_error==85 && result.native_completed);
    assert(read_result(result.identity).submit_error==86);
    for(int scenario=1;scenario<=2;++scenario) {
        cold_boot();input=config();result=(esp32_mquickjs_wifi_twt_setup_dispatch_t){0};early_return=scenario;
        assert(esp32_mquickjs_wifi_twt_sdk_individual_submit(&input,&result)==ESP_ERR_INVALID_STATE);
        assert(result.identity==1 && result.handoff_error==ESP_ERR_INVALID_STATE && result.config.flow_id==0);
        assert(result.sdk_error==(scenario==2?83:0) && result.native_entered==(scenario==2));
        assert(s_setup_submit.occupied && s_setup_submit.result.config.flow_id==7);
        assert(read_result(result.identity).flags&ESP32_MQUICKJS_WIFI_TWT_SETUP_SUBMITTING);
        uint32_t identity=0;assert(esp32_mquickjs_wifi_twt_setup_submit_enter_native(&s_setup_submit.result.config,&identity)==ESP_ERR_INVALID_STATE);
        input.twt_id++;esp32_mquickjs_wifi_twt_setup_dispatch_t next={0};
        assert(esp32_mquickjs_wifi_twt_sdk_individual_submit(&input,&next)==ESP_ERR_INVALID_STATE && !next.identity && sdk_calls==1);
    }
    cold_boot();input=config();result=(esp32_mquickjs_wifi_twt_setup_dispatch_t){0};
    tx_fault=ESP32_MQUICKJS_WIFI_TWT_TX_SETUP_IDENTITY;
    assert(esp32_mquickjs_wifi_twt_sdk_individual_submit(&input,&result)==ESP_ERR_INVALID_STATE);
    assert(result.identity && result.native_entered && result.native_completed && !result.driver_called && !native_calls);
    assert(!s_setup_submit.occupied);
    cold_boot();uint32_t identity=0;input=config();
    assert(esp32_mquickjs_wifi_twt_setup_submit_enter_native(&input,&identity)==0 && !identity);
    assert(!esp32_mquickjs_wifi_twt_setup_submit_driver_native(&input,1));
    esp32_mquickjs_wifi_twt_setup_submit_complete_native(&input,1,0);assert(!s_setup_submit.occupied);
    assert(!live && !locked);return 0;
}
