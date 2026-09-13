
static void configure(void){
    uint8_t byte=42;esp32_mquickjs_wifi_eap_input_t input={.policy={.methods=ESP_EAP_TYPE_PEAP}};
    input.fields[ESP32_MQUICKJS_WIFI_EAP_USERNAME]=(esp32_mquickjs_wifi_eap_span_t){&byte,1};
    esp32_mquickjs_wifi_eap_profile_t *p=NULL;
    assert(esp32_mquickjs_wifi_eap_profile_create(&input,&p)==ESP_OK);
    assert(esp32_mquickjs_wifi_eap_config_replace(s_eap_config.revision,p)==ESP_OK);
    esp32_mquickjs_wifi_eap_profile_release(p);
}
static esp32_mquickjs_future_driver_state_t *begin(esp32_mquickjs_wifi_eap_config_action_t action){
    esp32_mquickjs_future_driver_state_t *s=calloc(1,sizeof(*s));assert(s);
    s->action=action;s->timeout_ms=5000;atomic_init(&s->done,false);atomic_init(&s->cancelled,false);
    assert(esp32_mquickjs_wifi_eap_config_begin(s_eap_config.revision,action,&s->control,&s->profile)==ESP_OK);
    active=s;return s;
}
static void finish(esp32_mquickjs_future_driver_state_t *s){
    assert(enterprise_poll(s)==ESP32_MQUICKJS_FUTURE_READY);enterprise_destroy(s);
    assert(!s_eap_config.operation.identity);
}
int main(void){
    assert(esp32_mquickjs_wifi_eap_config_open()==ESP_OK);configure();
    esp32_mquickjs_future_driver_state_t *s=begin(ESP32_MQUICKJS_WIFI_EAP_CONFIG_ENABLE);
    assert(enterprise_timeout(s)==5000);enterprise_destroy(s); /* Captured but never started. */
    assert(s_eap_config.configured && !s_eap_config.operation.identity && !installs);
    s=begin(ESP32_MQUICKJS_WIFI_EAP_CONFIG_ENABLE);assert(enterprise_start(NULL,NULL,0,s));
    queue_full=true;finish(s);queue_full=false;assert(!binding && !installs);
    s=begin(ESP32_MQUICKJS_WIFI_EAP_CONFIG_ENABLE);assert(enterprise_start(NULL,NULL,0,s));
    assert(enterprise_cancel(s)==ESP32_MQUICKJS_CANCEL_REQUESTED);finish(s);assert(!installs);
    s=begin(ESP32_MQUICKJS_WIFI_EAP_CONFIG_ENABLE);assert(enterprise_start(NULL,NULL,0,s));
    assert(enterprise_poll(s)==ESP32_MQUICKJS_FUTURE_PENDING);
    enterprise_cancel(s);assert(enterprise_poll(s)==ESP32_MQUICKJS_FUTURE_PENDING);
    assert(s_eap_config.operation_profile && s_eap_config.operation.identity);
    run_job();finish(s);assert(!installs); /* Queued worker observed cancellation. */
    s=begin(ESP32_MQUICKJS_WIFI_EAP_CONFIG_ENABLE);assert(enterprise_start(NULL,NULL,0,s));
    assert(enterprise_poll(s)==ESP32_MQUICKJS_FUTURE_PENDING);cancel_inside_sdk=true;
    run_job();cancel_inside_sdk=false;
    assert(binding && s_eap_config.operation.identity);
    finish(s);assert(installs==1 && binding && s_eap_config.configured);

    s=begin(ESP32_MQUICKJS_WIFI_EAP_CONFIG_CLEAR);assert(enterprise_start(NULL,NULL,0,s));
    disconnect_ready=false;assert(enterprise_poll(s)==ESP32_MQUICKJS_FUTURE_PENDING && !job.fn);
    enterprise_cancel(s);finish(s);assert(binding && !clears);
    disconnect_ready=true;clear_error=ESP_ERR_INVALID_STATE;
    s=begin(ESP32_MQUICKJS_WIFI_EAP_CONFIG_CLEAR);assert(enterprise_start(NULL,NULL,0,s));
    assert(enterprise_poll(s)==ESP32_MQUICKJS_FUTURE_PENDING);run_job();finish(s);
    assert(binding && s_eap_config.configured && clears==1);
    clear_error=ESP_OK;s=begin(ESP32_MQUICKJS_WIFI_EAP_CONFIG_CLEAR);assert(enterprise_start(NULL,NULL,0,s));
    assert(enterprise_poll(s)==ESP32_MQUICKJS_FUTURE_PENDING);run_job();finish(s);
    assert(!binding && !s_eap_config.configured && clears==2);
    configure();unsigned old_disconnects=disconnects;
    s=begin(ESP32_MQUICKJS_WIFI_EAP_CONFIG_DISABLE);assert(enterprise_start(NULL,NULL,0,s));
    assert(enterprise_poll(s)==ESP32_MQUICKJS_FUTURE_PENDING);run_job();finish(s);
    assert(s_eap_config.configured && disconnects==old_disconnects);
    esp32_mquickjs_wifi_eap_config_begin_close();assert(esp32_mquickjs_wifi_eap_config_finish_close());
    assert(!s_eap_profile_counts.profiles && !s_eap_profile_counts.reserved_bytes && !critical);
    return 0;
}
