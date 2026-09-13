
bool current_task_is_wifi_task(void){return on_wifi;}
bool esp32qjs_eap_on_worker(void){return on_worker;}
bool esp32qjs_eap_configuration_idle(void){assert(on_wifi);return !foreign && !resources;}
void esp32qjs_eap_configuration_changed(void){assert(on_wifi);dirty=true;commits++;}
static int eloop_register_timeout_blocking(int (*fn)(void *,void *),void *a,void *b){
    assert(!on_wifi && !on_worker);dispatches++;if(dispatch_fail)return 72;
    on_wifi=true;int result=fn(a,b);on_wifi=false;return result;
}
esp_err_t esp32_mquickjs_wifi_eap_sdk_snapshot(esp32_mquickjs_wifi_eap_sdk_snapshot_t *out){
    assert(on_wifi);*out=(esp32_mquickjs_wifi_eap_sdk_snapshot_t){.entered=true,.resources=resources,
        .cleanup_error=observed_cleanup_error,.control_error=observed_control_error};return ESP_OK;
}
esp_err_t esp_wifi_sta_enterprise_disable(void){
    assert(on_wifi);clears++;if(clear_fail && activation_attempted)return 73;
    resources=0;borrowed_ca=borrowed_cert=borrowed_key=NULL;activation_attempted=false;return 0;
}
esp_err_t esp_wifi_sta_enterprise_enable(void){
    assert(on_wifi && dirty);activation_attempted=true;
    unsigned result=setter();resources|=ESP32_MQUICKJS_WIFI_EAP_SDK_DRIVER_ENABLED;
    if(!result){resources|=ESP32_MQUICKJS_WIFI_EAP_SDK_ENABLED;okc=true;}
    return result;
}
void esp_wifi_set_okc_support(bool value){assert(on_wifi);okc=value;}
esp_err_t esp_eap_client_set_disable_time_check(bool value){unsigned r=setter();if(!r)time_disabled=value;return r;}
esp_err_t esp_eap_client_set_eap_methods(esp_eap_method_t value){seen_methods=value;return setter();}
esp_err_t esp_eap_client_set_ttls_phase2_method(esp_eap_ttls_phase2_types value){seen_phase2=value;return setter();}
esp_err_t esp_eap_client_set_suiteb_192bit_certification(bool value){seen_suiteb=value;return setter();}
esp_err_t esp_eap_client_use_default_cert_bundle(bool value){seen_bundle=value;return setter();}
#define BYTES_SETTER(name) esp_err_t name(const unsigned char *p,int n){assert(p && n>0);unsigned r=setter();if(!r)resources|=ESP32_MQUICKJS_WIFI_EAP_SDK_GLOBAL_CREDENTIALS;return r;}
BYTES_SETTER(esp_eap_client_set_identity)
BYTES_SETTER(esp_eap_client_set_username)
BYTES_SETTER(esp_eap_client_set_password)
BYTES_SETTER(esp_eap_client_set_new_password)
esp_err_t esp_eap_client_set_ca_cert(const unsigned char *p,int n){
    unsigned r=setter();if(!r){borrowed_ca=p;ca_length=n;resources|=ESP32_MQUICKJS_WIFI_EAP_SDK_GLOBAL_CREDENTIALS;}return r;
}
esp_err_t esp_eap_client_set_certificate_and_key(const unsigned char *c,int cn,const unsigned char *k,int kn,const unsigned char *p,int pn){
    assert(p && pn==4 && !memcmp(p,"pass",4));unsigned r=setter();
    if(!r){borrowed_cert=c;cert_length=cn;borrowed_key=k;key_length=kn;resources|=ESP32_MQUICKJS_WIFI_EAP_SDK_GLOBAL_CREDENTIALS;}return r;
}
esp_err_t esp_eap_client_set_domain_name(const char *p){assert(p && !strcmp(p,"example.test"));return setter();}
esp_err_t esp_eap_client_set_fast_params(esp_eap_fast_config p){seen_fast=p.fast_provisioning;return setter();}
esp_err_t esp_eap_client_set_pac_file(const unsigned char *p,int n){assert(p && n==0);seen_pac++;return setter();}
