
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG -1
#define ESP_ERR_INVALID_STATE -2
#define WIFI_RADIO_MAX_LEASES 2
#define ESP32_MQUICKJS_WIFI_RADIO_UNINITIALIZED 0
#define ESP32_MQUICKJS_WIFI_RADIO_STOPPED 1
typedef int esp_err_t;
typedef struct { unsigned generation,identity; } esp32_mquickjs_wifi_radio_lifecycle_t;
static struct {
    esp32_mquickjs_wifi_radio_lifecycle_t lifecycle;
    struct { unsigned identity; } operation,leases[2];
    bool started,stop_required,promiscuous_claimed,restart_required;
    unsigned wake_locks;const char *fault_stage,*cleanup_stage;int driver_state;
} s_radio;
static int depth,initializes,init_error;
static void wifi_radio_operation_lock(void) { assert(!depth);depth=1; }
static void wifi_radio_operation_unlock(void) { assert(depth);depth=0; }
static int wifi_radio_initialize(void) { assert(depth);initializes++;return init_error; }
