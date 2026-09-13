
#define ESP_ERR_WIFI_NOT_STARTED -10
#define ESP_ERR_WIFI_NOT_CONNECT -11
static int link_result=ESP_ERR_WIFI_NOT_CONNECT,install_error,clear_error;
static bool retain_failure,status_fail,disconnect_pending,helpers_idle=true;
static unsigned installs,clears,disconnects,drains,releases;
static uint64_t binding,next_binding;
static bool config_busy,config_closing;
static struct {esp32_mquickjs_wifi_eap_config_token_t control;uint64_t binding;} s_wifi_eap_stop;
static bool stop_cleanup_ready;
static int wifi_finish_configuration_cleanup(void){
    assert(s_wifi_eap_stop.control.identity);
    if(!stop_cleanup_ready)return ESP_ERR_INVALID_STATE;
    s_wifi_eap_stop.control.identity=0;config_busy=false;return ESP_OK;
}
void esp32_mquickjs_wifi_eap_config_begin_close(void){assert(!locks);config_closing=true;}
void esp32_mquickjs_wifi_eap_config_status(esp32_mquickjs_wifi_eap_config_status_t *r){assert(!locks);*r=(esp32_mquickjs_wifi_eap_config_status_t){.busy=config_busy};}
bool esp32_mquickjs_wifi_eap_config_finish_close(void){assert(!locks && config_closing);return !binding && !config_busy;}
static int esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap){assert(locks && !critical);memset(ap,0xa5,sizeof(*ap));return link_result;}
esp_err_t esp32_mquickjs_wifi_eap_install(esp32_mquickjs_wifi_eap_profile_t *p,esp32_mquickjs_wifi_eap_install_result_t *r){
    assert(p && locks && !critical);installs++;
    *r=(esp32_mquickjs_wifi_eap_install_result_t){.entered=true,.error=install_error};
    if(!install_error || retain_failure){binding=++next_binding;r->retained=true;r->identity=binding;r->enabled=!install_error;}
    return install_error;
}
esp_err_t esp32_mquickjs_wifi_eap_install_clear(uint64_t id,esp32_mquickjs_wifi_eap_install_result_t *r){
    assert(locks && !critical && id==binding);clears++;
    *r=(esp32_mquickjs_wifi_eap_install_result_t){.error=clear_error};
    if(clear_error)return clear_error; /* Dispatch failure has no SDK snapshot. */
    binding=0;r->entered=true;r->sdk.entered=true;return 0;
}
esp_err_t esp32_mquickjs_wifi_eap_install_status(uint64_t id,esp32_mquickjs_wifi_eap_install_result_t *r){
    assert(locks && !critical && id==binding);
    *r=(esp32_mquickjs_wifi_eap_install_result_t){.entered=!status_fail,.identity=status_fail?0:binding,.retained=!status_fail};
    return status_fail?74:0;
}
static int esp32_mquickjs_wifi_start_disconnect(bool *pending){assert(!locks);disconnects++;*pending=disconnect_pending;return 0;}
static bool esp32_mquickjs_wifi_drain_scan(void){assert(!locks);drains++;return true;}
static void wifi_release_radio_operation(void){assert(!locks);releases++;}
static bool wifi_helpers_idle(bool connected){assert(!connected && !locks);return helpers_idle;}
