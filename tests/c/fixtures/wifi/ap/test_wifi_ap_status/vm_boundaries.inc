
#include "cutils.h"
#define ESP_OK 0
#define MACSTR "%02x:%02x:%02x:%02x:%02x:%02x"
#define MAC2STR(a) (a)[0],(a)[1],(a)[2],(a)[3],(a)[4],(a)[5]
typedef int esp_err_t;
typedef enum { WIFI_AUTH_OPEN,WIFI_AUTH_WPA_PSK,WIFI_AUTH_WPA2_PSK,WIFI_AUTH_WPA_WPA2_PSK,
    WIFI_AUTH_WPA3_PSK,WIFI_AUTH_WPA2_WPA3_PSK,WIFI_AUTH_OWE } wifi_auth_mode_t;
typedef struct {uint32_t generation,identity;int client;bool acquired;} esp32_mquickjs_wifi_radio_lease_t;
#define ESP32_MQUICKJS_WIFI_RADIO_CLIENT_RAW_TX 91
static struct {uint32_t generation,identity;} s_ap_raw_owner;
static void *s_ap_netif;
static struct { bool acquired; } s_ap_lease;
static bool s_ap_cleanup_pending;
static const char *s_ap_stage;
static int scenario,queries;
