
static void reset(void) {
    memset(&s_espnow_session,0,sizeof(s_espnow_session));memset(stages,0,sizeof(stages));
    s_espnow_session.lifecycle=ESPNOW_LIFECYCLE_ACTIVE;s_espnow_session.generation=7;s_espnow_session.runtime=(void *)1;
    s_espnow_session.radio_lease.acquired=true;s_espnow_session.event_queue=&queue_value;s_espnow_session.event_queue_retained=true;
    s_espnow_session.tx_task=(void *)2;s_espnow_session.tx_task_exited=true;
    s_espnow_session.receive_callback_registered=s_espnow_session.send_callback_registered=s_espnow_session.now_initialized=s_espnow_session.power_save_enabled=true;
    s_espnow_session.power_save_cleanup_required=true;s_espnow_session.interval_token.identity=1;
    failed_stage=resets=queue_releases=task_deletes=native_deinits=reaps=root_deletes=disposals=retires=frees=0;
    queue_closed=accept_worker=queued=false;thrown_code=NULL;
}
static esp32_mquickjs_future_driver_state_t *close_state(void) {
    esp32_mquickjs_future_driver_state_t *s=calloc(1,sizeof(*s));assert(s);
    s->operation=ESPNOW_OPERATION_CLOSE_SESSION;s->generation=7;s->event_queue_rooted=s->owner_rooted=true;return s;
}
int main(void) {
    for(unsigned failure=1;failure<=6;++failure) {
        reset();failed_stage=failure;esp32_mquickjs_future_driver_state_t *s=close_state();
        s_espnow_session.active_close=s;s_espnow_session.cleanup_scheduled=true;
        espnow_close_worker(&s_espnow_session);
        assert(s->completed && !s->result_bool && s->err==(failure==6 ? ESP_ERR_INVALID_STATE : 70+(int)failure));
        assert(s_espnow_session.lifecycle==ESPNOW_LIFECYCLE_CLOSING && !resets && !queue_releases && s_espnow_session.radio_lease.acquired);
        assert(!s_espnow_session.cleanup_scheduled && !s_espnow_session.active_close);
        assert(espnow_control_finish(NULL,s)==-s->err && !strcmp(thrown_code,"ESPNOW_CLEANUP_PENDING"));
        espnow_control_destroy(s);assert(!disposals && !retires && root_deletes==2 && frees==1 && reaps>=1);
        unsigned before[7];memcpy(before,stages,sizeof(before));failed_stage=0;
        espnow_close_native(&s_espnow_session);
        assert(s_espnow_session.lifecycle==ESPNOW_LIFECYCLE_CLOSED && resets==1 && queue_releases==1 && task_deletes==1 && native_deinits==1);
        for(unsigned i=1;i<failure;++i)assert(stages[i]==before[i]);
        assert(stages[failure]==before[failure]+1);
        s=close_state();s->result_bool=true;assert(espnow_control_finish(NULL,s)==1);espnow_control_destroy(s);assert(disposals==1 && retires==1);
    }
    reset();s_espnow_session.callbacks_active=1;s_espnow_session.cleanup_scheduled=true;
    espnow_close_worker(&s_espnow_session);assert(resets==1 && stages[1]==1 && stages[2]==1 && native_deinits==1);
    reset();s_espnow_session.cleanup_scheduled=true;espnow_close_native(&s_espnow_session);assert(!stages[1] && !resets); /* Existing cleaner owns every suffix. */
    s_espnow_session.cleanup_scheduled=false;failed_stage=5;espnow_close_native(&s_espnow_session);
    assert(!queued && !s_espnow_session.cleanup_scheduled && reaps==1 && !resets && s_espnow_session.now_initialized);
    accept_worker=true;assert(espnow_schedule_background_close(&s_espnow_session) && queued && s_espnow_session.cleanup_scheduled);
    unsigned before=stages[5];espnow_close_native(&s_espnow_session);assert(stages[5]==before);
    failed_stage=0;espnow_close_worker(&s_espnow_session);assert(resets==1 && queue_releases==1 && !s_espnow_session.cleanup_scheduled);
    assert(espnow_closed_and_quiescent(&s_espnow_session));
    s_espnow_session.cleanup_scheduled=true;assert(!espnow_closed_and_quiescent(&s_espnow_session));s_espnow_session.cleanup_scheduled=false;
    s_espnow_session.reaper_registered=true;assert(!espnow_closed_and_quiescent(&s_espnow_session));s_espnow_session.reaper_registered=false;
    esp32_mquickjs_future_driver_state_t old_close={0};s_espnow_session.active_close=&old_close;assert(!espnow_closed_and_quiescent(&s_espnow_session));
    for(unsigned failure=1;failure<=5;++failure) {
        if(failure==3 || failure==4)continue;
        reset();s_espnow_session.cleanup_scheduled=true;failed_stage=failure;
        assert(espnow_cleanup_failed_restore(&s_espnow_session)==70+(int)failure);
        assert(s_espnow_session.now_initialized && s_espnow_session.radio_lease.acquired && s_espnow_session.interval_token.identity);
        assert(failure!=1 || s_espnow_session.receive_callback_registered);
        assert(failure!=2 || s_espnow_session.send_callback_registered);
        failed_stage=0;
        assert(espnow_cleanup_failed_restore(&s_espnow_session)==ESP_OK && !s_espnow_session.now_initialized);
        const char *stage=NULL;
        assert(espnow_restore_power_save(&s_espnow_session,&stage)==ESP_OK && !stages[3] && stages[4]==1);
    }
    return 0;
}
