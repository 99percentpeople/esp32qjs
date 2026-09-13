
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#include <string.h>
#undef NULL
#define NULL 0
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define CONFIG_ESP_WIFI_DPP_SUPPORT 1
#define DRAM_ATTR
#define IRAM_ATTR
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERR_NO_MEM 2
#define ESP_ERR_INVALID_ARG 3
#define ESP_TIMER_TASK 0
typedef int esp_err_t;
typedef uint32_t esp_timer_handle_t;
typedef void ETSTimerFunc(void *);
typedef struct {uint32_t next,timer_expire,period,fn,timer_arg;} ETSTimer;
typedef struct {void(*callback)(void*);void*arg;int dispatch_method;const char*name;}esp_timer_create_args_t;
typedef unsigned portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
static unsigned critical;
#define portENTER_CRITICAL_SAFE(m) do{(void)m;assert(!critical);critical++;}while(0)
#define portEXIT_CRITICAL_SAFE(m) do{(void)m;assert(critical==1);critical--;}while(0)
