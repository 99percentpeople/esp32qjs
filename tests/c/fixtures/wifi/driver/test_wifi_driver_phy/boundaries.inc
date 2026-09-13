
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NOT_SUPPORTED 3
#define ESP_ERR_WIFI_NOT_INIT 4
#define ESP_ERR_INVALID_RESPONSE 5
static unsigned locks, reads;
static int sdk_error;
static uint16_t bits=WIFI_PROTOCOL_11B|WIFI_PROTOCOL_11G;
static wifi_bandwidth_t width=WIFI_BW40;
static wifi_band_mode_t band_mode=WIFI_BAND_MODE_2G_ONLY;
static struct {
    bool driver_owned,storage_configured,restart_required;
    struct {unsigned identity;} lifecycle,operation;
    const char *fault_stage,*cleanup_stage;
    esp32_mquickjs_wifi_radio_driver_state_t driver_state;
    wifi_mode_t effective_mode;
} s_radio={.driver_owned=true,.storage_configured=true,
    .driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED,.effective_mode=WIFI_MODE_APSTA};
static void wifi_radio_operation_lock(void) {assert(!locks);++locks;}
static void wifi_radio_operation_unlock(void) {assert(locks==1);--locks;}
static int boundary(wifi_interface_t iface) {assert(locks==1 && (iface==WIFI_IF_STA || iface==WIFI_IF_AP));++reads;return sdk_error;}
static int esp_wifi_get_protocol(wifi_interface_t iface,uint8_t *out) {*out=bits;int err=boundary(iface);return err ? err : band_mode==WIFI_BAND_MODE_AUTO ? ESP_ERR_NOT_SUPPORTED : ESP_OK;}
static int esp_wifi_get_bandwidth(wifi_interface_t iface,wifi_bandwidth_t *out) {*out=width;int err=boundary(iface);return err ? err : band_mode==WIFI_BAND_MODE_AUTO ? ESP_ERR_NOT_SUPPORTED : ESP_OK;}
#if CONFIG_SOC_WIFI_SUPPORT_5G
static int esp_wifi_get_band_mode(wifi_band_mode_t *out) {assert(locks==1);++reads;*out=band_mode;return sdk_error;}
static int esp_wifi_get_protocols(wifi_interface_t iface,wifi_protocols_t *out) {
    *out=(wifi_protocols_t){.ghz_2g=0xffff,.ghz_5g=0xffff};
    if(band_mode!=WIFI_BAND_MODE_5G_ONLY)out->ghz_2g=bits;
    if(band_mode!=WIFI_BAND_MODE_2G_ONLY)out->ghz_5g=WIFI_PROTOCOL_11A|WIFI_PROTOCOL_11N;
    return boundary(iface);
}
static int esp_wifi_get_bandwidths(wifi_interface_t iface,wifi_bandwidths_t *out) {
    *out=(wifi_bandwidths_t){.ghz_2g=99,.ghz_5g=99};
    if(band_mode!=WIFI_BAND_MODE_5G_ONLY)out->ghz_2g=width;
    if(band_mode!=WIFI_BAND_MODE_2G_ONLY)out->ghz_5g=WIFI_BW20;
    return boundary(iface);
}
#endif
