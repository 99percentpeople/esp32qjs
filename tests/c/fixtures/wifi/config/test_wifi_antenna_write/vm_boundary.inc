
#define ESP_OK 0
static int native_writes,native_error,write_allocation_count;
static esp32_mquickjs_wifi_antenna_snapshot_t captured;
static int esp32_mquickjs_wifi_radio_write_antenna(bool gpio,const esp32_mquickjs_wifi_antenna_snapshot_t *input,esp32_mquickjs_wifi_radio_config_result_t *result){
 (void)gpio;native_writes++;write_allocation_count=calls;captured=*input;
 *result=(esp32_mquickjs_wifi_radio_config_result_t){.stage="antenna-phy-idle",.error=native_error};return native_error;
}
