
#include <stdatomic.h>
#include <errno.h>
#define MALLOC_CAP_INTERNAL 2
#define ESP_ERR_NOT_SUPPORTED 5
#define configASSERT(value) assert(value)
#define portMAX_DELAY UINT32_MAX
#define pdTRUE 1
typedef pthread_mutex_t StaticSemaphore_t;
typedef StaticSemaphore_t *SemaphoreHandle_t;
static _Thread_local unsigned session_mutex_depth;
static unsigned control_allocations,fail_control_at;
static bool fail_mutex,queue_full;
static void *heap_caps_calloc(size_t count,size_t size,unsigned caps) {
    assert(!critical_depth && !session_mutex_depth && (caps==1 || caps==3));
    if(++control_allocations==fail_control_at)return NULL;
    void *p=heap_caps_malloc(count*size,MALLOC_CAP_8BIT);if(p)memset(p,0,count*size);return p;
}
static SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *storage) {
    assert(!critical_depth && !session_mutex_depth);if(fail_mutex)return NULL;
    assert(!pthread_mutex_init(storage,NULL));return storage;
}
static int xSemaphoreTake(SemaphoreHandle_t mutex,uint32_t wait) {
    assert(!critical_depth && !session_mutex_depth);
    int error=wait ? pthread_mutex_lock(mutex) : pthread_mutex_trylock(mutex);
    if(!error){++session_mutex_depth;return pdTRUE;}assert(!wait && error==EBUSY);return 0;
}
static void xSemaphoreGive(SemaphoreHandle_t mutex) {
    assert(session_mutex_depth==1 && !critical_depth);--session_mutex_depth;assert(!pthread_mutex_unlock(mutex));
}
static void vSemaphoreDelete(SemaphoreHandle_t mutex) {
    assert(!critical_depth && !session_mutex_depth);assert(!pthread_mutex_destroy(mutex));
}
static void session_payload_free(void *p) {
    assert(!session_mutex_depth);if(p)heap_caps_free(p);
}
#define esp32_mquickjs_memory_payload_free session_payload_free
static struct {void (*function)(void *);void *opaque;} jobs[16];
static unsigned job_count;
static bool esp32_mquickjs_submit_background_worker(void (*function)(void *),void *opaque) {
    assert(!critical_depth);if(queue_full || job_count==16)return false;
    jobs[job_count].function=function;jobs[job_count++].opaque=opaque;return true;
}
static void run_job(void) {
    assert(job_count && !session_mutex_depth);
    void (*function)(void *)=jobs[0].function;void *opaque=jobs[0].opaque;
    memmove(jobs,jobs+1,--job_count*sizeof(jobs[0]));function(opaque);
}
static unsigned acquires,releases,stops,live_leases,lease_serial;
static bool live_identity[64];
static esp_err_t acquire_error,stop_error;
static uint32_t rate_owner;
static pthread_mutex_t submit_pause_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t submit_changed=PTHREAD_COND_INITIALIZER;
static bool block_submit,submit_paused,submit_proceed;
static esp_err_t esp32_mquickjs_wifi_radio_raw_tx_acquire(esp32_mquickjs_wifi_raw_tx_interface_t interface,
    uint8_t channel,const wifi_tx_rate_config_t *rate,esp32_mquickjs_wifi_radio_lease_t *lease,uint8_t *actual) {
    assert(!critical_depth && !session_mutex_depth && !lease->acquired);
    if(rate)assert(esp32_mquickjs_wifi_tx_rate_valid(rate));
    assert(interface==ESP32_MQUICKJS_WIFI_RAW_TX_STATION && channel==6);
    ++acquires;++live_leases;assert(++lease_serial<64);live_identity[lease_serial]=true;
    *lease=(esp32_mquickjs_wifi_radio_lease_t){7,lease_serial,ESP32_MQUICKJS_WIFI_RADIO_CLIENT_RAW_TX,true};
    if(rate){assert(!rate_owner);rate_owner=lease->identity;}
    *actual=6;return acquire_error;
}
static esp_err_t esp32_mquickjs_wifi_radio_raw_tx_submit(const esp32_mquickjs_wifi_radio_lease_t *lease,
    esp32_mquickjs_wifi_raw_tx_interface_t interface,bool driver_sequence,const uint8_t *bytes,size_t length,
    token_t *token,esp32_mquickjs_wifi_raw_tx_validation_t *validation,uint8_t *channel) {
    assert(!critical_depth && !session_mutex_depth && lease->acquired && live_identity[lease->identity] && *channel==6);
    assert(!pthread_mutex_lock(&submit_pause_lock));
    if(block_submit) {
        submit_paused=true;assert(!pthread_cond_broadcast(&submit_changed));
        while(!submit_proceed)assert(!pthread_cond_wait(&submit_changed,&submit_pause_lock));
    }
    assert(!pthread_mutex_unlock(&submit_pause_lock));
    esp32_mquickjs_wifi_raw_tx_validation_policy_t policy={.interface=interface,.driver_sequence=driver_sequence};
    esp_err_t error=esp32_mquickjs_wifi_raw_tx_broker_register(lease->generation);if(error)return error;
    sending_token=token;
    error=esp32_mquickjs_wifi_raw_tx_broker_submit(lease->generation,lease->identity,bytes,length,&policy,token,validation);
    sending_token=NULL;return error;
}
static bool esp32_mquickjs_wifi_radio_raw_tx_retire(const esp32_mquickjs_wifi_radio_lease_t *lease,token_t *token) {
    assert(!critical_depth && !session_mutex_depth && lease->acquired && live_identity[lease->identity]);
    assert(token->radio_lease_identity==lease->identity);
    return esp32_mquickjs_wifi_raw_tx_broker_retire(token);
}
static esp_err_t esp32_mquickjs_wifi_radio_release_and_stop_idle(esp32_mquickjs_wifi_radio_lease_t *lease) {
    assert(!critical_depth && !session_mutex_depth);
    if(lease->acquired && rate_owner==lease->identity) {
        ++stops;if(stop_error)return stop_error;rate_owner=0;
    }
    if(lease->acquired) {
        assert(live_identity[lease->identity] && live_leases);live_identity[lease->identity]=false;
        --live_leases;++releases;memset(lease,0,sizeof(*lease));
    }
    if(live_leases)return ESP_OK;
    ++stops;return stop_error;
}
/* AP helper is an injected runtime-only boundary; its production coordinator
 * and native Radio handoff have separate fixtures. */
