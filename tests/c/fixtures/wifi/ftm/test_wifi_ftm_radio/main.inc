
static void reset_ftm(void) {
    reset_vendor();s_radio.next_operation_identity=1;s_radio.effective_mode=WIFI_MODE_STA;
    memset(&s_ftm,0,sizeof(s_ftm));memset(&s_ftm_timer_fence,0,sizeof(s_ftm_timer_fence));
    ftm_start_calls=ftm_end_calls=report_calls=timer_creates=timer_starts=ftm_posts=0;
    ftm_start_error=ftm_end_error=report_error=timer_error=ftm_post_error=0;
    ftm_early=report_race=ftm_queued=timer_pending=false;quiescent_error=0;
}
static void terminal(void){wifi_radio_operation_lock();ftm_event();wifi_radio_operation_unlock();}
static void timer_fire(void){assert(timer_pending);timer_pending=false;timer_args.callback(timer_args.arg);}
static void marker_fire(void){assert(ftm_queued);wifi_radio_operation_lock();wifi_radio_lifecycle_fence(NULL,7,3,&ftm_marker);wifi_radio_operation_unlock();ftm_queued=false;}
static unsigned owners(void){return s_radio.clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_FTM];}
int main(void) {
    wifi_ftm_initiator_cfg_t cfg={.resp_mac={2,3,4,5,6,7},.channel=6,.frm_count=64,.burst_period=2};
    esp32_mquickjs_wifi_ftm_token_t token={0},old,other={0};esp32_mquickjs_wifi_ftm_state_t state;
    wifi_ftm_report_entry_t entries[64];
    reset_ftm();cfg.frm_count=63;
    assert(esp32_mquickjs_wifi_radio_ftm_start(&cfg,&token)==ESP_ERR_INVALID_ARG && !ftm_start_calls);
    cfg.frm_count=64;cfg.burst_period=1;
    assert(esp32_mquickjs_wifi_radio_ftm_start(&cfg,&token)==ESP_ERR_INVALID_ARG && !ftm_start_calls && !owners());
    cfg.burst_period=2;cfg.channel=1;connected=true;
    assert(esp32_mquickjs_wifi_radio_ftm_start(&cfg,&token)==ESP_ERR_INVALID_STATE && !owners());
    connected=false;cfg.channel=6;ftm_early=true;
    assert(!esp32_mquickjs_wifi_radio_ftm_start(&cfg,&token) && owners()==1 && ftm_start_calls==1);
    assert(esp32_mquickjs_wifi_radio_ftm_start(&cfg,&other)==ESP_ERR_INVALID_STATE && !other.identity);
    old=token;cfg.resp_mac[5]=9;assert(s_ftm.config.resp_mac[5]==7);
    esp32_mquickjs_wifi_radio_operation_t bypass=s_radio.operation;esp32_mquickjs_wifi_radio_end_operation(&bypass);
    wifi_radio_operation_lock();wifi_radio_release_locked(&s_ftm.lease);wifi_radio_operation_unlock();assert(owners()==1);
    assert(esp32_mquickjs_wifi_radio_ftm_collect(&token,entries,SIZE_MAX,64,&state)==ESP_ERR_INVALID_ARG && !report_calls);
    timer_error=77;assert(esp32_mquickjs_wifi_radio_ftm_collect(&token,entries,sizeof(entries),64,&state)==77 && owners()==1);
    timer_error=0;
    assert(esp32_mquickjs_wifi_radio_ftm_collect(&token,entries,sizeof(entries),64,&state)==ESP_ERR_TIMEOUT && timer_pending && !report_calls);
    timer_fire();report_error=88;
    assert(esp32_mquickjs_wifi_radio_ftm_collect(&token,entries,sizeof(entries),64,&state)==88 && !state.report_consumed);
    report_error=0;
    assert(!esp32_mquickjs_wifi_radio_ftm_collect(&token,entries,sizeof(entries),64,&state));
    assert(state.copied_entries==64 && state.report.ftm_report_num_entries==70 && entries[63].t1==UINT64_MAX-63);
    assert(esp32_mquickjs_wifi_radio_ftm_collect(&token,entries,sizeof(entries),64,&state)==ESP_ERR_INVALID_STATE && report_calls==2);
    ftm_post_error=66;assert(esp32_mquickjs_wifi_radio_ftm_retire(&token,&state)==66 && owners()==1);
    ftm_post_error=0;assert(esp32_mquickjs_wifi_radio_ftm_retire(&token,&state)==ESP_ERR_TIMEOUT && ftm_queued);
    marker_fire();assert(!esp32_mquickjs_wifi_radio_ftm_retire(&token,&state) && !token.identity && !owners());
    assert(!esp32_mquickjs_wifi_radio_ftm_start(&cfg,&token) && token.identity!=old.identity);
    assert(esp32_mquickjs_wifi_radio_ftm_end(&old)==ESP_ERR_INVALID_STATE && owners()==1 && !ftm_end_calls);
    /* Setup boundaries may reset a quarantined fixture; production cannot. */
    reset_ftm();token=(esp32_mquickjs_wifi_ftm_token_t){0};ftm_start_error=55;
    assert(esp32_mquickjs_wifi_radio_ftm_start(&cfg,&token)==55 && token.identity && owners()==1);
    ftm_end_error=44;assert(esp32_mquickjs_wifi_radio_ftm_end(&token)==44 && ftm_end_calls==1);
    ftm_end_error=0;assert(!esp32_mquickjs_wifi_radio_ftm_end(&token) && ftm_end_calls==2);
    assert(!esp32_mquickjs_wifi_radio_ftm_end(&token) && ftm_end_calls==2);
    assert(esp32_mquickjs_wifi_radio_ftm_collect(&token,NULL,0,0,&state)==ESP_ERR_TIMEOUT && !timer_pending && !report_calls);
    terminal();assert(esp32_mquickjs_wifi_radio_ftm_collect(&token,NULL,0,0,&state)==ESP_ERR_TIMEOUT);
    timer_fire();assert(!esp32_mquickjs_wifi_radio_ftm_collect(&token,NULL,0,0,&state) && state.report_discarded && state.submit_error==55);
    assert(esp32_mquickjs_wifi_radio_ftm_retire(&token,&state)==ESP_ERR_TIMEOUT);terminal();marker_fire();
    assert(esp32_mquickjs_wifi_radio_ftm_retire(&token,&state)==ESP_ERR_TIMEOUT && state.ambiguous && owners()==1);
    reset_ftm();token=(esp32_mquickjs_wifi_ftm_token_t){0};ftm_early=true;
    assert(!esp32_mquickjs_wifi_radio_ftm_start(&cfg,&token));
    assert(esp32_mquickjs_wifi_radio_ftm_collect(&token,entries,sizeof(entries),64,&state)==ESP_ERR_TIMEOUT);
    timer_fire();report_race=true;
    assert(esp32_mquickjs_wifi_radio_ftm_collect(&token,entries,sizeof(entries),64,&state)==ESP_ERR_INVALID_RESPONSE);
    assert(state.report_consumed && state.ambiguous && !state.copied_entries && !entries[0].t1 && owners()==1);
    reset_ftm();token=(esp32_mquickjs_wifi_ftm_token_t){0};s_radio.next_operation_identity=UINT32_MAX;
    assert(!esp32_mquickjs_wifi_radio_ftm_start(&cfg,&token) && token.identity==UINT32_MAX && !s_radio.next_operation_identity);
    return 0;
}
