
int main(void) {
    esp32_mquickjs_future_driver_state_t state = {.connection_generation=1,.adapter_generation=1};
    s_ble.scanner.active=true; s_ble.scanner.generation=1; s_ble.scanner.payloads=(void *)1;
    ble_scan_destroy(&state);
    assert(freed==0 && roots==0 && orphan==1);
    assert(s_ble.scanner.payloads==(void *)1);
}
