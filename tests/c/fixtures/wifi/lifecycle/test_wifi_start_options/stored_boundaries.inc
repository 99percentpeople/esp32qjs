
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT 1
#define WIFI_RADIO_MAX_LEASES 2
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG -1
#define ESP_ERR_INVALID_STATE -2
#define ESP32_MQUICKJS_WIFI_RADIO_UNINITIALIZED 0
#define ESP32_MQUICKJS_WIFI_RADIO_STOPPED 2
#define ESP32_MQUICKJS_WIFI_RADIO_FAULTED 3
#define ESP32_MQUICKJS_WIFI_RADIO_CLEANUP_PENDING 4
#define WIFI_IF_AP 1
typedef int esp_err_t;
typedef struct {unsigned generation,identity;} esp32_mquickjs_wifi_radio_lifecycle_t;
typedef int wifi_sae_pwe_method_t;
#define WIFI_AUTH_WPA3_PSK 6
#define WIFI_AUTH_WPA2_WPA3_PSK 7
#define WPA3_SAE_PWE_UNSPECIFIED 0
#define WPA3_SAE_PWE_HUNT_AND_PECK 1
#define WPA3_SAE_PWE_HASH_TO_ELEMENT 2
#define WPA3_SAE_PWE_BOTH 3
typedef struct {struct {uint8_t channel; int authmode,sae_pwe_h2e; bool wpa3_compatible_mode;} ap;uint8_t secret[64];} wifi_config_t;
static struct {
    esp32_mquickjs_wifi_radio_lifecycle_t lifecycle;
    bool started,stop_required,promiscuous_claimed,restart_required,driver_owned,storage_configured;
    struct {unsigned identity;} operation,leases[2];
    unsigned wake_locks;int driver_state;const char *fault_stage,*cleanup_stage;
} s_radio;
static int depth,gets,validations,regulations;
static bool getter_error,validation_error,regulatory_error;
static void wifi_radio_operation_lock(void) {assert(!depth);depth=1;}
static void wifi_radio_operation_unlock(void) {assert(depth);depth=0;}
static int esp_wifi_get_config(int iface,wifi_config_t *config) {
    assert(depth && iface==WIFI_IF_AP);gets++;memset(config,0x55,sizeof(*config));config->ap.channel=6;
    config->ap.authmode=3;config->ap.sae_pwe_h2e=WPA3_SAE_PWE_BOTH;config->ap.wpa3_compatible_mode=false;
    return getter_error?-7:0;
}
static int esp32_mquickjs_wifi_radio_validate_ap_config(const wifi_config_t *config) {
    assert(depth && config->secret[0]==0x55);validations++;return validation_error?-8:0;
}
static int wifi_radio_validate_regulatory_channel(uint8_t channel) {assert(depth && channel==6);regulations++;return regulatory_error?-9:0;}
