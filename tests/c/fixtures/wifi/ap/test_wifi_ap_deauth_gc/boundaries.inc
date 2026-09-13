
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE -2
static bool s_ap_cleanup_pending;
static int s_ap_lease,mode,sdk_calls;
static const char *s_ap_stage;
static int esp32_mquickjs_wifi_radio_ap_deauth(const void *lease,const uint8_t mac[6],
 bool *requested,bool *unknown,const char **stage){
 static const uint8_t expected[6]={2,0xab,0xcd,0,0xef,1};
 assert(lease==&s_ap_lease&&!memcmp(mac,expected,6));sdk_calls++;
 *requested=mode==0||mode==4;*unknown=mode==3;
 *stage=mode>=2?"deauth-request":NULL;return mode>=2?-77:0;}
