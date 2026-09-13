
static esp32_mquickjs_wifi_btwt_timer_result_t broadcast_results[32];
static esp32_mquickjs_wifi_twt_teardown_tx_snapshot_t broadcast_teardown;
static uint16_t broadcast_attempts[32];
static unsigned broadcast_submits, broadcast_teardowns, broadcast_retires;
static esp_err_t broadcast_submit_error, broadcast_retire_error, broadcast_teardown_error;
static bool broadcast_accept=true, broadcast_early;
static uint32_t broadcast_sequence=1000;
static void heap_caps_free(void *p) {assert(!critical);free(p);}
uint16_t esp32_mquickjs_wifi_twt_tx_broadcast_remaining(unsigned slot) {
    assert(!critical && slot<32);return 255-broadcast_attempts[slot];
}
bool esp32_mquickjs_wifi_btwt_setup_result(unsigned slot,uint32_t identity,esp32_mquickjs_wifi_btwt_timer_result_t *out) {
    assert(!critical);if(broadcast_results[slot].identity!=identity)return false;
    *out=broadcast_results[slot];return true;
}
esp_err_t esp32_mquickjs_wifi_twt_sdk_broadcast_submit(const wifi_btwt_setup_config_t *config,
    esp32_mquickjs_wifi_btwt_dispatch_t *out) {
    assert(locks && !critical);++broadcast_submits;
    if(broadcast_early) {
        esp32_mquickjs_wifi_twt_token_t tokens[31];
        unsigned count=esp32_mquickjs_wifi_radio_twt_broadcast_tokens(tokens,31,false);
        bool found=false;
        for(unsigned i=0;i<count;++i) {
            esp32_mquickjs_wifi_twt_broadcast_radio_state_t state;
            assert(esp32_mquickjs_wifi_radio_twt_broadcast_status(&tokens[i],&state));
            if(state.requested.btwt_id==config->btwt_id)found=state.dispatching;
        }
        assert(found);
    }
    if(!broadcast_accept)return broadcast_submit_error;
    ++broadcast_attempts[config->btwt_id];
    *out=(esp32_mquickjs_wifi_btwt_dispatch_t){.config=*config,.identity=++broadcast_sequence,
        .native_entered=true,.native_completed=true,.driver_called=true,.sdk_error=broadcast_submit_error};
    broadcast_results[config->btwt_id]=(esp32_mquickjs_wifi_btwt_timer_result_t){.identity=out->identity,.held=true};
    return broadcast_submit_error;
}
void esp32_mquickjs_wifi_twt_teardown_tx_snapshot(esp32_mquickjs_wifi_twt_teardown_tx_snapshot_t *out) {
    assert(!critical);*out=broadcast_teardown;
}
esp_err_t esp32_mquickjs_wifi_twt_sdk_broadcast_teardown(unsigned slot,uint32_t identity) {
    assert(locks && !critical && broadcast_results[slot].identity==identity);++broadcast_teardowns;
    broadcast_teardown=(esp32_mquickjs_wifi_twt_teardown_tx_snapshot_t){.broadcast=true,.flow=slot,.identity=identity};
    return broadcast_teardown_error;
}
esp_err_t esp32_mquickjs_wifi_btwt_retire_poll(esp32_mquickjs_wifi_btwt_retire_t *state,
    const esp32_mquickjs_wifi_twt_token_t *token,unsigned slot,uint32_t identity) {
    assert(locks && !critical && token->identity && broadcast_results[slot].identity==identity);
    ++broadcast_retires;state->stage="injected-broadcast-retirement";
    /* Real owner must hide an in-progress mutable retirement record. */
    esp32_mquickjs_wifi_twt_broadcast_radio_state_t out;
    assert(!esp32_mquickjs_wifi_radio_twt_broadcast_status(token,&out));
    if(broadcast_retire_error)return broadcast_retire_error;
    broadcast_results[slot].held=false;state->released=true;state->stage=NULL;
    native_individual.broadcast_id_bitmap &= ~(1U<<slot);return 0;
}
