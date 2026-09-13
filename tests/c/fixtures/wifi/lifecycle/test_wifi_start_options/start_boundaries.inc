
#define WIFI_RADIO_NAN_PENDING false
#define WIFI_RADIO_MESH_PENDING false
static int mode_queries,mode_error;
static int esp_wifi_get_mode(wifi_mode_t *mode) { assert(locked && !critical);mode_queries++;*mode=s_radio.effective_mode;return mode_error; }
