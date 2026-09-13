
#define CONFIG_WPS_REGISTRAR 1
#define ESP_ERR_WIFI_MODE 0x3006
#define WPS_OWNER_ENROLLEE 1
enum wps_owner{OWNER_UNUSED};
static void *s_wps_native;
static int wps_set_type(int type){(void)type;assert(false);return 0;}
static int wifi_wps_disable_internal(void *a,void *b){(void)a;(void)b;assert(false);return 0;}
