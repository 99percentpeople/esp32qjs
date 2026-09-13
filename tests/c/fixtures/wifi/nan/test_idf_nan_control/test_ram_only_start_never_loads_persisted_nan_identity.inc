
#define CONFIG_ESP_WIFI_NAN_SECURITY 1
#define ESP_WIFI_NAN_NIK_LEN 16
#define ESP_LOGW(...) ((void)0)
static unsigned credential_loads,credential_erases,nik_saves;
static int esp_wifi_nan_erase_all_creds(void){++credential_erases;return 0;}
static int esp_wifi_nan_load_saved_creds(void*nik,bool*valid,void*peers,unsigned*count){
 (void)peers;++credential_loads;memset(nik,0x41,16);*valid=true;*count=1;return 0;}
static int os_get_random(void*nik,unsigned n){memset(nik,0x52,n);return 0;}
static int esp_wifi_nan_save_own_nik(void*nik){assert(nik);++nik_saves;return 0;}
