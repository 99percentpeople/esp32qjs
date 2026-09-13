
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define CONFIG_ESP_WIFI_WAPI_PSK 1
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 259
#define ESP_ERR_NO_MEM 257
typedef int esp_err_t,JSContext,JSValue,portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
static int lock_depth;
#define portENTER_CRITICAL(p) do{(void)(p);assert(!lock_depth);++lock_depth;}while(0)
#define portEXIT_CRITICAL(p) do{(void)(p);assert(lock_depth==1);--lock_depth;}while(0)
