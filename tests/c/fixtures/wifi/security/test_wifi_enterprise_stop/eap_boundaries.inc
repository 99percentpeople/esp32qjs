
static bool s_wifi_ap_stop_cleanup;
static int wifi_adopt_ap_stop_cleanup(void){assert(0);return ESP_ERR_INVALID_STATE;}
static int wifi_cleanup_failed_init(void){assert(0);return ESP_ERR_INVALID_STATE;}
static struct {esp32_mquickjs_wifi_eap_config_token_t control;uint64_t binding;
    esp32_mquickjs_wifi_eap_profile_t *profile;bool release_ap,checkpoint_attempted;} s_wifi_eap_stop;
typedef struct {int unused;} esp32_mquickjs_wifi_eap_install_result_t;
static bool config_busy,config_closing;
static unsigned config_begins,config_finishes,clear_calls;
static uint64_t eap_binding;
static int clear_error;
static uint64_t esp32_mquickjs_wifi_radio_eap_identity(void){return eap_binding;}
static const esp32_mquickjs_wifi_radio_lease_t *esp32_mquickjs_wifi_ap_control_lease(void){return s_ap_lease.acquired?&s_ap_lease:NULL;}
void esp32_mquickjs_wifi_eap_config_status(esp32_mquickjs_wifi_eap_config_status_t *s){
    *s=(esp32_mquickjs_wifi_eap_config_status_t){.revision=9,.busy=config_busy,.closing=config_closing};
}
esp_err_t esp32_mquickjs_wifi_eap_config_begin(uint64_t revision,esp32_mquickjs_wifi_eap_config_action_t action,
    esp32_mquickjs_wifi_eap_config_token_t *t,esp32_mquickjs_wifi_eap_profile_t **p){
    assert(revision==9 && action==ESP32_MQUICKJS_WIFI_EAP_CONFIG_DISABLE && !t->identity);
    if(config_busy || config_closing)return ESP_ERR_INVALID_STATE;
    *t=(esp32_mquickjs_wifi_eap_config_token_t){88,9,action};*p=(void *)1;config_busy=true;config_begins++;return ESP_OK;
}
esp_err_t esp32_mquickjs_wifi_eap_config_finish(esp32_mquickjs_wifi_eap_config_token_t *t,bool discard){
    assert(config_busy && t->identity==88 && !discard);config_finishes++;config_busy=false;memset(t,0,sizeof(*t));return ESP_OK;
}
static int esp32_mquickjs_wifi_radio_eap_begin_stop(uint64_t binding,
    const esp32_mquickjs_wifi_radio_lease_t *app,const esp32_mquickjs_wifi_radio_lease_t *sta,
    const esp32_mquickjs_wifi_radio_lease_t *ap,esp32_mquickjs_wifi_radio_lifecycle_t *token){
    assert(binding==eap_binding && binding && config_busy);
    return esp32_mquickjs_wifi_radio_begin_lifecycle(app,sta,ap,token);
}
void esp32_mquickjs_wifi_eap_config_begin_close(void){config_closing=true;}
bool esp32_mquickjs_wifi_eap_config_finish_close(void){return config_closing && !config_busy && !eap_binding;}
static int esp32_mquickjs_wifi_start_disconnect(bool *pending){(void)pending;assert(0);return ESP_ERR_INVALID_STATE;}
static bool esp32_mquickjs_wifi_drain_scan(void){assert(0);return false;}
static int esp32_mquickjs_wifi_radio_eap_clear(uint64_t id,esp32_mquickjs_wifi_eap_install_result_t *r){
    (void)id;(void)r;assert(0);return ESP_ERR_INVALID_STATE;
}
static int esp32_mquickjs_wifi_radio_eap_clear_lifecycle(uint64_t binding,
    const esp32_mquickjs_wifi_radio_lifecycle_t *token,esp32_mquickjs_wifi_eap_install_result_t *result){
    (void)result;assert(exact(token) && config_busy);clear_calls++;
    if(!eap_binding)return ESP_OK;
    assert(binding==eap_binding);if(clear_error)return clear_error;eap_binding=0;return ESP_OK;
}
