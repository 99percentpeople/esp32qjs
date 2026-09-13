
int main(void) {
    esp32_mquickjs_future_driver_state_t state = {.connection_generation=1,.adapter_generation=1};
    s_ble.OWNER.active=true; s_ble.OWNER.generation=1; s_ble.OWNER.payloads=(void *)1;
    stop_result=1;
    timeout_branch(BLE_OP_OPERATION);
    DESTROY(&state);
    assert(freed==0 && roots==0 && orphan==1);
    assert(s_ble.OWNER.active);
    stop_result=0;
    timeout_branch(BLE_OP_OPERATION);
    assert(!s_ble.OWNER.active);
    DESTROY(&state);
    assert(roots==1);
    s_ble.OWNER.active=true;
    stop_result=BLE_HS_EALREADY;
    timeout_branch(BLE_OP_OPERATION);
    assert(!s_ble.OWNER.active);
}
