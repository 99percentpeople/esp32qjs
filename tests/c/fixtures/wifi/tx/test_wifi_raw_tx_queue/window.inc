int main(void) {
    esp32_mquickjs_wifi_raw_tx_queue_t q = {0};
    esp32_mquickjs_wifi_raw_tx_queue_slot_t slots[4];
    uint8_t bytes[5][32] = {{0}};
    esp32_mquickjs_wifi_raw_tx_payload_t frames[4], removed[4] = {0}, retired = {0}, borrowed = {0};
    esp32_mquickjs_wifi_raw_tx_admission_t admission;
    esp32_mquickjs_wifi_raw_tx_ticket_t tickets[3] = {0}, stale;
    esp32_mquickjs_wifi_raw_tx_flush_token_t flush = {0};
    esp32_mquickjs_wifi_raw_tx_flush_status_t fence;
    assert(esp32_mquickjs_wifi_raw_tx_queue_init(&q, slots, 4, 7));
    assert(esp32_mquickjs_wifi_raw_tx_queue_limits(&q, 96, 3));
    for (unsigned i = 0; i < 3; ++i) frames[i] = (esp32_mquickjs_wifi_raw_tx_payload_t){bytes[i],32};
    assert(esp32_mquickjs_wifi_raw_tx_queue_admit(&q,frames,3,0,removed,4,&admission)==0);
    assert(q.used_bytes==96 && q.high_water_bytes==96);
    assert(!esp32_mquickjs_wifi_raw_tx_queue_writable(&q,1,32));
    assert(esp32_mquickjs_wifi_raw_tx_queue_flush_begin(&q,&flush)==0);
    for (unsigned i=0;i<3;++i) {
        assert(esp32_mquickjs_wifi_raw_tx_queue_take(&q,&tickets[i],&borrowed)); borrowed=(esp32_mquickjs_wifi_raw_tx_payload_t){0};
        assert(esp32_mquickjs_wifi_raw_tx_queue_accept(&q,&tickets[i]));
    }
    assert(q.in_flight==3 && q.queued==0);
    stale=tickets[1];
    assert(esp32_mquickjs_wifi_raw_tx_queue_finish(&q,&tickets[1],ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_SUCCESS,&retired));
    retired=(esp32_mquickjs_wifi_raw_tx_payload_t){0};
    assert(!esp32_mquickjs_wifi_raw_tx_queue_finish(&q,&stale,ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_SUCCESS,&retired));
    assert(q.used_bytes==64 && esp32_mquickjs_wifi_raw_tx_queue_writable(&q,1,32));
    frames[0]=(esp32_mquickjs_wifi_raw_tx_payload_t){bytes[3],32};
    assert(esp32_mquickjs_wifi_raw_tx_queue_admit(&q,frames,1,0,removed,4,&admission)==0);
    assert(esp32_mquickjs_wifi_raw_tx_queue_flush_status(&q,&flush,&fence) && fence.pending==2);
    for (unsigned i=0;i<3;i+=2) {
        assert(esp32_mquickjs_wifi_raw_tx_queue_finish(&q,&tickets[i],ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_SUCCESS,&retired));
        retired=(esp32_mquickjs_wifi_raw_tx_payload_t){0};
    }
    assert(esp32_mquickjs_wifi_raw_tx_queue_flush_status(&q,&flush,&fence) && fence.pending==0 && fence.totals.settled==3);
    assert(q.used_bytes==32 && q.queued==1);
    uint16_t count=0;
    assert(esp32_mquickjs_wifi_raw_tx_queue_close(&q,removed,4,&count) && count==1 && q.used_bytes==0);
    assert(!esp32_mquickjs_wifi_raw_tx_queue_writable(&q,1,0));
    assert(esp32_mquickjs_wifi_raw_tx_queue_flush_release(&q,&flush));
    assert(esp32_mquickjs_wifi_raw_tx_queue_deinit(&q));
    return 0;
}
