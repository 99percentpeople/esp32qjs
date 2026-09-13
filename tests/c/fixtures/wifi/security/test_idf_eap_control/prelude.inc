
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef int esp_err_t;
#define CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT 1
#define CONFIG_ESP_WIFI_ENABLE_ROAMING_APP 1
#define EAP_PEER_METHOD 1
#define TRUE 1
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM -2
#define ESP_ERR_INVALID_STATE -3
#define ESP_ERR_INVALID_ARG -4
#define wpa_printf(...) ((void)0)
typedef enum {WPA2_STATE_DISABLED,WPA2_STATE_ENABLED} wpa2_state_t;
struct eap_sm {int unused;};
static struct eap_sm *gEapSm;
static void *s_wpa2_data_lock;
static bool g_wpa_config_changed;
struct wpa_sm {int (*wpa_sm_eap_disable)(void);int (*wpa_sm_wps_disable)(void);};
static struct wpa_sm gWpaSm;
