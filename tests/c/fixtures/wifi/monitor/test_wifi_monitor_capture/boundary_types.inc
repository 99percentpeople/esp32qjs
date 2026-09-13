
typedef int esp_err_t,wifi_mode_t,wifi_second_chan_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_TIMEOUT 3
#define WIFI_MODE_STA 1
#define WIFI_PS_NONE 0
#define WIFI_SECOND_CHAN_NONE 0
#define ESP32_MQUICKJS_WIFI_RADIO_CLIENT_MONITOR 5
typedef struct { uint32_t generation,identity;int client;bool acquired; } esp32_mquickjs_wifi_radio_lease_t;
typedef struct { uint32_t generation,radio_lease_identity,identity;int client;bool acquired,framework_enabled; } esp32_mquickjs_wifi_radio_promiscuous_lease_t;
typedef struct { int unused; } esp32_mquickjs_wifi_promiscuous_subscriber_t;
typedef struct { uint8_t primary;int secondary;uint32_t generation;bool fixed,conflicted; } esp32_mquickjs_wifi_radio_channel_status_t;
typedef struct { uint32_t generation;bool power_save_available;int power_save,cleanup_error;const char *cleanup_stage; } esp32_mquickjs_wifi_radio_status_t;
typedef struct { bool closed,has_event;esp32_mquickjs_wifi_monitor_event_t event; } esp32_mquickjs_event_queue_t;
typedef struct { esp32_mquickjs_wifi_monitor_resources_t *resources;esp32_mquickjs_event_queue_t *queue; } esp32_mquickjs_wifi_monitor_queue_t;
