
int main(void) {
    esp32_mquickjs_future_driver_state_t scan={.kind=WIFI_FUTURE_SCAN};
    assert(wifi_future_start(NULL,NULL,1,&scan));
    assert(admissions==1 && operation_releases==1 && !s_wifi_state.radio_operation.identity);
}
