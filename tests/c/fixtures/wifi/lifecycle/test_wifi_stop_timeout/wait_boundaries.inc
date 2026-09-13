
#define CONFIG_ESP32_MQUICKJS_WIFI_RADIO 1
#define configTICK_RATE_HZ 1000
#define portMAX_DELAY UINT32_MAX
typedef void *TaskHandle_t;
static TaskHandle_t current_task=(void *)1;
static TaskHandle_t xTaskGetCurrentTaskHandle(void) { return current_task; }
