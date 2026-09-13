
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 259
#define ESP_ERR_INVALID_RESPONSE 260
#define esp_test_rx_statistics rx_storage
#define esp_test_rx_error_occurs rx_storage[2]
#define esp_test_rx_mu_statistics multi_user
#define esp_test_tx_statistics_aci_bitmap aci_bitmap
static bool in_wifi_task,discard;
static bool current_task_is_wifi_task(void){return in_wifi_task;}
static int eloop_register_timeout_blocking(int (*handler)(void*,void*),void *a,void *b){
    assert(!in_wifi_task);if(discard)return -1;in_wifi_task=true;
    int result=handler(a,b);in_wifi_task=false;return result;
}
