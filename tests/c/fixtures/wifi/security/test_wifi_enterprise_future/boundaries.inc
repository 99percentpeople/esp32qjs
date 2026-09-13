
static struct {void (*fn)(void *);void *arg;} job;
static bool queue_full,disconnect_ready=true,ready=true,cancel_inside_sdk;
static unsigned installs,clears,disconnects;
static esp_err_t clear_error,helper_error;
static esp32_mquickjs_future_driver_state_t *active;
static bool esp32_mquickjs_submit_background_worker(void (*fn)(void *),void *arg){
    assert(!critical);if(queue_full || job.fn)return false;job.fn=fn;job.arg=arg;return true;
}
static void run_job(void){
    assert(job.fn);void (*fn)(void *)=job.fn;void *arg=job.arg;job.fn=NULL;job.arg=NULL;fn(arg);
}
static bool esp32_mquickjs_wifi_radio_eap_ready(void){return ready;}
static esp_err_t esp32_mquickjs_wifi_eap_capture_owners(esp32_mquickjs_wifi_radio_lease_t owners[3]){
    owners[0]=(esp32_mquickjs_wifi_radio_lease_t){1,77,0,true};return helper_error;
}
static esp_err_t esp32_mquickjs_wifi_radio_eap_status(uint64_t id,esp32_mquickjs_wifi_eap_install_result_t *r){
    assert(id==binding && id);r->enabled=ready;return ESP_OK;
}
static esp_err_t esp32_mquickjs_wifi_radio_eap_install(const esp32_mquickjs_wifi_radio_lease_t *a,
    const esp32_mquickjs_wifi_radio_lease_t *s,const esp32_mquickjs_wifi_radio_lease_t *ap,
    esp32_mquickjs_wifi_eap_profile_t *profile,esp32_mquickjs_wifi_eap_install_result_t *r){
    (void)s;(void)ap;assert(!critical && a->identity==77 && profile && !binding);installs++;
    if(cancel_inside_sdk)atomic_store(&active->cancelled,true);
    binding=100;ready=true;r->enabled=true;return ESP_OK;
}
static esp_err_t esp32_mquickjs_wifi_radio_eap_clear(uint64_t id,esp32_mquickjs_wifi_eap_install_result_t *r){
    (void)r;assert(!critical && id && id==binding);clears++;if(!clear_error)binding=0;return clear_error;
}
static esp_err_t esp32_mquickjs_wifi_eap_disconnect_ready(bool *out){
    assert(binding);disconnects++;*out=disconnect_ready;return ESP_OK;
}
