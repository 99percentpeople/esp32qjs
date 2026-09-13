
#define ESP32_MQUICKJS_WIFI_DEFAULT_TIMEOUT_MS 15000
#define ESP32_MQUICKJS_WIFI_SSID_MAX_LEN 32
#define ESP32_MQUICKJS_WIFI_PASSWORD_MAX_LEN 64
#define WIFI_FAST_SCAN 0
#define WIFI_CONNECT_AP_BY_SIGNAL 0
#define WIFI_CONNECT_AP_BY_SECURITY 1
#define WIFI_AUTH_WEP 1
#define WIFI_AUTH_WPA_PSK 2
#define WIFI_AUTH_WPA2_PSK 3
#define WIFI_AUTH_WPA_WPA2_PSK 4
#define WIFI_AUTH_ENTERPRISE 5
#define WIFI_AUTH_WPA_ENTERPRISE 16
#define WIFI_IF_STA 0
#define WIFI_IF_AP 1
typedef int wifi_interface_t;
typedef int wifi_auth_mode_t;
typedef union { wifi_sta_config_t sta;struct {wifi_auth_mode_t authmode;bool wpa3_compatible_mode;} ap; } wifi_config_t;
/* SDK channel validation boundary; not exercised by this security/PHY suite. */
static unsigned esp32_mquickjs_wifi_radio_5ghz_channel_bit(uint8_t channel) {
    assert(!"unexpected channel validation");return 0;
}
