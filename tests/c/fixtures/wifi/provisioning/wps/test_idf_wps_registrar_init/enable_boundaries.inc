
static bool esp32qjs_wps_ap_command_allowed(uint32_t id){return id==0;}
static bool current_task_is_wifi_task(void){return true;}
static struct hostapd_data enable_hapd;
static void *hostapd_get_hapd_data(void){return &enable_hapd;}
static int esp32qjs_wps_ap_result_bind(void *p){return p?ESP_OK:ESP_ERR_INVALID_STATE;}
static void esp32qjs_wps_ap_init_failed(struct hostapd_data *h,int error){assert(h==&enable_hapd && error);}
static uint32_t esp32qjs_wps_ap_result_identity(const void *p){(void)p;return 0;}
static void *esp32qjs_wps_ap_result_context(uint32_t id){(void)id;return NULL;}
static bool esp32qjs_wps_ap_result_can_bind(void){return true;}
typedef int wifi_mode_t;
#define WIFI_MODE_NULL 0
#define WIFI_MODE_AP 2
#define WIFI_MODE_APSTA 3
#define WIFI_AUTH_OPEN 0
#define WPS_TYPE_DISABLE 0
#define WPS_TYPE_PBC 1
#define WPS_TYPE_PIN 2
#define WPS_STATUS_DISABLE 0
#define ESP_ERR_WIFI_STATE 41
#define ESP_ERR_WIFI_MODE 42
#define ESP_ERR_WIFI_WPS_TYPE 43
enum wps_owner {WPS_OWNER_NONE,WPS_OWNER_ENROLLEE,WPS_OWNER_REGISTRAR};
typedef struct {int wps_type;} esp_wps_config_t;
static int mode_error,factory_error,init_error,initialized;
static int esp_wifi_get_user_init_flag_internal(void){return 1;}
static int esp_wifi_get_mode(wifi_mode_t *mode){*mode=WIFI_MODE_AP;return mode_error;}
static int esp_wifi_ap_get_prof_authmode_internal(void){return 2;}
static enum wps_owner wps_get_owner(void){return WPS_OWNER_NONE;}
static void wps_set_owner(enum wps_owner owner){assert(owner==WPS_OWNER_REGISTRAR);}
static int wps_set_factory_info(const esp_wps_config_t *config){(void)config;return factory_error;}
static int wps_set_type(int type){(void)type;return 0;}
static int wps_set_status(int status){(void)status;return 0;}
static int wifi_ap_wps_init(const esp_wps_config_t *config){(void)config;initialized++;return init_error;}
