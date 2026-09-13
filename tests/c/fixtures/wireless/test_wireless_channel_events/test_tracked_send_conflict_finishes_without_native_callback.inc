
int main(void) {
    uint8_t bytes[4]={0};esp32_mquickjs_future_driver_state_t state={.payload=bytes,.payload_length=4};
    espnow_session_t session={.pending_tracked_send=&state};
    assert(espnow_start_tracked_send(&session));
    assert(!sends && wakes==1 && atomic_load(&state.completed));
    assert(state.err==ESP_ERR_ESPNOW_CHAN && !atomic_load(&session.active_send));
    assert(atomic_load(&session.send_failures)==1 && !atomic_load(&session.pending_tracked_send));
}
