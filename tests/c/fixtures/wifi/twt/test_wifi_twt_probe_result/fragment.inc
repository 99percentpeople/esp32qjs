
#define ESP_ERR_NOT_FINISHED 31
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
typedef void *TaskHandle_t;
static TaskHandle_t xTaskGetCurrentTaskHandle(void){return (void *)31;}
static void *heap_caps_calloc(size_t n,size_t size,unsigned caps){(void)n;(void)size;(void)caps;assert(0);return NULL;}
static void esp32_mquickjs_wifi_twt_tx_cleanup_native(void){assert(0);}
static esp_err_t esp32_mquickjs_wifi_twt_tx_information_quiescent_native(uint32_t id){(void)id;assert(0);return 0;}
void esp32_mquickjs_wifi_twt_information_timer_snapshot(esp32_mquickjs_wifi_twt_information_timer_snapshot_t *out){
    *out=(esp32_mquickjs_wifi_twt_information_timer_snapshot_t){0};
}
