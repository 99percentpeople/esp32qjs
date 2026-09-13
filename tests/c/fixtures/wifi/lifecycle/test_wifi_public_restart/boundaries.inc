
static struct {int sta_detach_error;} s_wifi_state;
static bool s_wifi_configuration_cleanup,wait_active,driver_started;
static unsigned wait_calls,end_calls,execute_calls,status_calls,diagnostic_calls;
static uint32_t waited_ms;
static int scenario;
typedef struct {
    const char *fault_stage;int fault_error;bool restart_required;
    esp32_mquickjs_wifi_radio_config_result_t configuration;
} esp32_mquickjs_wifi_radio_status_t;
static esp32_mquickjs_wifi_radio_status_t native_radio;
static int esp32_mquickjs_wifi_radio_get_status(esp32_mquickjs_wifi_radio_status_t *out) {
    assert(!wait_active);++diagnostic_calls;*out=native_radio;return ESP_OK;
}
static uint32_t esp32_mquickjs_wifi_radio_restart_snapshot_bytes(void) {
    return scenario>=3 && execute_calls ? 696 : 0;
}
static int esp32_mquickjs_wifi_ap_netif_cleanup_error(void) {return scenario==5 ? 88 : 0;}
static const char *esp_err_to_name(int error) {(void)error;return "injected-sdk-error";}
static int esp32_mquickjs_wifi_wait_begin(uint32_t timeout) {
    assert(!wait_active && !wait_calls);++wait_calls;waited_ms=timeout;
    if(scenario==1)return ESP_ERR_INVALID_STATE;wait_active=true;return ESP_OK;
}
static void esp32_mquickjs_wifi_wait_end(void) {
    assert(wait_active && execute_calls==1);wait_active=false;++end_calls;
}
static int esp32_mquickjs_wifi_restart_stopped_interfaces(
    bool allow_ap_restart, esp32_mquickjs_wifi_configuration_execution_t *execution) {
    assert(allow_ap_restart==(scenario==6));
    assert(wait_active && !execute_calls);++execute_calls;
    *execution=(esp32_mquickjs_wifi_configuration_execution_t){.stage="restart-admission"};
    if(scenario==2)return ESP_ERR_INVALID_STATE;
    execution->admitted=true;s_wifi_configuration_cleanup=true;
    if(scenario==7) {
        execution->stop_attempted=true;execution->stage="restart-retry-stop";
        native_radio.configuration=(esp32_mquickjs_wifi_radio_config_result_t){0};
        native_radio.fault_stage="original-start";native_radio.fault_error=77;return 88;
    }
    execution->stage="restart-stopped-station-prepare";
    if(scenario==3)return 77;
    execution->stop_attempted=true;execution->stage="restart-checkpoint";
    native_radio.configuration=(esp32_mquickjs_wifi_radio_config_result_t){.stage="restart-station-config",.error=77};
    if(scenario==4)return 77;
    execution->configuration_attempted=execution->resume_attempted=true;
    if(scenario==5){execution->stage="restart-resume";native_radio.fault_stage="start";native_radio.fault_error=77;return 77;}
    driver_started=true;s_wifi_configuration_cleanup=false;execution->stage="complete";return ESP_OK;
}
static JSValue wifi_make_status_object(JSContext *ctx) {
    assert(driver_started && !wait_active && execute_calls==1 && end_calls==1);++status_calls;
    return JS_NewObject(ctx);
}
