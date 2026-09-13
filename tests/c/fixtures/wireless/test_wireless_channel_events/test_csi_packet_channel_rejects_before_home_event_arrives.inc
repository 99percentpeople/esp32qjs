
int main(void) {
    wifi_csi_session_t session={.radio_lease={.acquired=true,.generation=17},.lifecycle=WIFI_CSI_RUNNING,.options={.fixed_channel=true,.channel=6}};
    session.resources=&session.storage;atomic_store(&session.resources->accepting,true);conflict=false;
    uint8_t bytes[4]={0};wifi_csi_info_t info={.buf=bytes,.len=4};
    wifi_csi_rx_callback(&session,&info);
    assert(!published && leaves==1 && atomic_load(&session.channel_conflicted));
    assert(atomic_load(&session.lifecycle)==WIFI_CSI_FAULTED);
}
