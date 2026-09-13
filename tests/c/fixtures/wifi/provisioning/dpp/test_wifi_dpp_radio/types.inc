
#include <stdlib.h>
#define ESP_ERR_WIFI_NOT_CONNECT -20
#define ESP_ERR_WIFI_NOT_STARTED -21
#define ESP_ERR_WIFI_CONN -24
#define ESP_ERR_INVALID_RESPONSE -22
#define ESP_ERR_NOT_FINISHED -23
#define RADIO_EVENTS_IDLE 0
#define RADIO_EVENTS_START 1
#define RADIO_EVENTS_STOP 2
#define AP_STOP_IDLE 0
typedef struct {uint8_t secret[32];} esp_dpp_config_data_t;
typedef struct {int unused;} wifi_event_action_tx_status_t;
