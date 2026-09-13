
static struct {
    int lock;uint32_t generation,next_lease_identity,next_lifecycle_identity,wake_locks;
    bool driver_owned,storage_configured,started,stop_required,restart_required,promiscuous_claimed;
    const char *fault_stage,*cleanup_stage;esp_err_t fault_error;
    esp32_mquickjs_wifi_radio_driver_state_t driver_state;
    wifi_mode_t effective_mode;wifi_storage_t storage;
    uint32_t clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_COUNT];
    wifi_radio_live_lease_t leases[WIFI_RADIO_MAX_LEASES];
    struct {unsigned identity,lease_identity;} operation;
    esp32_mquickjs_wifi_radio_lifecycle_t lifecycle;
} s_radio;
static struct {struct {unsigned identity,owner_identity,generation;} owner;bool uncertain;} s_interval;
static struct {unsigned identity,generation;} s_tx_rate_lease;
static unsigned locks,critical,native_calls,fail_call,invalidations;
static unsigned removal_calls[10];
static uint8_t native_ie[10][257];
static uint16_t native_length[10];
static void (*on_lock)(void);
static void wifi_radio_operation_lock(void) {
    assert(!locks && !critical);locks=1;
    if(on_lock){void (*f)(void)=on_lock;on_lock=NULL;f();}
}
static void wifi_radio_operation_unlock(void) {assert(locks && !critical);locks=0;}
#define taskENTER_CRITICAL(p) do {(void)(p);assert(locks && !critical);critical=1;} while(0)
#define taskEXIT_CRITICAL(p) do {(void)(p);assert(locks && critical);critical=0;} while(0)
static void wifi_radio_invalidate_stop_snapshot_locked(void) {assert(locks && !critical);++invalidations;}
static void wifi_radio_release_promiscuous_locked(esp32_mquickjs_wifi_radio_promiscuous_lease_t *p) {(void)p;assert(0);}
static int esp_wifi_set_vendor_ie(bool enabled,wifi_vendor_ie_type_t frame,wifi_vendor_ie_id_t index,const void *data) {
    assert(locks && !critical && (unsigned)frame<5 && (unsigned)index<2);++native_calls;
    unsigned slot=(unsigned)frame*2U+index;
    if(enabled) {assert(data);const uint8_t *bytes=data;native_length[slot]=bytes[1]+2U;memcpy(native_ie[slot],bytes,native_length[slot]);}
    else {++removal_calls[slot];native_length[slot]=0;}
    /* Deliberately mutate even on an error: retained owner cannot assume absence. */
    return native_calls==fail_call ? 77 : ESP_OK;
}
