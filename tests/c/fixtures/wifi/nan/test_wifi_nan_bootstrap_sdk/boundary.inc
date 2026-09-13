
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#define ESP_LOGI(...) ((void)0)
#define MACADDR_COPY(d,s) memcpy(d,s,6)
#define WIFI_NAN_NPBA_TYPE_REQUEST 1
#define WIFI_NAN_NPBA_TYPE_RESPONSE 2
#define WIFI_NAN_PAIRING_STATUS_ACCEPTED 0
#define WIFI_NAN_PAIRING_STATUS_REJECTED 1
#define WIFI_NAN_PAIRING_STATUS_COMEBACK 2
#define WIFI_NAN_BOOTSTRAP_PIN_CODE_DISPLAY 2
#define WIFI_NAN_BOOTSTRAP_PIN_CODE_KEYPAD 32
#define ESP32_MQUICKJS_NAN_BOOTSTRAP_REQUEST 1
#define ESP32_MQUICKJS_NAN_BOOTSTRAP_RESPONSE 2
#define ESP32_MQUICKJS_NAN_BOOTSTRAP_ACCEPTED 0
#define ESP32_MQUICKJS_NAN_BOOTSTRAP_REJECTED 1
#define ESP32_MQUICKJS_NAN_BOOTSTRAP_COMEBACK 2
#define ESP32_MQUICKJS_NAN_SDK_BOOTSTRAP 9
struct nan_cb_npba_t {uint8_t type,status;uint16_t methods;};
typedef struct {uint8_t type,status,peer_svc_id,own_svc_id,peer_nmi[6],reason_code;uint16_t methods;} wifi_nan_bootstrap_event_t;
typedef struct {uint8_t type,peer_service_id,status;uint16_t methods;} esp32_mquickjs_wifi_nan_sdk_bootstrap_t;
typedef struct {int kind;uint8_t service_id,peer[6];const void*data;size_t size;} esp32_mquickjs_wifi_nan_sdk_notice_t;
static unsigned notices;static uint8_t last_status;
static void (*s_bootstrap_cb)(const wifi_nan_bootstrap_event_t*);
static void esp32_mquickjs_wifi_nan_sdk_notice(const esp32_mquickjs_wifi_nan_sdk_notice_t*n){
 assert(n->kind==ESP32_MQUICKJS_NAN_SDK_BOOTSTRAP&&n->service_id==7&&n->peer[0]==2&&n->size==sizeof(esp32_mquickjs_wifi_nan_sdk_bootstrap_t));
 const esp32_mquickjs_wifi_nan_sdk_bootstrap_t*data=n->data;assert(data->peer_service_id==9);last_status=data->status;++notices;
}
