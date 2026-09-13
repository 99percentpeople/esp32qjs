
int main(void) {
    uint8_t bytes[4]={0};espnow_tx_packet_t packet={.length=4};
    espnow_session_t session={.tx_payloads=bytes,.max_payload_bytes=4,.tx_packets=&packet};
    assert(espnow_start_queued_send(&session));
    assert(sends==0 && atomic_load(&session.queued_send_completed));
    assert(atomic_load(&session.queued_send_error)==ESP_ERR_ESPNOW_CHAN);
}
