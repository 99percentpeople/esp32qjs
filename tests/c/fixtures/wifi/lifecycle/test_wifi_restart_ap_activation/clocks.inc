
#include <stdbool.h>
#include <stdint.h>
#define ESP_ERR_TIMEOUT 78
#define WIFI_RADIO_START_EVENT_TIMEOUT_MS 8
#define pdMS_TO_TICKS(v) (v)
typedef unsigned TickType_t;
typedef struct {int unused;} esp32_mquickjs_runtime_t;
typedef struct {int unused;} esp32_mquickjs_native_wait_t;
static unsigned ticks,waits,delays,wait_limit=8,station_activations;
static bool csa_pending,cancel_wait,never_settle,station_channel_drift;
static uint8_t csa_primary;
static int csa_secondary;
