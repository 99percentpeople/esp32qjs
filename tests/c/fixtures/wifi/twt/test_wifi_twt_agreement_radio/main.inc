
static unsigned individual_owners(void) {return s_radio.clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_TWT];}
static void individual_reset(void) {
    reset_vendor();s_radio.effective_mode=WIFI_MODE_STA;s_radio.started=true;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
    s_radio.next_operation_identity=1;s_twt_identity=(esp32_mquickjs_wifi_twt_identity_t){.next_identity=1};
    memset(native_individual.individual_ids,0xff,sizeof(native_individual.individual_ids));
}
int main(void) {
    individual_reset();
    esp32_mquickjs_wifi_itwt_options_t options={.config={.setup_cmd=TWT_REQUEST,.min_wake_dura=1,
        .wake_invl_mant=10256,.timeout_time_ms=100},.timeout_ms=1};
    esp32_mquickjs_wifi_twt_token_t tokens[9]={0},old;
    allocation_fails=true;
    assert(esp32_mquickjs_wifi_radio_twt_individual_submit(&options,&tokens[0])==ESP_ERR_NO_MEM);
    assert(!individual_submits && !individual_owners());allocation_fails=false;early_individual=true;
    for(unsigned i=0;i<8;++i)assert(!esp32_mquickjs_wifi_radio_twt_individual_submit(&options,&tokens[i]));
    assert(individual_owners()==8 && s_radio.clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_TWT]==8 && !s_radio.operation.identity);
    assert(esp32_mquickjs_wifi_radio_twt_individual_submit(&options,&tokens[8])==ESP_ERR_NO_MEM && individual_submits==8);
    esp32_mquickjs_wifi_radio_lease_t borrowed=s_twt_individual[0].lease;
    wifi_radio_operation_lock();wifi_radio_release_locked(&borrowed);wifi_radio_operation_unlock();assert(borrowed.acquired);
    old=tokens[0];++old.generation;assert(esp32_mquickjs_wifi_radio_twt_individual_close(&old)==ESP_ERR_INVALID_STATE);
    native_individual.individual_ids[3]=individual_results[0].request_id;
    retirement_error=ESP_ERR_NOT_FINISHED;
    assert(esp32_mquickjs_wifi_radio_twt_individual_close(&tokens[0])==ESP_ERR_NOT_FINISHED && teardowns==1 && individual_owners()==8);
    assert(esp32_mquickjs_wifi_radio_twt_individual_close(&tokens[0])==ESP_ERR_NOT_FINISHED && teardowns==1);
    esp32_mquickjs_wifi_twt_individual_radio_state_t state;
    assert(esp32_mquickjs_wifi_radio_twt_individual_status(&tokens[0],&state) && state.closing && state.cleanup_error==ESP_ERR_NOT_FINISHED);
    old=tokens[0];retirement_error=0;assert(!esp32_mquickjs_wifi_radio_twt_individual_close(&tokens[0]) && !tokens[0].identity && individual_owners()==7);
    assert(esp32_mquickjs_wifi_radio_twt_individual_close(&old)==ESP_ERR_INVALID_STATE);
    assert(!esp32_mquickjs_wifi_radio_twt_individual_submit(&options,&tokens[0]) && tokens[0].identity>old.identity);
    assert(!esp32_mquickjs_wifi_radio_twt_individual_request_close(&old));
    for(unsigned i=0;i<8;++i)assert(!esp32_mquickjs_wifi_radio_twt_individual_close(&tokens[i]));
    assert(!individual_owners());accept_individual=false;individual_error=77;
    assert(esp32_mquickjs_wifi_radio_twt_individual_submit(&options,&tokens[0])==77 && tokens[0].identity);
    unsigned before=retire_steps;assert(!esp32_mquickjs_wifi_radio_twt_individual_close(&tokens[0]) && retire_steps==before);
    options.connection_id_set=true;options.config.twt_id=0;
    assert(esp32_mquickjs_wifi_radio_twt_individual_submit(&options,&tokens[0])==ESP_ERR_INVALID_STATE);
    options.connection_id_set=false;s_twt_identity.next_connection_id=32768;
    assert(esp32_mquickjs_wifi_radio_twt_individual_submit(&options,&tokens[0])==ESP_ERR_NO_MEM);
    assert(!locks && !critical && !individual_owners());free(s_twt_individual);return 0;
}
