
#define ESP_ERR_NOT_FINISHED 0x10c
#define WPS_TYPE_DISABLE 0
#define WPS_TYPE_PBC 1
#define WPS_TYPE_PIN 2
#define WPS_STATUS_DISABLE 0
#define WPS_OWNER_REGISTRAR 2
typedef struct {int wps_type;} esp_wps_config_t;
struct hostapd_data{int value;};
static struct hostapd_data hapd;
static int enables,starts,deinits,factory_calls,enable_error,deinit_error,factory_error;
static int wps_get_type(void){return WPS_TYPE_DISABLE;}
static int wps_get_status(void){return WPS_STATUS_DISABLE;}
static bool is_dpp_enabled(void){return false;}
static void wps_set_owner(int owner){native_owner=owner;}
static struct hostapd_data *hostapd_get_hapd_data(void){return &hapd;}
