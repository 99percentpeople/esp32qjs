
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NOT_FINISHED 0x10a
#define ESP_ERR_WIFI_NOT_INIT 0x3001
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
#define ESP_TIMER_TASK 0
typedef void *esp_timer_handle_t;
typedef struct {void (*callback)(void *);void *arg;int dispatch_method;const char *name;} esp_timer_create_args_t;
