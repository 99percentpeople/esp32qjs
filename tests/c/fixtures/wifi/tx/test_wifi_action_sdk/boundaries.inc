
#define ACTION_SDK_FENCE_TYPE ((wifi_action_tx_t)INT32_MAX)
#define ACTION_SDK_QUIESCENT_TYPE ((wifi_action_tx_t)(INT32_MAX - 1))
#define ESP_ERR_TIMEOUT 99
static uint8_t g_offchan_ctx[28];
static unsigned delegates;static bool ioctl_task;
static esp_err_t __real_wifi_action_tx_process(void *message) {
    assert(message && ioctl_task);++delegates;return 77;
}
static esp_err_t __real_wifi_roc_process(void *message) {
    assert(message && ioctl_task);++delegates;return 88;
}
static int other_receive(uint8_t *a,uint8_t *b,size_t c,uint8_t d){(void)a;(void)b;(void)c;(void)d;return 0;}
