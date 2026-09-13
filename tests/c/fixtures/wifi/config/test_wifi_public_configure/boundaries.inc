
static bool esp32_mquickjs_wifi_capture_configuration(JSContext *ctx,JSValue options,
    esp32_mquickjs_wifi_configuration_t *capture) {
    assert(native_live==1 && JS_GetClassID(ctx,options)>=0);
    memset(capture,0x55,sizeof(*capture));memcpy(capture->station.sta.password,"capture-secret",14);
    if(scenario==6) {JS_ThrowTypeError(ctx,"injected capture rejection");return false;}
    if(scenario==7) {esp32_mquickjs_wifi_throw_configuration_error(ctx,ESP_ERR_NOT_SUPPORTED,NULL,NULL);return false;}
    if(scenario==8) {wifi_station_unsupported(ctx,"heDcmSet","wifi.configure");return false;}
    return true;
}
static int esp32_mquickjs_wifi_apply_configuration(esp32_mquickjs_wifi_configuration_t *capture,
    esp32_mquickjs_wifi_configuration_execution_t *execution) {
    assert(native_live==1 && !memcmp(capture->station.sta.password,"capture-secret",14));apply_calls++;
    *execution=(esp32_mquickjs_wifi_configuration_execution_t){.stage="admission"};
    if(scenario==1)return ESP_ERR_INVALID_STATE;
    execution->admitted=true;execution->stop_attempted=true;s_wifi_configuration_cleanup=true;
    execution->stage="configuration-stop";if(scenario==2)return -77;
    execution->configuration_attempted=true;execution->stage="configuration-commit";
    native_radio.configuration=(esp32_mquickjs_wifi_radio_config_result_t){.stage="country-config",
        .mutation_attempted=true,.persistent_mutation_possible=true,.error=-77};
    if(scenario==3)return -77;
    native_radio.configuration.stage="complete";native_radio.configuration.error=0;
    execution->resume_attempted=true;execution->stage="configuration-resume";
    if(scenario==4) {
        native_radio.fault_stage="tx-power-readback";
        native_radio.activation=(esp32_mquickjs_wifi_radio_config_result_t){.stage="tx-power-readback",.error=-77,.mutation_attempted=true};
        return -77;
    }
    if(scenario==5) {native_radio.fault_stage="start";return -77;}
    driver_started=true;s_wifi_configuration_cleanup=false;execution->stage="complete";return ESP_OK;
}
static JSValue esp32_mquickjs_wifi_make_status_object(JSContext *ctx) {
    assert(!native_live && free_calls==1 && driver_started);status_calls++;
    return JS_NewObject(ctx);
}
static void verified_free(void *p) {
    assert(native_live==1 && !free_calls);
    for(size_t i=0;i<sizeof(esp32_mquickjs_wifi_configuration_t);i++)assert(((unsigned char *)p)[i]==0);
    free_calls++;heap_caps_free(p);
}
#define esp32_mquickjs_memory_payload_free verified_free