static bool ap_helper_live;
static unsigned ap_opens,ap_closes;
static esp_err_t ap_open_error,ap_close_error;
static esp_err_t esp32_mquickjs_wifi_ap_open_raw_tx_rate(
    esp32_mquickjs_wifi_raw_tx_ap_context_t *context,const wifi_tx_rate_config_t *rate,
    uint8_t channel,esp32_mquickjs_wifi_radio_lease_t *lease,uint8_t *actual,const char **stage) {
    assert(!critical_depth && !session_mutex_depth && !ap_helper_live && !context->owned && channel==6);
    assert(esp32_mquickjs_wifi_tx_rate_valid(rate));++ap_opens;
    context->owned=ap_helper_live=true;*stage="ap-prepare";
    if(ap_open_error)return ap_open_error;
    lease->acquired=true;lease->generation=7;lease->identity=++lease_serial;
    lease->client=ESP32_MQUICKJS_WIFI_RADIO_CLIENT_RAW_TX;
    live_identity[lease->identity]=true;++live_leases;rate_owner=lease->identity;*actual=6;return ESP_OK;
}
static esp_err_t esp32_mquickjs_wifi_ap_close_raw_tx_rate(
    esp32_mquickjs_wifi_raw_tx_ap_context_t *context,esp32_mquickjs_wifi_radio_lease_t *lease,
    bool physically_terminated,const char **stage) {
    assert(!critical_depth && !session_mutex_depth && context->owned);++ap_closes;*stage="ap-retire";
    if(ap_close_error)return ap_close_error;
    if(physically_terminated)assert(!lease->acquired);
    else {esp_err_t err=esp32_mquickjs_wifi_radio_release_and_stop_idle(lease);if(err)return err;}
    context->owned=ap_helper_live=false;*stage=NULL;return ESP_OK;
}

