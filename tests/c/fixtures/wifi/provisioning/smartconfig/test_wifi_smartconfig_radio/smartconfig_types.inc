
#include <stdlib.h>
#define ESP_ERR_WIFI_NOT_CONNECT -20
#define ESP_ERR_WIFI_NOT_STARTED -21
#define ESP_ERR_INVALID_RESPONSE -22
#define ESP_ERR_NOT_FINISHED -23
typedef enum {SC_TYPE_ESPTOUCH,SC_TYPE_AIRKISS,SC_TYPE_ESPTOUCH_AIRKISS,SC_TYPE_ESPTOUCH_V2} smartconfig_type_t;
typedef struct {uint8_t ssid[32],password[64];bool bssid_set;uint8_t bssid[6];smartconfig_type_t type;uint8_t token,cellphone_ip[4];} smartconfig_event_got_ssid_pswd_t;
