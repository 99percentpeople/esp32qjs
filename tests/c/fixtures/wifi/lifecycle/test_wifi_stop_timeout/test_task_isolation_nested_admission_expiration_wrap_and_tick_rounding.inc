
int main(void) {
    assert(esp32_mquickjs_wifi_wait_begin(0)==ESP_ERR_INVALID_ARG);
    assert(esp32_mquickjs_wifi_wait_begin(60001)==ESP_ERR_INVALID_ARG);
    ticks=UINT32_MAX-2;
    assert(esp32_mquickjs_wifi_wait_begin(61)==ESP_OK);
    assert(esp32_mquickjs_wifi_wait_remaining(1000)==7);
    ticks+=4;assert(esp32_mquickjs_wifi_wait_remaining(1000)==3);
    assert(esp32_mquickjs_wifi_wait_begin(500)==ESP_ERR_INVALID_STATE);
    current_task=(void *)2;
    assert(esp32_mquickjs_wifi_wait_remaining(1000)==1000);
    assert(esp32_mquickjs_wifi_wait_begin(100)==ESP_ERR_INVALID_STATE);
    esp32_mquickjs_wifi_wait_end();
    current_task=(void *)1;
    assert(esp32_mquickjs_wifi_wait_remaining(1000)==3);
    ticks+=3;assert(esp32_mquickjs_wifi_wait_remaining(1000)==0);
    esp32_mquickjs_wifi_wait_end();
    assert(esp32_mquickjs_wifi_wait_begin(1)==ESP_OK);
    assert(esp32_mquickjs_wifi_wait_remaining(1000)==1);
    esp32_mquickjs_wifi_wait_end();
    assert(esp32_mquickjs_wifi_wait_remaining(17)==17);
}
