
int main(void) {
    reset_fixture();esp32_mquickjs_wifi_radio_configuration_selection_t request={.mode_set=true,.mode=WIFI_MODE_APSTA,.start_only=true};
    esp32_mquickjs_wifi_radio_lifecycle_t token={0};bool running=true;
    assert(esp32_mquickjs_wifi_radio_begin_start_lifecycle(NULL,NULL,NULL,&request,&token,&running)==ESP_ERR_NOT_SUPPORTED);
    assert(!running && !token.identity && !mode_queries && s_radio.next_lifecycle_identity==11);
}
