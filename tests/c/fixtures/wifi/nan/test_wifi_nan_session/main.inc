
int main(void) {
    wifi_nan_sync_config_t config={.op_channel=6};
    esp32_mquickjs_wifi_nan_session_t *s=NULL;
    esp32_mquickjs_wifi_nan_session_status_t status;
    assert(esp32_mquickjs_wifi_nan_open_runtime()==ESP_OK);
    config.op_channel=0;
    assert(esp32_mquickjs_wifi_nan_session_create(&config,1000,&s)==ESP_ERR_INVALID_ARG&&!allocation_calls);
    config.op_channel=6;config.group_mgmt_prot=true;
    assert(esp32_mquickjs_wifi_nan_session_create(&config,1000,&s)==ESP_ERR_NOT_SUPPORTED&&!allocation_calls);
    config.group_mgmt_prot=false;fail_allocation=1;
    assert(esp32_mquickjs_wifi_nan_session_create(&config,1000,&s)==ESP_ERR_NO_MEM&&!s&&!allocations&&!s_nan_handles);
    fail_allocation=0;
    /* Preserve the SDK channel field instead of inferring a 2.4 GHz-only
     * target. Radio/SDK admission, tested separately, decides RF support. */
    config.op_channel=149;
    assert(esp32_mquickjs_wifi_nan_session_create(&config,1000,&s)==ESP_OK);
    esp32_mquickjs_wifi_nan_session_release(s);s=NULL;config.op_channel=6;

    /* Full worker queue: close before native dispatch, then drain without START. */
    assert(esp32_mquickjs_wifi_nan_session_create(&config,1000,&s)==ESP_OK);
    assert(esp32_mquickjs_wifi_nan_session_activate(s)==ESP_OK);
    queue_full=true;assert(!esp32_mquickjs_wifi_nan_service()&&!queued&&allocations==1);
    assert(esp32_mquickjs_wifi_nan_session_status(s,&status)&&!status.worker_busy&&status.cleanup_error==ESP_ERR_NO_MEM);
    esp32_mquickjs_wifi_nan_session_close(s,false);esp32_mquickjs_wifi_nan_session_release(s);s=NULL;
    queue_full=false;assert(esp32_mquickjs_wifi_nan_service());run_queued(NULL);
    assert(!begins&&!closes&&!allocations&&!s_nan_active&&!s_nan_workers);

    /* Real production worker is entered while another thread times out/closes
     * and drops the public root. A delayed Radio cleanup keeps native storage. */
    assert(esp32_mquickjs_wifi_nan_session_create(&config,1000,&s)==ESP_OK);
    assert(esp32_mquickjs_wifi_nan_session_activate(s)==ESP_OK);
    pause_begin=true;close_error=ESP_ERR_TIMEOUT;
    assert(esp32_mquickjs_wifi_nan_service());pthread_t worker;
    assert(!pthread_create(&worker,NULL,run_queued,NULL));
    pthread_mutex_lock(&gate);while(!begin_entered)pthread_cond_wait(&condition,&gate);pthread_mutex_unlock(&gate);
    esp32_mquickjs_wifi_nan_session_close(s,true);
    assert(esp32_mquickjs_wifi_nan_session_status(s,&status)&&status.timed_out&&status.closing&&!status.ready);
    esp32_mquickjs_wifi_nan_session_release(s);
    assert(allocations==1);
    pthread_mutex_lock(&gate);resume_begin=true;pthread_cond_broadcast(&condition);pthread_mutex_unlock(&gate);
    assert(!pthread_join(worker,NULL));
    assert(allocations==1&&s_nan_active==s&&s->references==1&&s->operation.identity&&begins==1&&closes==1);
    esp32_mquickjs_wifi_nan_global_status_t global;
    esp32_mquickjs_wifi_nan_global_status(&global);
    assert(global.active&&global.handles==1&&!global.workers&&global.session.closing&&global.session.timed_out);
    assert(esp32_mquickjs_wifi_nan_open_runtime()==ESP_ERR_INVALID_STATE);
    assert(!esp32_mquickjs_wifi_nan_prepare_runtime_destroy());
    assert(queued);close_error=ESP_OK;run_queued(NULL);s=NULL;
    assert(!allocations&&!s_nan_active&&!s_nan_workers&&begins==1&&closes==2);
    assert(esp32_mquickjs_wifi_nan_prepare_runtime_destroy());
    assert(esp32_mquickjs_wifi_nan_open_runtime()==ESP_OK);

    /* Ready Sessions keep the driver owner after startup; teardown owns close. */
    pause_begin=false;
    assert(esp32_mquickjs_wifi_nan_session_create(&config,1000,&s)==ESP_OK);
    assert(esp32_mquickjs_wifi_nan_session_activate(s)==ESP_OK);
    assert(esp32_mquickjs_wifi_nan_service());run_queued(NULL);
    assert(esp32_mquickjs_wifi_nan_session_status(s,&status)&&status.ready&&!status.retired);
    assert(!esp32_mquickjs_wifi_nan_prepare_runtime_destroy());run_queued(NULL);
    assert(esp32_mquickjs_wifi_nan_prepare_runtime_destroy());
    assert(esp32_mquickjs_wifi_nan_open_runtime()==ESP_ERR_INVALID_STATE); /* public handle remains */
    esp32_mquickjs_wifi_nan_session_release(s);s=NULL;
    assert(esp32_mquickjs_wifi_nan_open_runtime()==ESP_OK&&!allocations);

    /* No runtime poll occurs during this late START: the worker must still
     * enforce the original deadline before publishing readiness. */
    start_advance_us=2000000;
    assert(esp32_mquickjs_wifi_nan_session_create(&config,1000,&s)==ESP_OK);
    assert(esp32_mquickjs_wifi_nan_session_activate(s)==ESP_OK);
    assert(esp32_mquickjs_wifi_nan_service());run_queued(NULL);
    assert(esp32_mquickjs_wifi_nan_session_status(s,&status)&&status.timed_out&&status.retired&&!status.ready);
    esp32_mquickjs_wifi_nan_session_release(s);s=NULL;start_advance_us=0;

    /* Failed native admission cannot stop another owner's Radio. */
    unsigned closed=closes;begin_error=ESP_ERR_INVALID_STATE;
    assert(esp32_mquickjs_wifi_nan_session_create(&config,1000,&s)==ESP_OK);
    assert(esp32_mquickjs_wifi_nan_session_activate(s)==ESP_OK);
    assert(esp32_mquickjs_wifi_nan_service());run_queued(NULL);
    assert(esp32_mquickjs_wifi_nan_session_status(s,&status)&&status.retired&&status.error==begin_error&&closes==closed);
    esp32_mquickjs_wifi_nan_session_release(s);s=NULL;
    s_nan_next_identity=UINT32_MAX;
    assert(esp32_mquickjs_wifi_nan_session_create(&config,1000,&s)==ESP_OK&&s->status.identity==UINT32_MAX);
    esp32_mquickjs_wifi_nan_session_release(s);s=NULL;
    assert(esp32_mquickjs_wifi_nan_session_create(&config,1000,&s)==ESP_ERR_NO_MEM&&!s&&!allocations);
}
