
#define ESP_ERR_NO_MEM -8
#define ESP_ERR_NOT_FINISHED -9
static struct {unsigned identity;} s_wifi_lifecycle;
esp_err_t esp32_mquickjs_wifi_drain_scan(void) {return ESP_OK;}
static int esp32_mquickjs_wifi_start_scan(const wifi_scan_config_t *c,uint32_t generation) {
    (void)c;(void)generation;assert(0);return ESP_FAIL;
}
