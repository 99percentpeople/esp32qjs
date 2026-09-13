int main(void) {
    esp32_mquickjs_wifi_raw_tx_queue_t q={0};
    esp32_mquickjs_wifi_raw_tx_queue_slot_t slots[6];
    uint8_t bytes[8][96]={{0}};
    esp32_mquickjs_wifi_raw_tx_payload_t frames[2],removed[6]={{0}},borrowed={0},retired={0};
    esp32_mquickjs_wifi_raw_tx_admission_t admission;
    esp32_mquickjs_wifi_raw_tx_ticket_t tickets[3]={{0}};
    assert(esp32_mquickjs_wifi_raw_tx_queue_init(&q,slots,6,7));
    assert(esp32_mquickjs_wifi_raw_tx_queue_limits(&q,192,3));
    for(unsigned n=0;n<3;++n) {
        for(unsigned i=0;i<2;++i)frames[i]=(esp32_mquickjs_wifi_raw_tx_payload_t){bytes[n*2+i],32};
        assert(!esp32_mquickjs_wifi_raw_tx_queue_admit(&q,frames,2,0,removed,6,&admission));
    }
    for(unsigned i=0;i<3;++i) {
        assert(esp32_mquickjs_wifi_raw_tx_queue_take(&q,&tickets[i],&borrowed));
        borrowed=(esp32_mquickjs_wifi_raw_tx_payload_t){0};
        assert(esp32_mquickjs_wifi_raw_tx_queue_accept(&q,&tickets[i]));
    }
    assert(esp32_mquickjs_wifi_raw_tx_queue_finish(&q,&tickets[1],ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_SUCCESS,&retired));
    retired=(esp32_mquickjs_wifi_raw_tx_payload_t){0};
    frames[0]=(esp32_mquickjs_wifi_raw_tx_payload_t){bytes[6],64};
    /* A packet slot is available; only bytes force eviction. Batch B's unsent
     * remainder is protected because its first packet is already in flight. */
    assert(q.free_count==1 && q.used_bytes==160);
    assert(!esp32_mquickjs_wifi_raw_tx_queue_admit(&q,frames,1,ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_DROP_OLDEST_BATCH,removed,6,&admission));
    assert(admission.evicted_packets==2 && admission.evicted_batches==1);
    assert(removed[0].data==bytes[4] && removed[1].data==bytes[5]);
    assert(q.used_bytes==160 && q.in_flight==2 && q.high_water_bytes==192);
    memset(removed,0,sizeof(removed));
    uint16_t count=0;
    assert(esp32_mquickjs_wifi_raw_tx_queue_close(&q,removed,6,&count) && count==2 && q.used_bytes==64);
    for(unsigned i=0;i<3;i+=2) {
        assert(esp32_mquickjs_wifi_raw_tx_queue_finish(&q,&tickets[i],ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_SUCCESS,&retired));
        retired=(esp32_mquickjs_wifi_raw_tx_payload_t){0};
    }
    assert(!q.used_bytes && esp32_mquickjs_wifi_raw_tx_queue_deinit(&q));
    return 0;
}
