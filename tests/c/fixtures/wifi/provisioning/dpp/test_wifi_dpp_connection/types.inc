
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define CONFIG_ESP_WIFI_DPP_SUPPORT 1
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_NOT_SUPPORTED 2
typedef int esp_err_t;
enum {ESP_DPP_AKM_UNKNOWN,ESP_DPP_AKM_DPP,ESP_DPP_AKM_PSK,ESP_DPP_AKM_SAE,
 ESP_DPP_AKM_PSK_SAE,ESP_DPP_AKM_SAE_DPP,ESP_DPP_AKM_PSK_SAE_DPP};
enum {WIFI_AUTH_DPP=20,WIFI_AUTH_WPA3_PSK=30,WIFI_AUTH_WPA2_PSK=40,WPA3_SAE_PWE_BOTH=3};
typedef struct {struct {uint8_t ssid[32],password[64],channel;struct {int authmode;}threshold;
 struct {bool capable,required;}pmf_cfg;int sae_pwe_h2e;}sta;}wifi_config_t;
