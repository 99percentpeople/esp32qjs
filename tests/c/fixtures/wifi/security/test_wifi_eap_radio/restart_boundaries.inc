
#define RADIO_EVENTS_IDLE 0
#define ESP_ERR_INVALID_RESPONSE -30
typedef __typeof__(s_radio.configuration) esp32_mquickjs_wifi_radio_config_result_t;
static struct {void *snapshot;} s_config_restart;
static struct {esp32_mquickjs_wifi_radio_lifecycle_t owner;} s_policy_restart;
static bool source_rejected;
static unsigned source_reads;
static int esp_wifi_get_mode(wifi_mode_t *mode){assert(locks&&!critical);*mode=s_radio.effective_mode;return 0;}
static int wifi_radio_record_fault(const char *stage,int error){
    assert(locks&&!critical);s_radio.fault_stage=stage;s_radio.fault_error=error;return error;
}
esp_err_t esp32_mquickjs_wifi_eap_install_restart_source(uint64_t id,
    esp32_mquickjs_wifi_eap_profile_t *profile,esp32_mquickjs_wifi_eap_install_result_t *out){
    assert(locks&&!critical&&profile==(void *)1);++source_reads;
    *out=(esp32_mquickjs_wifi_eap_install_result_t){.enabled=!source_rejected,.retained=true,.identity=id};
    return source_rejected?ESP_ERR_INVALID_STATE:ESP_OK;
}
