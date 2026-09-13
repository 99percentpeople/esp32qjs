
static wifi_radio_wps_t *s_wps_radio;
static struct {uint32_t owners[3];} *s_sc_radio;
static uint32_t s_wps_fence_posted,s_wps_fence_seen;
#define WIFI_RADIO_DPP_PENDING false
#define WIFI_RADIO_WPS_PENDING (s_wps_radio != NULL)
#define WIFI_RADIO_SMARTCONFIG_PENDING (s_sc_radio != NULL)
#define WIFI_RADIO_EAP_PENDING false
#define WIFI_RADIO_WPS_FENCE_EVENT 8
#define ESP32QJS_WIFI_RADIO_CONTROL_EVENT 1
