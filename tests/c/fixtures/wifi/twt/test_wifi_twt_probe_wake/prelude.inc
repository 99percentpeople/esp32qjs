
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define CONFIG_SOC_WIFI_HE_SUPPORT 1
#define CONFIG_IDF_TARGET_ESP32C5 1
#define DRAM_ATTR
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 0x103
typedef int esp_err_t;
typedef unsigned portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
static unsigned locked;
#define portENTER_CRITICAL_SAFE(lock) do {assert(!locked);locked=1;} while(0)
#define portEXIT_CRITICAL_SAFE(lock) do {assert(locked==1);locked=0;} while(0)
