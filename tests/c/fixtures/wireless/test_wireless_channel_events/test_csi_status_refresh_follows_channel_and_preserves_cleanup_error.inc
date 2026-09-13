
int main(void) {
    wifi_csi_session_t session={.radio_lease={.acquired=true,.generation=17},.lifecycle=WIFI_CSI_RUNNING};
    session.resources=&session.storage;atomic_store(&session.resources->accepting,true);
    fixed=false;conflict=false;
    wifi_csi_refresh_channel(&session);
    assert(session.effective_channel==11 && session.radio_generation==17 && !session.last_error);
    fixed=true;conflict=true;
    wifi_csi_refresh_channel(&session);
    assert(atomic_load(&session.lifecycle)==WIFI_CSI_FAULTED);
    assert(session.last_error==ESP_ERR_INVALID_STATE && !strcmp(session.last_error_code,"WIFI_CSI_CHANNEL_CONFLICT"));
    session.last_error=-9;session.last_error_code="cleanup-error";session.last_error_stage="unregister";
    wifi_csi_refresh_channel(&session);
    assert(session.last_error==-9 && !strcmp(session.last_error_stage,"unregister"));
}
