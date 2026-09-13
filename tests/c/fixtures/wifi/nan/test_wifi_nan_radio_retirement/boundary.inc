
static wifi_radio_nan_t *s_nan_radio;
static struct {
    int lock,driver_state;
    uint32_t generation;
    esp32_mquickjs_wifi_radio_operation_t operation;
    unsigned event_phase,event_live;
    bool stop_submitted,started,stop_required,restart_required,driver_owned;
    const char *fault_stage,*cleanup_stage;
    esp_err_t fault_error,cleanup_error;
    wifi_mode_t effective_mode;
    wifi_storage_t storage;
} s_radio;
static bool mutation_locked, critical;
static unsigned stops, waits, resets, detaches, unobserves, mode_writes, storage_writes, releases, frees;
static int wait_error, detach_error, observer_error, mode_error, storage_error;
static wifi_mode_t native_mode=4;
static esp_netif_t netif;
#define taskENTER_CRITICAL(p) do{(void)(p);assert(!critical);critical=true;}while(0)
#define taskEXIT_CRITICAL(p) do{(void)(p);assert(critical);critical=false;}while(0)
static void wifi_radio_operation_lock(void){assert(!mutation_locked);mutation_locked=true;}
static void wifi_radio_operation_unlock(void){assert(mutation_locked&&!critical);mutation_locked=false;}
static bool wifi_radio_lease_valid(const esp32_mquickjs_wifi_radio_lease_t *lease){
    return lease->acquired&&lease->generation==s_radio.generation&&lease->identity==s_radio.operation.lease_identity;
}
static void sdk_boundary(void){assert(mutation_locked&&!critical&&s_nan_radio&&s_radio.operation.identity);}
static int service_callback_error,tx_error;
static unsigned tx_seals,tx_closes;
static int esp32_mquickjs_wifi_nan_sdk_service_quiesce(uint8_t id){sdk_boundary();assert(!id);return service_callback_error;}
static void esp32_mquickjs_wifi_nan_tx_seal(void){sdk_boundary();++tx_seals;}
static int esp32_mquickjs_wifi_nan_tx_close(void){sdk_boundary();++tx_closes;return tx_error;}
static int wifi_radio_begin_events(unsigned phase,wifi_mode_t expected){sdk_boundary();assert(expected==4);s_radio.event_phase=phase;return ESP_OK;}
static void wifi_radio_set_state(int state){s_radio.driver_state=state;}
static int esp_wifi_stop(void){sdk_boundary();++stops;return ESP_OK;}
static int wifi_radio_wait_events_inner(void *runtime){sdk_boundary();assert(!runtime);++waits;if(!wait_error){s_radio.event_phase=0;s_radio.event_live=0;}return wait_error;}
static int esp32_mquickjs_wifi_nan_sdk_sync_reset(void){sdk_boundary();assert(!s_radio.started);++resets;return ESP_OK;}
static int esp32_mquickjs_wifi_netif_retire(esp_netif_t **p,int *error){sdk_boundary();assert(resets&&*p==&netif&&!*error);++detaches;if(!detach_error)*p=NULL;return detach_error;}
static int esp32_mquickjs_wifi_nan_sdk_unobserve(uint32_t *identity){sdk_boundary();assert(!s_nan_radio->netif&&*identity==9);++unobserves;if(!observer_error)*identity=0;return observer_error;}
static int esp_wifi_get_mode(wifi_mode_t *mode){sdk_boundary();*mode=native_mode;return ESP_OK;}
static int esp_wifi_set_mode(wifi_mode_t mode){sdk_boundary();assert(!s_nan_radio->observer);++mode_writes;native_mode=mode;return mode_error;}
static int esp_wifi_set_storage(wifi_storage_t storage){sdk_boundary();assert(storage==0);++storage_writes;return storage_error;}
static int wifi_radio_cleanup_fault(const char *stage,int error){sdk_boundary();s_radio.cleanup_stage=stage;s_radio.cleanup_error=error;return error;}
static void wifi_radio_release_locked(esp32_mquickjs_wifi_radio_lease_t *lease){assert(mutation_locked&&!critical&&!s_radio.operation.identity);++releases;memset(lease,0,sizeof(*lease));}
static void esp32_mquickjs_memory_payload_free(void *p){assert(mutation_locked&&!critical&&!s_nan_radio&&releases);++frees;free(p);}
