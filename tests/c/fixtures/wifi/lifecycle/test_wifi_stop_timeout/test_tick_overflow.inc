
int main(void) {
    assert(esp32_mquickjs_wifi_wait_begin(INT32_MAX)==ESP_ERR_INVALID_ARG);
    assert(esp32_mquickjs_wifi_wait_remaining(17)==17);
    assert(esp32_mquickjs_wifi_wait_begin(214748364U)==ESP_OK);
    assert(esp32_mquickjs_wifi_wait_remaining(0)==2147483640U);
    esp32_mquickjs_wifi_wait_end();
    assert(esp32_mquickjs_wifi_wait_begin(214748365U)==ESP_ERR_INVALID_ARG);
    assert(esp32_mquickjs_wifi_wait_begin(60001)==ESP_OK);
    esp32_mquickjs_wifi_wait_end();
}
