
static bool esp32qjs_wps_ap_command_allowed(uint32_t id){return id==0;}
static bool current_task_is_wifi_task(void){return true;}
static uint32_t esp32qjs_wps_ap_result_identity(const void *p){(void)p;return 0;}
static void *esp32qjs_wps_ap_result_context(uint32_t id){(void)id;return NULL;}
#define ESP_OK 0
#define ESP_FAIL -1
#define MSG_ERROR 0
#define ESP_ERR_WIFI_MODE 0x301
#define ESP_ERR_WIFI_WPS_SM 0x302
#define ESP_ERR_WIFI_WPS_TYPE 0x303
#define ESP_ERR_WIFI_STATE 0x304
#define WPS_TYPE_DISABLE 0
#define WPS_TYPE_PBC 1
#define WPS_TYPE_PIN 2
#define WPS_STATUS_DISABLE 0
#define WPS_STATUS_SCANNING 1
#define WPS_STATUS_PENDING 2
typedef enum {WIFI_MODE_NULL,WIFI_MODE_AP,WIFI_MODE_APSTA} wifi_mode_t;
enum wps_owner{WPS_OWNER_NONE,WPS_OWNER_ENROLLEE,WPS_OWNER_REGISTRAR};
struct protocol{u8 dev_password[8];};
struct wps_sm{struct protocol *wps;};
static struct protocol protocol;
static struct wps_sm owner={&protocol},*gWpsSm=&owner;
static int mode_error,status_error,start_error,status_calls,native_status;
static int esp_wifi_get_mode(wifi_mode_t *p){if(mode_error)return mode_error;*p=WIFI_MODE_AP;return 0;}
static enum wps_owner wps_get_owner(void){return WPS_OWNER_REGISTRAR;}
static int wps_get_type(void){return WPS_TYPE_PBC;}
static int wps_get_status(void){return native_status;}
static int wps_set_status(int state){status_calls++;if(status_error)return status_error;native_status=state;return 0;}
static int esp_wifi_get_user_init_flag_internal(void){return 1;}
static void *hostapd_get_hapd_data(void){return &owner;}
static int hostapd_wps_button_pushed(void *p,const u8 *arg){(void)p;(void)arg;return start_error;}
static int hostapd_wps_add_pin(void *p,const u8 *arg){(void)p;(void)arg;return start_error;}
