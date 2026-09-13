
int main(void) {
    wifi_csi_session_t session={.radio_lease={.acquired=true,.generation=17},.lifecycle=WIFI_CSI_RUNNING};
    session.resources=&session.storage;atomic_store(&session.resources->accepting,true);
    uint8_t bytes[4]={0};wifi_csi_info_t info={.buf=bytes,.len=4};
    wifi_csi_rx_callback(&session,&info);
    assert(published==0 && leaves==1);
}
