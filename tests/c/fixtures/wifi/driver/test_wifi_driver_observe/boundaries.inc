
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NOT_SUPPORTED 3
#define ESP_ERR_WIFI_NOT_INIT 4
#define ESP_ERR_WIFI_NOT_STARTED 5
#define ESP_ERR_INVALID_RESPONSE 6
static unsigned locks,reads;
static int sdk_error;
static wifi_band_t band=WIFI_BAND_2G;
static wifi_band_mode_t band_mode=WIFI_BAND_MODE_2G_ONLY;
static wifi_ps_type_t ps=WIFI_PS_MIN_MODEM;
static wifi_phy_mode_t phy=WIFI_PHY_MODE_HT40;
static int64_t tsf=INT64_C(9007199254740991);
static uint16_t aid=17,seconds=300;
static struct {
    bool driver_owned,storage_configured,started,restart_required;
    struct {unsigned identity;} lifecycle,operation;
    const char *fault_stage,*cleanup_stage;
    esp32_mquickjs_wifi_radio_driver_state_t driver_state;
    wifi_mode_t effective_mode;
} s_radio={.driver_owned=true,.storage_configured=true,.started=true,
    .driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED,.effective_mode=WIFI_MODE_APSTA};
static void wifi_radio_operation_lock(void) {assert(!locks);++locks;}
static void wifi_radio_operation_unlock(void) {assert(locks==1);--locks;}
static int step_read(void) {assert(locks==1);++reads;return sdk_error;}
static int esp_wifi_get_band(wifi_band_t *out) {*out=band;return step_read();}
static int esp_wifi_get_band_mode(wifi_band_mode_t *out) {*out=band_mode;return step_read();}
static int esp_wifi_get_ps(wifi_ps_type_t *out) {*out=ps;return step_read();}
static int esp_wifi_get_max_tx_power(int8_t *out) {*out=73;return step_read();}
static int esp_wifi_sta_get_rssi(int *out) {*out=-63;return step_read();}
static int esp_wifi_sta_get_aid(uint16_t *out) {*out=aid;return step_read();}
static int esp_wifi_sta_get_negotiated_phymode(wifi_phy_mode_t *out) {*out=phy;return step_read();}
static int64_t esp_wifi_get_tsf_time(wifi_interface_t iface) {assert(iface==WIFI_IF_STA || iface==WIFI_IF_AP);step_read();return tsf;}
static int esp_wifi_get_inactive_time(wifi_interface_t iface,uint16_t *out) {assert(iface==WIFI_IF_STA || iface==WIFI_IF_AP);*out=seconds;return step_read();}
