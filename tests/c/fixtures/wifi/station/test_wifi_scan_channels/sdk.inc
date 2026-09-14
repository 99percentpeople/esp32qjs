typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NOT_SUPPORTED 0x106
#define ESP_ERR_NOT_ALLOWED 0x10d
#define ESP32_MQUICKJS_WIFI_RADIO_OPERATION_SCAN 1
typedef struct {
    unsigned identity, generation, lease_identity, kind;
} esp32_mquickjs_wifi_radio_operation_t;
static struct {
    esp32_mquickjs_wifi_radio_operation_t operation;
    bool started;
    struct { unsigned identity; } lifecycle;
    const char *fault_stage;
} s_radio = { .operation = {1, 2, 3, ESP32_MQUICKJS_WIFI_RADIO_OPERATION_SCAN}, .started = true };
static wifi_country_t country = { .cc = "01", .schan = 1, .nchan = 11, .policy = WIFI_COUNTRY_POLICY_AUTO };
static wifi_band_mode_t band = WIFI_BAND_MODE_AUTO;
static int lock_depth, country_error, band_error;
static void wifi_radio_operation_lock(void) { assert(lock_depth++ == 0); }
static void wifi_radio_operation_unlock(void) { assert(--lock_depth == 0); }
static esp_err_t esp_wifi_get_country(wifi_country_t *out) {
    assert(lock_depth == 1); *out = country; return country_error;
}
static esp_err_t esp_wifi_get_band_mode(wifi_band_mode_t *out) {
    assert(lock_depth == 1); *out = band; return band_error;
}
