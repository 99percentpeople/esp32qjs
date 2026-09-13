
int main(void) {
    wifi_csi_session_t session={.radio_lease={.acquired=true,.generation=17},.lifecycle=WIFI_CSI_RUNNING};
    session.resources=&session.storage;atomic_store(&session.resources->accepting,true);
    uint8_t bytes[4]={0};wifi_csi_info_t info={.buf=bytes,.len=4};
    conflict=false;fixed=false;
    wifi_csi_rx_callback(&session,&info);assert(published==1 && leaves==1);assert(published_time==UINT64_C(0x300000064) && published_generation==17);
    observation_error=-9;
    wifi_csi_rx_callback(&session,&info);assert(published==1 && leaves==2);
    assert(atomic_load(&session.resources->accepting));
    observation_error=0;wifi_csi_rx_callback(&session,&info);assert(published==2);
}
