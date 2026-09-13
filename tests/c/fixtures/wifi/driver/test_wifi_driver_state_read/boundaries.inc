
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NOT_SUPPORTED 3
#define ESP_ERR_WIFI_NOT_INIT 4
#define ESP_ERR_WIFI_NOT_STARTED 5
#define ESP_ERR_INVALID_RESPONSE 6
#define WIFI_RADIO_MAX_LEASES 2
static unsigned locks,critical,reads,current_reads,home_reads;
static int sdk_error;
static bool overlap;
static wifi_mode_t native_mode;
static wifi_country_t country;
static uint8_t current_channel,home_channel;
static wifi_second_chan_t second;
static struct {
    int lock;
    uint32_t generation,channel_generation;
    uint64_t channel_observation_revision;
    esp_err_t channel_observation_error;
    uint8_t primary_channel;
    wifi_second_chan_t secondary_channel;
    wifi_radio_live_lease_t leases[WIFI_RADIO_MAX_LEASES];
    bool driver_owned,storage_configured,started,restart_required;
    struct {unsigned identity;} lifecycle,operation;
    const char *fault_stage,*cleanup_stage;
    esp32_mquickjs_wifi_radio_driver_state_t driver_state;
} s_radio;
static void wifi_radio_operation_lock(void) {assert(!locks && !critical);++locks;}
static void wifi_radio_operation_unlock(void) {assert(locks==1 && !critical);--locks;}
#define taskENTER_CRITICAL(p) do {(void)(p);assert(!critical);++critical;} while(0)
#define taskEXIT_CRITICAL(p) do {(void)(p);assert(critical==1);--critical;} while(0)
static int state_sdk_step(void) {assert(locks==1 && !critical);++reads;return sdk_error;}
static int wifi_radio_refresh_channel(uint8_t *,wifi_second_chan_t *,uint32_t *);
static int esp_wifi_get_mode(wifi_mode_t *out) {*out=native_mode;return state_sdk_step();}
static int esp_wifi_get_country(wifi_country_t *out) {*out=country;return state_sdk_step();}
static int esp_wifi_get_channel(uint8_t *out,wifi_second_chan_t *secondary) {
    *out=current_channel;*secondary=second;++current_reads;
    if(overlap) {
        /* Interrupt the older SDK read with a later production refresh. */
        overlap=false;current_channel=11;uint8_t p;wifi_second_chan_t s;uint32_t g;
        assert(wifi_radio_refresh_channel(&p,&s,&g)==ESP_OK);
    }
    return state_sdk_step();
}
static int esp_wifi_get_home_channel(uint8_t *out,wifi_second_chan_t *secondary) {
    *out=home_channel;*secondary=second;++home_reads;return state_sdk_step();
}
