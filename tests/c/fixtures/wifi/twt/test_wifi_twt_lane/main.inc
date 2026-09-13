
#define api(name) esp32_mquickjs_wifi_twt_##name
typedef esp32_mquickjs_wifi_twt_lane_t lane_t;
typedef esp32_mquickjs_wifi_twt_token_t token_t;
typedef esp32_mquickjs_wifi_twt_identity_t domain_t;
typedef esp32_mquickjs_wifi_twt_config_t config_t;
static esp32_mquickjs_wifi_itwt_options_t individual(void) {
    return (esp32_mquickjs_wifi_itwt_options_t){.config={.setup_cmd=TWT_REQUEST,
        .min_wake_dura=1,.wake_invl_mant=10256,.timeout_time_ms=100},.timeout_ms=1};
}
static wifi_event_sta_itwt_setup_t accepted(lane_t *lane,unsigned flow) {
    wifi_event_sta_itwt_setup_t event={.config=lane->requested.individual,.status=1,
        .target_wake_time=UINT64_C(0xfedcba9876543210)};
    event.config.setup_cmd=TWT_ACCEPT;event.config.flow_id=flow;
    return event;
}
static void retire(lane_t *lane,token_t *token) {
    uint32_t revision;
    assert(api(retirement_revision)(lane,token,&revision));
    assert(!api(event_fenced)(lane,token,revision));
    assert(api(sdk_retired)(lane,token,revision));
    assert(!api(release)(lane,token));
    assert(api(event_fenced)(lane,token,revision));
    assert(api(release)(lane,token));
}
static void teardown_drained(lane_t *lane,token_t *token) {
    uint32_t revision=lane->revision;
    assert(!api(begin_teardown)(lane,token));
    assert(!api(teardown_fenced)(lane,token,revision));
    assert(!api(teardown_quiescent)(lane,token,revision+1));
    assert(api(teardown_quiescent)(lane,token,revision));
    assert(!api(begin_teardown)(lane,token));
    assert(api(teardown_fenced)(lane,token,revision));
}
static void individual_orderings(void) {
    /* Enumerate completion-before/after-SDK-return and close-before/after-
     * completion, preserving the exact production transitions in each order. */
    for(unsigned early=0;early<2;++early)for(unsigned close_early=0;close_early<2;++close_early) {
        domain_t domain={.next_identity=1};lane_t lane={0};token_t token={0};
        esp32_mquickjs_wifi_itwt_options_t options=individual();
        assert(api(reserve_individual)(&domain,&lane,7,&options,&token)==ESP_OK);
        token_t stale=token;stale.generation++;
        assert(!api(request_close)(&lane,&stale) && !api(begin_submit)(&lane,&stale));
        assert(api(begin_submit)(&lane,&token));
        assert(!api(begin_submit)(&lane,&token) && !api(release)(&lane,&token));
        assert(!api(terminated)(&lane,&token));
        if(close_early)assert(api(request_close)(&lane,&token));
        wifi_event_sta_itwt_setup_t event=accepted(&lane,5),wrong=event;
        wrong.config.twt_id++;
        uint32_t before=lane.revision;
        assert(!api(observe_individual)(&lane,&wrong) && lane.revision==before);
        if(early)assert(api(observe_individual)(&lane,&event));
        config_t written=lane.requested;written.individual.flow_id=3;
        assert(api(submitted)(&lane,&token,ESP_OK,&written));
        if(!early)assert(api(observe_individual)(&lane,&event));
        assert(lane.setup_success && lane.agreement_id==5 && !lane.ambiguous);
        assert(lane.requested.individual.flow_id==0 && lane.dispatched.individual.flow_id==3);
        assert(lane.setup.individual.target_wake_time==UINT64_C(0xfedcba9876543210));
        assert(!api(release)(&lane,&token));
        if(!close_early)assert(api(request_close)(&lane,&token));
        assert(api(begin_teardown)(&lane,&token));
        assert(!api(begin_teardown)(&lane,&token) && !api(terminated)(&lane,&token));
        uint32_t revision;
        assert(!api(retirement_revision)(&lane,&token,&revision));
        /* A completion inside SDK teardown may fail even when submission
         * returns ESP_OK. Retry must await independent native/event drain. */
        assert(api(observe_teardown)(&lane,ESP32_MQUICKJS_WIFI_TWT_INDIVIDUAL,5,0));
        assert(api(teardown_submitted)(&lane,&token,ESP_OK) && !lane.teardown_written);
        teardown_drained(&lane,&token);
        revision=lane.revision;
        assert(api(observe_teardown)(&lane,ESP32_MQUICKJS_WIFI_TWT_INDIVIDUAL,5,0));
        assert(!api(teardown_fenced)(&lane,&token,revision));
        teardown_drained(&lane,&token);
        assert(api(begin_teardown)(&lane,&token));
        assert(api(teardown_submitted)(&lane,&token,1234) && lane.teardown_error==1234);
        teardown_drained(&lane,&token);
        assert(api(begin_teardown)(&lane,&token));
        assert(api(teardown_submitted)(&lane,&token,ESP_OK) && lane.teardown_written);
        assert(!api(begin_teardown)(&lane,&token));
        assert(!api(observe_teardown)(&lane,ESP32_MQUICKJS_WIFI_TWT_INDIVIDUAL,3,1));
        assert(api(observe_teardown)(&lane,ESP32_MQUICKJS_WIFI_TWT_INDIVIDUAL,5,1));
        assert(!api(release)(&lane,&token));
        assert(api(retirement_revision)(&lane,&token,&revision));
        assert(!api(sdk_retired)(&lane,&token,revision+1));
        assert(api(sdk_retired)(&lane,&token,revision));
        assert(api(event_fenced)(&lane,&token,revision));
        /* Even an identical late setup invalidates both earlier fences. */
        assert(api(observe_individual)(&lane,&event) && !lane.ambiguous);
        assert(!lane.sdk_retired && !lane.event_fenced && !api(release)(&lane,&token));
        assert(!api(event_fenced)(&lane,&token,revision));
        token_t old=token;retire(&lane,&token);
        assert(!token.identity && !api(release)(&lane,&old));
        assert(api(reserve_individual)(&domain,&lane,8,&options,&token)==ESP_OK);
        assert(token.identity>old.identity && lane.requested.individual.twt_id!=event.config.twt_id);
        assert(api(begin_submit)(&lane,&token));
        assert(!api(observe_individual)(&lane,&event));
        written=lane.requested;assert(api(submitted)(&lane,&token,4321,&written));
        assert(lane.submit_error==4321 && !api(release)(&lane,&token));
        retire(&lane,&token);
    }
}
static void identity_and_exhaustion(void) {
    domain_t domain={.next_identity=1};lane_t a={0},b={0};token_t ta={0},tb={0};
    esp32_mquickjs_wifi_itwt_options_t options=individual();
    assert(api(reserve_individual)(NULL,&a,1,&options,&ta)==ESP_ERR_INVALID_ARG);
    assert(api(reserve_individual)(&domain,&a,0,&options,&ta)==ESP_ERR_INVALID_ARG);
    assert(api(reserve_individual)(&domain,&a,1,&options,&ta)==ESP_OK);
    assert(api(reserve_individual)(&domain,&b,1,&options,&tb)==ESP_OK);
    assert(ta.identity!=tb.identity && a.requested.individual.twt_id!=b.requested.individual.twt_id);
    token_t old=ta;
    assert(api(request_close)(&a,&ta) && !api(begin_submit)(&a,&ta));
    assert(api(release)(&a,&ta));
    assert(!api(release)(&b,&old) && !api(request_close)(&b,&old));
    assert(api(release)(&b,&tb));
    options.connection_id_set=true;options.config.twt_id=1;
    uint64_t next=domain.next_identity;
    assert(api(reserve_individual)(&domain,&a,1,&options,&ta)==ESP_ERR_INVALID_STATE);
    assert(domain.next_identity==next && !ta.identity && !a.token.identity);
    options.config.twt_id=32767;
    assert(api(reserve_individual)(&domain,&a,1,&options,&ta)==ESP_OK);
    assert(a.requested.individual.twt_id==32767 && api(release)(&a,&ta));
    options.connection_id_set=false;
    assert(api(reserve_individual)(&domain,&a,2,&options,&ta)==ESP_ERR_NO_MEM);
    esp32_mquickjs_wifi_btwt_options_t bt={.config={.setup_cmd=TWT_REQUEST,.btwt_id=1,.timeout_time_ms=1},.timeout_ms=1};
    domain.next_identity=UINT64_MAX;
    assert(api(reserve_broadcast)(&domain,&a,2,&bt,&ta)==ESP_OK && ta.identity==UINT64_MAX);
    assert(api(release)(&a,&ta) && !domain.next_identity);
    assert(api(reserve_broadcast)(&domain,&a,3,&bt,&ta)==ESP_ERR_NO_MEM);
}
static void conflicts_and_failure(void) {
    domain_t domain={.next_identity=1};lane_t lane={0};token_t token={0};
    esp32_mquickjs_wifi_itwt_options_t options=individual();
    assert(api(reserve_individual)(&domain,&lane,1,&options,&token)==ESP_OK);
    assert(api(begin_submit)(&lane,&token));
    config_t written=lane.requested;
    assert(api(submitted)(&lane,&token,ESP_OK,&written));
    wifi_event_sta_itwt_setup_t event=accepted(&lane,2);
    event.status=ESP_OK; /* zero is NOT setup success */
    assert(api(observe_individual)(&lane,&event) && !lane.setup_success);
    assert(!api(release)(&lane,&token));retire(&lane,&token);
    assert(api(reserve_individual)(&domain,&lane,1,&options,&token)==ESP_OK);
    assert(api(begin_submit)(&lane,&token));written=lane.requested;
    assert(api(submitted)(&lane,&token,ESP_OK,&written));event=accepted(&lane,2);
    assert(api(observe_individual)(&lane,&event));
    event.target_wake_time++;
    assert(api(observe_individual)(&lane,&event) && lane.ambiguous);
    assert(lane.setup.individual.target_wake_time!=event.target_wake_time);
    assert(api(request_close)(&lane,&token) && !api(begin_teardown)(&lane,&token));
    lane.revision=UINT32_MAX;
    uint32_t revision;
    assert(!api(retirement_revision)(&lane,&token,&revision));
    assert(!api(sdk_retired)(&lane,&token,UINT32_MAX));
    assert(!api(release)(&lane,&token));
    assert(api(terminated)(&lane,&token));
    assert(!api(observe_individual)(&lane,&event));
    assert(api(release)(&lane,&token));
}
static void broadcast(void) {
    domain_t domain={.next_identity=1};lane_t lane={0};token_t token={0};
    esp32_mquickjs_wifi_btwt_options_t options={.config={.setup_cmd=TWT_REQUEST,
        .btwt_id=2,.timeout_time_ms=1},.timeout_ms=1};
    assert(api(reserve_broadcast)(&domain,&lane,1,&options,&token)==ESP_OK);
    assert(api(begin_submit)(&lane,&token));
    wifi_event_sta_btwt_setup_t event={.status=BTWT_SETUP_SUCCESS,.setup_cmd=TWT_ACCEPT,
        .btwt_id=7,.min_wake_dura=1,.wake_invl_mant=512,.wake_invl_expn=12,
        .target_wake_time=UINT64_C(0xffffffff00000001)};
    assert(api(observe_broadcast)(&lane,&event));
    config_t written=lane.requested;written.broadcast.btwt_id=4;
    assert(api(submitted)(&lane,&token,ESP_OK,&written));
    assert(lane.agreement_id==7 && lane.requested.broadcast.btwt_id==2 && lane.dispatched.broadcast.btwt_id==4);
    assert(!lane.ambiguous && lane.setup.broadcast.target_wake_time==event.target_wake_time);
    assert(!api(observe_teardown)(&lane,ESP32_MQUICKJS_WIFI_TWT_INDIVIDUAL,7,1));
    assert(api(request_close)(&lane,&token) && api(begin_teardown)(&lane,&token));
    assert(api(observe_teardown)(&lane,ESP32_MQUICKJS_WIFI_TWT_BROADCAST,7,1));
    assert(api(teardown_submitted)(&lane,&token,ESP_OK));
    assert(!api(begin_teardown)(&lane,&token));retire(&lane,&token);
    assert(api(reserve_broadcast)(&domain,&lane,1,&options,&token)==ESP_OK);
    assert(api(begin_submit)(&lane,&token));written=lane.requested;
    assert(api(submitted)(&lane,&token,ESP_OK,&written));
    event.btwt_id=32;assert(api(observe_broadcast)(&lane,&event) && lane.ambiguous);
    assert(api(request_close)(&lane,&token) && !api(begin_teardown)(&lane,&token));
    assert(api(terminated)(&lane,&token) && api(release)(&lane,&token));
}
int main(void) {
    individual_orderings();identity_and_exhaustion();conflicts_and_failure();broadcast();
    return 0;
}
