
void esp32_mquickjs_wifi_twt_setup_results_connection_closed_native(void);
void esp32_mquickjs_wifi_twt_setup_timers_connection_closed_native(void);
void esp32_mquickjs_wifi_twt_information_timers_connection_closed_native(void);
#define ESP_FAIL (-1)
#define WIFI_EVENT_BTWT_SETUP 33
#define WIFI_EVENT_BTWT_TEARDOWN 34
#define WIFI_EVENT ((const char *)19)
#define ESP_EVENT_DECLARE_BASE(name)
#define ESP32QJS_WIFI_RADIO_CONTROL_EVENT ((const char *)20)
typedef void *TaskHandle_t;
static TaskHandle_t current_task=(void *)1;
static TaskHandle_t xTaskGetCurrentTaskHandle(void){return current_task;}
static esp_err_t esp_event_post(const char *,int32_t,const void *,size_t,unsigned);
