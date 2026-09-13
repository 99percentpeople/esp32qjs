
typedef int JSContext,esp32_mquickjs_runtime_t;
typedef unsigned esp32_mquickjs_future_token_t;
typedef const void *esp32_mquickjs_resource_key_t;
typedef enum {ESP32_MQUICKJS_FUTURE_PENDING,ESP32_MQUICKJS_FUTURE_READY} esp32_mquickjs_future_poll_t;
typedef enum {ESP32_MQUICKJS_CANCEL_REQUESTED} esp32_mquickjs_cancel_result_t;
static struct {void (*function)(void *);void *opaque;} job;
static bool queue_full,cancel_during_acquire;
static unsigned acquires,releases,stops;
static esp_err_t acquire_error,stop_error;
static esp32_mquickjs_future_driver_state_t *running;
/* One-shot fixture has no Sessions; their production owner/worker is exercised
 * by test_wifi_raw_tx_session.py, sharing the same real arbiter and broker. */
static bool esp32_mquickjs_wifi_raw_tx_sessions_service(void) {return false;}
static bool esp32_mquickjs_wifi_raw_tx_sessions_runtime_service(void) {return false;}
static bool esp32_mquickjs_wifi_raw_tx_periodic_jobs_service(void) {return false;}
static void esp32_mquickjs_wifi_raw_tx_sessions_request_close(void) {}
static bool esp32_mquickjs_wifi_raw_tx_sessions_drained(void) {return true;}
static bool esp32_mquickjs_submit_background_worker(void (*function)(void *),void *opaque) {
    assert(!critical_depth);
    if(queue_full || job.function)return false;
    job.function=function;job.opaque=opaque;return true;
}
static void run_job(void) {
    assert(job.function);void (*function)(void *)=job.function;void *opaque=job.opaque;
    job.function=NULL;job.opaque=NULL;function(opaque);
}
static void esp32_mquickjs_memory_payload_free(void *p) {if(p)heap_caps_free(p);}
static esp_err_t esp32_mquickjs_wifi_radio_raw_tx_acquire(esp32_mquickjs_wifi_raw_tx_interface_t interface,
    uint8_t channel,const wifi_tx_rate_config_t *rate,esp32_mquickjs_wifi_radio_lease_t *lease,uint8_t *actual) {
    assert(!rate && !critical_depth && interface==ESP32_MQUICKJS_WIFI_RAW_TX_STATION && channel==6);
    ++acquires;*lease=(esp32_mquickjs_wifi_radio_lease_t){7,123,ESP32_MQUICKJS_WIFI_RADIO_CLIENT_RAW_TX,true};
    *actual=6;
    if(cancel_during_acquire)atomic_store(&running->cancel_requested,true);
    return acquire_error;
}
static esp_err_t esp32_mquickjs_wifi_radio_raw_tx_submit(const esp32_mquickjs_wifi_radio_lease_t *lease,
    esp32_mquickjs_wifi_raw_tx_interface_t interface,bool driver_sequence,const uint8_t *bytes,size_t length,
    token_t *token,esp32_mquickjs_wifi_raw_tx_validation_t *validation,uint8_t *channel) {
    assert(lease->acquired && *channel==6 && !critical_depth);
    esp32_mquickjs_wifi_raw_tx_validation_policy_t policy={.interface=interface,.driver_sequence=driver_sequence};
    esp_err_t error=esp32_mquickjs_wifi_raw_tx_broker_register(lease->generation);
    if(error)return error;
    sending_token=token;
    error=esp32_mquickjs_wifi_raw_tx_broker_submit(lease->generation,lease->identity,bytes,length,&policy,token,validation);
    sending_token=NULL;return error;
}
static bool esp32_mquickjs_wifi_radio_raw_tx_retire(const esp32_mquickjs_wifi_radio_lease_t *lease,token_t *token) {
    assert(lease->acquired && token->radio_lease_identity==lease->identity && !critical_depth);
    return esp32_mquickjs_wifi_raw_tx_broker_retire(token);
}
static esp_err_t esp32_mquickjs_wifi_radio_release_and_stop_idle(esp32_mquickjs_wifi_radio_lease_t *lease) {
    assert(!critical_depth);
    if(lease->acquired){++releases;memset(lease,0,sizeof(*lease));}
    ++stops;return stop_error;
}
