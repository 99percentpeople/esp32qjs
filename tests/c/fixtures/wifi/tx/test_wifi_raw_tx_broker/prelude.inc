
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NO_MEM 3
#define ESP_ERR_TIMEOUT 4
#define MALLOC_CAP_8BIT 1
typedef pthread_mutex_t portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED PTHREAD_MUTEX_INITIALIZER
static _Thread_local unsigned critical_depth;
#define portENTER_CRITICAL(lock) do { assert(!pthread_mutex_lock(lock)); ++critical_depth; } while(0)
#define portEXIT_CRITICAL(lock) do { --critical_depth; assert(!pthread_mutex_unlock(lock)); } while(0)
