
#define ESP32_MQUICKJS_WIFI_RADIO_UNINITIALIZED 0
#define ESP32_MQUICKJS_WIFI_RADIO_STARTED 4
static struct {
    int lock,driver_state;bool storage_configured,started,restart_required,stop_required,promiscuous_claimed;
    const char *fault_stage,*cleanup_stage;
    wifi_mode_t effective_mode;wifi_storage_t storage;
    unsigned generation,next_lifecycle_identity,wake_locks;
    esp32_mquickjs_wifi_radio_lifecycle_t lifecycle;
    struct {unsigned identity;} operation;
    struct {unsigned identity;esp32_mquickjs_wifi_radio_client_t client;} leases[WIFI_RADIO_MAX_LEASES];
} s_radio;
static int locked,critical;static bool change_at_lock;
static void wifi_radio_operation_lock(void) {
    assert(!locked && !critical);locked=1;
    if(change_at_lock) {
        s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;s_radio.storage_configured=true;
        s_radio.started=true;s_radio.stop_required=true;s_radio.effective_mode=WIFI_MODE_AP;s_radio.storage=WIFI_STORAGE_FLASH;
    }
}
static void wifi_radio_operation_unlock(void) {assert(locked && !critical);locked=0;}
#define taskENTER_CRITICAL(p) do {(void)(p);assert(locked && !critical);critical=1;} while(0)
#define taskEXIT_CRITICAL(p) do {(void)(p);assert(locked && critical);critical=0;} while(0)
static void reset_fixture(void) {
    assert(!locked && !critical);memset(&s_radio,0,sizeof(s_radio));
    s_radio.generation=7;s_radio.next_lifecycle_identity=11;change_at_lock=false;
}
