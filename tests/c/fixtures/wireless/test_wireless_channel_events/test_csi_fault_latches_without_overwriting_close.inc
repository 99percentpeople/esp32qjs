
int main(void) {
    wifi_csi_session_t session={.radio_lease={.acquired=true,.generation=17},.lifecycle=WIFI_CSI_RUNNING};
    session.resources=&session.storage;atomic_store(&session.resources->accepting,true);
    uint8_t bytes[4]={0};wifi_csi_info_t info={.buf=bytes,.len=4};
    wifi_csi_rx_callback(&session,&info);
    assert(atomic_load(&session.lifecycle)==WIFI_CSI_FAULTED && atomic_load(&session.channel_conflicted));
    conflict=false;wifi_csi_rx_callback(&session,&info);assert(published==0 && leaves==2);
    atomic_store(&session.lifecycle,99);session.resources=&session.storage;atomic_store(&session.resources->accepting,true);
    conflict=true;wifi_csi_rx_callback(&session,&info);
    assert(atomic_load(&session.lifecycle)==99 && published==0 && leaves==3);
}
