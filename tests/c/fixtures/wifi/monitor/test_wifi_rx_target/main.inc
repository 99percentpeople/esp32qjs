
static void metadata(wifi_pkt_rx_ctrl_t *rx,unsigned length) {
    memset(rx,0,sizeof(*rx));rx->rssi=-63;rx->noise_floor=-97;rx->channel=6;rx->timestamp=UINT32_MAX-7;
    rx->rate=9;
#if CONFIG_SOC_WIFI_HE_SUPPORT
    rx->sig_len=length+4;rx->dump_len=length;rx->second=2;
    rx->cur_bb_format=3;rx->he_siga1=0x12345678;rx->he_siga2=0xabcd;rx->rxend_state=0;
#else
    rx->sig_len=length;rx->secondary_channel=2;rx->sig_mode=1;rx->ant=1;
    rx->cwb=1;rx->mcs=7;rx->ampdu_cnt=2;rx->aggregation=1;rx->stbc=1;rx->sgi=1;rx->fec_coding=1;
    rx->smoothing=1;rx->not_sounding=0;
#endif
}
static void no_packet(const esp32_mquickjs_wifi_rx_target_view_t *view) {
    assert(!view->bytes && !view->readable_length && !view->header.address_mask && !view->header_type_matches);
}
static void ht_signal_fields(void) {
#if CONFIG_SOC_WIFI_HE_SUPPORT
    for(unsigned mcs=0;mcs<128;mcs++) for(unsigned flags=0;flags<256;flags++) {
        wifi_pkt_rx_ctrl_t rx;metadata(&rx,24);
        rx.cur_bb_format=RX_BB_FORMAT_HT;
        /* Construct through the actual SDK declaration, not the decoder masks. */
        esp_wifi_htsig_t signal={.mcs=mcs,.cbw=!!(flags&1),.smoothing=!!(flags&2),
            .not_sounding=!!(flags&4),.aggregation=!!(flags&8),.stbc=(flags>>4)&3,
            .fec_coding=!!(flags&64),.sgi=!!(flags&128)};
        _Static_assert(sizeof(signal)==sizeof(uint32_t), "SDK HT SIG storage");
        uint32_t word;memcpy(&word,&signal,sizeof(word));rx.he_siga1=word;
        uint8_t packet[sizeof(rx)+24];memcpy(packet,&rx,sizeof(rx));memset(packet+sizeof(rx),0,24);
        packet[sizeof(rx)]=0x08;
        esp32_mquickjs_wifi_rx_target_view_t out;
        assert(esp32_mquickjs_wifi_rx_target_view(packet,WIFI_PKT_DATA,&out)==ESP32_MQUICKJS_WIFI_RX_SPAN_PACKET);
        const esp32_mquickjs_wifi_rx_driver_metadata_t *d=&out.metadata;
        assert(d->ht_fields_available==(mcs<=76) && !d->antenna_available);
        if(mcs>76)continue;
        assert(d->mcs==mcs && d->bandwidth_mhz==((flags&1)?40:20));
        assert(d->smoothing==!!(flags&2) && d->sounding==!(flags&4));
        assert(d->aggregation==!!(flags&8) && d->stbc==!!(flags&48));
        assert(d->ldpc==!!(flags&64) && d->short_gi==!!(flags&128));
        assert(!d->ampdu_count && d->he_layout);
    }
    /* Other formats must never interpret these same bits as HT fields. */
    for(unsigned format=0;format<16;format++) {
        if(format==RX_BB_FORMAT_HT)continue;
        wifi_pkt_rx_ctrl_t rx;metadata(&rx,24);rx.cur_bb_format=format;rx.he_siga1=UINT32_MAX;
        uint8_t packet[sizeof(rx)+24];memcpy(packet,&rx,sizeof(rx));memset(packet+sizeof(rx),0,24);
        esp32_mquickjs_wifi_rx_target_view_t out;
        assert(esp32_mquickjs_wifi_rx_target_view(packet,WIFI_PKT_DATA,&out)==ESP32_MQUICKJS_WIFI_RX_SPAN_PACKET);
        assert(!out.metadata.ht_fields_available);
    }
#endif
}
int main(void) {
    ht_signal_fields();
    size_t page=(size_t)sysconf(_SC_PAGESIZE),prefix=sizeof(wifi_pkt_rx_ctrl_t);
    uint8_t *mapping=mmap(NULL,page*3,PROT_NONE,MAP_ANONYMOUS|MAP_PRIVATE,-1,0);
    assert(mapping!=MAP_FAILED && prefix+64<page);
    assert(!mprotect(mapping+page,page,PROT_READ|PROT_WRITE));
    uint8_t *end=mapping+page*2;
    esp32_mquickjs_wifi_rx_target_view_t view;
    assert(esp32_mquickjs_wifi_rx_target_view(NULL,WIFI_PKT_DATA,&view)==ESP32_MQUICKJS_WIFI_RX_SPAN_INVALID_ARGUMENT);
    no_packet(&view);assert(!view.metadata.available);
    assert(esp32_mquickjs_wifi_rx_target_view(end,(wifi_promiscuous_pkt_type_t)99,&view)==ESP32_MQUICKJS_WIFI_RX_SPAN_UNKNOWN_TYPE);
    no_packet(&view);assert(!view.metadata.available);
    assert(esp32_mquickjs_wifi_rx_target_view(end,WIFI_PKT_MISC,NULL)==ESP32_MQUICKJS_WIFI_RX_SPAN_INVALID_ARGUMENT);
    assert(esp32_mquickjs_wifi_rx_target_view((void *)UINTPTR_MAX,WIFI_PKT_DATA,&view)==ESP32_MQUICKJS_WIFI_RX_SPAN_INVALID_ARGUMENT);
    wifi_pkt_rx_ctrl_t rx;metadata(&rx,1000);
    uint8_t *buffer=end-prefix;memcpy(buffer,&rx,prefix);
    /* MIMO/MISC may report a long packet, but only this prefix exists. */
    assert(esp32_mquickjs_wifi_rx_target_view(buffer,WIFI_PKT_MISC,&view)==ESP32_MQUICKJS_WIFI_RX_SPAN_METADATA_ONLY);
    no_packet(&view);assert(view.metadata.available && view.metadata.type==ESP32_MQUICKJS_WIFI_PACKET_MISC);
    for(unsigned length=0;length<=64;length++) {
        metadata(&rx,length);buffer=end-prefix-length;
        memcpy(buffer,&rx,prefix);memset(buffer+prefix,0,length);
        if(length)buffer[prefix]=0x08; /* non-QoS Data */
        uint8_t before[sizeof(rx)+64];memcpy(before,buffer,prefix+length);
        esp32_mquickjs_wifi_rx_span_status_t result=esp32_mquickjs_wifi_rx_target_view(buffer,WIFI_PKT_DATA,&view);
        assert(!memcmp(before,buffer,prefix+length));
        assert(view.metadata.available && view.metadata.rssi==-63 && view.metadata.noise_floor==-97);
        assert(view.metadata.primary==6 && view.metadata.secondary_raw==2 && view.metadata.timestamp_us==UINT32_MAX-7);
        if(!length) { assert(result==ESP32_MQUICKJS_WIFI_RX_SPAN_INVALID_LENGTH);no_packet(&view);continue; }
        assert(result==ESP32_MQUICKJS_WIFI_RX_SPAN_PACKET && view.bytes==buffer+prefix && view.readable_length==length);
        assert(view.header.status==(length>=24?ESP32_MQUICKJS_WIFI_RX_PARSED:ESP32_MQUICKJS_WIFI_RX_SHORT_HEADER));
        assert(view.header_type_matches==(length>=2));
#if CONFIG_SOC_WIFI_HE_SUPPORT
        assert(view.metadata.he_layout && view.metadata.dump_length_available && view.metadata.dump_length==length);
        assert(view.metadata.driver_length==length+4 && !view.metadata.antenna_available && !view.metadata.ht_fields_available);
        assert(view.metadata.signal_word1==0x12345678 && view.metadata.signal_word2==0xabcd && view.metadata.raw_format==3);
#else
        assert(!view.metadata.he_layout && !view.metadata.dump_length_available && view.metadata.driver_length==length);
        assert(view.metadata.antenna_available && view.metadata.antenna==1 && view.metadata.ht_fields_available);
        assert(view.metadata.bandwidth_mhz==40 && view.metadata.mcs==7 && view.metadata.ampdu_count==2);
        assert(view.metadata.stbc && view.metadata.short_gi && view.metadata.ldpc && view.metadata.aggregation && view.metadata.smoothing && view.metadata.sounding);
#endif
    }
    metadata(&rx,24);rx.rx_state=7;buffer=end-prefix-24;
    memcpy(buffer,&rx,prefix);memset(buffer+prefix,0,24);buffer[prefix]=0x08;
    assert(esp32_mquickjs_wifi_rx_target_view(buffer,WIFI_PKT_DATA,&view)==ESP32_MQUICKJS_WIFI_RX_SPAN_PACKET);
    assert(view.metadata.rx_state==7 && view.header.status==ESP32_MQUICKJS_WIFI_RX_PARSED);
    buffer[prefix]=0x80; /* SDK DATA + actual management header remain distinguishable. */
    assert(esp32_mquickjs_wifi_rx_target_view(buffer,WIFI_PKT_DATA,&view)==ESP32_MQUICKJS_WIFI_RX_SPAN_PACKET);
    assert(!view.header_type_matches && view.header.type==ESP32_MQUICKJS_WIFI_PACKET_MANAGEMENT);
#if CONFIG_SOC_WIFI_HE_SUPPORT
    /* Real pinned C5 promiscuous callbacks report dump_len=sig_len+4.
     * The public packet contract grants only sig_len payload bytes. Put the
     * following page behind a guard so accepting this shape cannot overread. */
    for(unsigned length=1;length<=64;length++) {
        metadata(&rx,length);rx.sig_len=length;rx.dump_len=length+4;
        buffer=end-prefix-length;memcpy(buffer,&rx,prefix);
        memset(buffer+prefix,0,length);buffer[prefix]=0x80;
        assert(esp32_mquickjs_wifi_rx_target_view(buffer,WIFI_PKT_MGMT,&view)==ESP32_MQUICKJS_WIFI_RX_SPAN_PACKET);
        assert(view.readable_length==length && view.metadata.driver_length==length);
        assert(view.metadata.dump_length==length+4);
    }
    metadata(&rx,1);rx.sig_len=1;rx.dump_len=2;buffer=end-prefix;memcpy(buffer,&rx,prefix);
    assert(esp32_mquickjs_wifi_rx_target_view(buffer,WIFI_PKT_DATA,&view)==ESP32_MQUICKJS_WIFI_RX_SPAN_INVALID_LENGTH);
    no_packet(&view);
    for(unsigned extra=1;extra<=8;extra++) {
        if(extra==4)continue;
        rx.sig_len=24;rx.dump_len=24+extra;memcpy(buffer,&rx,prefix);
        assert(esp32_mquickjs_wifi_rx_target_view(buffer,WIFI_PKT_DATA,&view)==ESP32_MQUICKJS_WIFI_RX_SPAN_INVALID_LENGTH);
        no_packet(&view);
    }
    rx.sig_len=100;rx.dump_len=0;memcpy(buffer,&rx,prefix);
    assert(esp32_mquickjs_wifi_rx_target_view(buffer,WIFI_PKT_DATA,&view)==ESP32_MQUICKJS_WIFI_RX_SPAN_INVALID_LENGTH);
    no_packet(&view);
#else
    metadata(&rx,24);rx.sig_mode=0;buffer=end-prefix-24;
    memcpy(buffer,&rx,prefix);memset(buffer+prefix,0,24);buffer[prefix]=0x08;
    assert(esp32_mquickjs_wifi_rx_target_view(buffer,WIFI_PKT_DATA,&view)==ESP32_MQUICKJS_WIFI_RX_SPAN_PACKET);
    assert(!view.metadata.ht_fields_available && !view.metadata.mcs && !view.metadata.bandwidth_mhz);
#endif
    assert(!munmap(mapping,page*3));
}
