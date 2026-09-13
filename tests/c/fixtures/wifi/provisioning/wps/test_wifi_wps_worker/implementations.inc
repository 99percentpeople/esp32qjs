
static wps_ipc_config_t pending;
int esp_wifi_ipc_internal(wps_ipc_config_t *config,bool sync) {
    assert(sync && !config->arg_size && config->arg==allocation);
    if(ipc_mode==1)return ESP_ERR_NO_MEM;
    if(ipc_mode==2) {pending=*config;return 909;}
    return config->fn(config->arg);
}
static void fire_timer(void) {
    assert(timer_exists);timer_args.callback(timer_args.arg);timer_returned=true;
}
static void reset_boundaries(void) {
    assert(!allocations && !timer_exists);
    start_calls=stop_calls=retire_calls=release_calls=copy_calls=commit_calls=0;
    creates=starts=stops=deletes=0;
    ipc_mode=create_error=start_error=stop_error=delete_error=release_error=0;
    timer_returned=timer_deleted=false;memset(&native,0,sizeof(native));callback_revision=0;
}
