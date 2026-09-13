
int main(void) {
    (void)begin;(void)start;(void)close_rate;(void)esp32_mquickjs_wifi_radio_5ghz_channel_bit;
    esp32_mquickjs_wifi_raw_tx_ap_context_t context={0};const char *stage;
    setup();assert(!esp32_mquickjs_wifi_ap_open_raw_tx_rate(&context,&requested,6,&lease,&channel,&stage));
    assert(context.owned && !context.lifecycle.identity && s_ap_raw_owner.identity==lease.identity && s_ap_netif);
    assert(!stage && !s_ap_cleanup_pending && allocations==frees);
    retire_error=61;
    assert(esp32_mquickjs_wifi_ap_close_raw_tx_rate(&context,&lease,false,&stage)==61);
    assert(context.owned && context.lifecycle.identity && !lease.acquired && s_ap_cleanup_pending && s_ap_netif);
    unsigned previous_writes=writes,previous_stops=stops;retire_error=0;
    assert(!esp32_mquickjs_wifi_ap_close_raw_tx_rate(&context,&lease,false,&stage));
    assert(!context.owned && !s_ap_raw_owner.identity && !s_ap_netif && !s_ap_coordinator.identity);
    assert(writes==previous_writes && stops==previous_stops);
    setup();prepare_error=62;
    assert(esp32_mquickjs_wifi_ap_open_raw_tx_rate(&context,&requested,6,&lease,&channel,&stage)==62);
    assert(context.owned && context.lifecycle.identity && s_ap_netif && !writes && !starts);
    prepare_error=0;assert(!esp32_mquickjs_wifi_ap_close_raw_tx_rate(&context,&lease,false,&stage));
    setup();unsigned prepared=prepares;
    assert(esp32_mquickjs_wifi_ap_open_raw_tx_rate(&context,&requested,11,&lease,&channel,&stage)==ESP_ERR_INVALID_ARG);
    assert(context.owned && prepares==prepared && !writes && !starts);
    assert(!esp32_mquickjs_wifi_ap_close_raw_tx_rate(&context,&lease,false,&stage));
    setup();s_radio.effective_mode=WIFI_MODE_APSTA;
    assert(!esp32_mquickjs_wifi_ap_open_raw_tx_rate(&context,&requested,6,&lease,&channel,&stage));
    assert(context.mode==WIFI_MODE_APSTA && context.station_cleanup_pending && station_live && s_radio.effective_mode==WIFI_MODE_APSTA);
    station_retire_error=63;unsigned written=writes;
    assert(esp32_mquickjs_wifi_ap_close_raw_tx_rate(&context,&lease,false,&stage)==63);
    assert(context.owned && context.lifecycle.identity && !s_ap_netif && station_live && context.station_cleanup_pending);
    unsigned restored=writes;assert(restored>written);station_retire_error=0;
    assert(!esp32_mquickjs_wifi_ap_close_raw_tx_rate(&context,&lease,false,&stage));
    assert(!station_live && !context.owned && writes==restored && s_radio.effective_mode==WIFI_MODE_APSTA);
    setup();s_radio.effective_mode=WIFI_MODE_APSTA;station_prepare_error=64;
    assert(esp32_mquickjs_wifi_ap_open_raw_tx_rate(&context,&requested,6,&lease,&channel,&stage)==64);
    assert(context.station_cleanup_pending && station_live && !s_ap_netif && !writes && !starts);
    station_prepare_error=0;assert(!esp32_mquickjs_wifi_ap_close_raw_tx_rate(&context,&lease,false,&stage));
    assert(!station_live && !context.owned);
    /* An old Station helper retirement can fail even for an AP-only source.
     * It must retain the exclusion token until that cleanup also completes. */
    setup();station_live=true;station_retire_error=65;
    assert(esp32_mquickjs_wifi_ap_open_raw_tx_rate(&context,&requested,6,&lease,&channel,&stage)==65);
    assert(context.owned && context.station_cleanup_pending && !writes && !starts);
    assert(esp32_mquickjs_wifi_ap_close_raw_tx_rate(&context,&lease,false,&stage)==65 && context.owned);
    station_retire_error=0;assert(!esp32_mquickjs_wifi_ap_close_raw_tx_rate(&context,&lease,false,&stage));
    assert(!context.owned && !station_live);
    setup();s_radio.effective_mode=WIFI_MODE_APSTA;
    assert(!esp32_mquickjs_wifi_ap_open_raw_tx_rate(&context,&requested,0,&lease,&channel,&stage));
    assert(esp32_mquickjs_wifi_ap_close_raw_tx_rate(&context,&lease,true,&stage)==ESP_ERR_INVALID_STATE);
    /* Inject central recovery's already-proven helper retirement and original
     * Radio owner release; a subsequently rebuilt helper must remain untouched. */
    memset(&lease,0,sizeof(lease));memset(&s_ap_raw_owner,0,sizeof(s_ap_raw_owner));
    s_ap_netif=(void *)2;station_live=true;unsigned retired=retires,station_retired=station_retires;
    assert(!esp32_mquickjs_wifi_ap_close_raw_tx_rate(&context,&lease,true,&stage));
    assert(!context.owned && s_ap_netif==(void *)2 && retires==retired && station_live && station_retires==station_retired);
    return 0;
}
