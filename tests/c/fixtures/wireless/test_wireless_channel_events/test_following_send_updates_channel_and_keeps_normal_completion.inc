
int main(void) {
    conflict=false;fixed=false;
    uint8_t bytes[4]={0};espnow_tx_packet_t packet={.length=4};
    espnow_session_t session={.tx_payloads=bytes,.max_payload_bytes=4,.tx_packets=&packet,.channel=6,.channel_generation=1};
    assert(espnow_start_queued_send(&session));
    assert(sends==1 && !atomic_load(&session.queued_send_completed));
    assert(atomic_load(&session.channel)==11 && atomic_load(&session.channel_generation)==2);
    assert(atomic_load(&session.sent_packets)==1 && !atomic_load(&session.send_failures));
}
