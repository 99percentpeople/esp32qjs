
#define ESP_ERR_WIFI_NOT_ASSOC 3015
#define ESP32_MQUICKJS_WIFI_TWT_MAX_INDIVIDUAL 8
typedef struct {struct {unsigned identity;} token;esp32_mquickjs_wifi_radio_lease_t lease;bool native_retired;} wifi_radio_twt_individual_t;
typedef wifi_radio_twt_individual_t wifi_radio_twt_broadcast_t;
static wifi_radio_twt_individual_t individual[8],*s_twt_individual=individual;
static wifi_radio_twt_broadcast_t *broadcast[32],**s_twt_broadcast=broadcast;
