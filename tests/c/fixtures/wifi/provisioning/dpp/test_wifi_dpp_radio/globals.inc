
static wifi_radio_dpp_t *s_dpp_radio;
static bool wifi_radio_dpp_stop_owner_locked(uint32_t identity);
#define WIFI_RADIO_DPP_PENDING (s_dpp_radio!=NULL)
#define WIFI_RADIO_WPS_PENDING false
#define WIFI_RADIO_SMARTCONFIG_PENDING false
#define WIFI_RADIO_EAP_PENDING false
