
#include "esp32_mquickjs_native_pool.h"
#include "esp32_mquickjs_espnow_tx_queue.h"
#define MALLOC_CAP_INTERNAL 2
#define ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL 0
#define ESP32_MQUICKJS_MEMORY_EXTERNAL 1
#define ESP32_MQUICKJS_WIRELESS_TX_READY 0
#define ESPNOW_TX_TASK_STACK_BYTES 1024
#define CONFIG_ESP32_MQUICKJS_ESPNOW_MAX_PEERS 1
#define ESPNOW_LIFECYCLE_CLOSED 0
#define ESPNOW_LIFECYCLE_OPENING 1
#define ESPNOW_LIFECYCLE_CLOSING 2
#define ESP32_MQUICKJS_EVENT_QUEUE_DROP_NEWEST 0
#define ESP32_MQUICKJS_WIFI_RADIO_CLIENT_ESPNOW 1
#define WIFI_MODE_STA 1
#define ESP_NOW_KEY_LEN 16
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERR_TIMEOUT 2
typedef int esp_err_t;
typedef struct {unsigned identity;} esp32_mquickjs_wifi_interval_token_t;
typedef struct { bool acquired; } esp32_mquickjs_wifi_radio_lease_t;
#define ESP_WIFI_CONNECTIONLESS_INTERVAL_DEFAULT_MODE 0
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
#define ESP_LOGE(...) ((void)0)
typedef int portMUX_TYPE,esp32_mquickjs_event_queue_t;
typedef void *TaskHandle_t;
typedef struct { int unused; } espnow_receive_event_t;
typedef struct { uint8_t *payload; } espnow_rx_slot_t;
typedef struct { uint8_t address[6];unsigned length; } espnow_tx_packet_t;
typedef struct { uint8_t lmk[16]; } espnow_peer_slot_t;
typedef struct { bool configured; } espnow_peer_rate_config_t;
typedef struct {
    JSContext *ctx;void *runtime;int token,err;int64_t completed_at_us;
    atomic_bool completed;bool transferred,event_queue_rooted,has_pmk,power_save_enabled,channel_fixed;
    uint8_t pmk[16];JSGCRef event_queue_ref;unsigned generation,send_timeout_ms,channel,wake_window_ms,wake_interval_ms,receive_capacity,max_payload_bytes,tx_queue_capacity;
    espnow_peer_rate_config_t broadcast_rate_config;esp32_mquickjs_espnow_tx_overflow_t tx_overflow;
} esp32_mquickjs_future_driver_state_t;
