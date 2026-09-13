
ETSTimer channel_timer,Restart_delay_timer,TouchRestart_ht20_timer,TouchRestart_ht40_timer,
 TouchUdpTimer,KissRes_ht20_timer,weixin_timer,restart_ht20_timer,restart_ht40_timer;
int esp_wifi_ipc_internal(sc_ipc_config_t *config,bool sync){assert(!locked && sync && !config->arg_size);if(ipc_error)return ipc_error;return config->fn(config->arg);}
int smartconfig_get_status(void){assert(!locked);return native_status;}
int esp_smartconfig_set_type_local(void *type){assert(!locked && (uintptr_t)type<=3);return 0;}
int esp_smartconfig_fast_mode_local(void *fast){assert(!locked && (uintptr_t)fast<=1);return 0;}
int esp_esptouch_set_timeout_local(void *seconds){assert(!locked && (uintptr_t)seconds>=15 && (uintptr_t)seconds<=255);return 0;}
int esp_smartconfig_start_local(void *config){smartconfig_start_config_t *c=config;assert(!locked && !c->enable_log && event_owned && timer_owned && ack_owner);if(c->esp_touch_v2_enable_crypt)assert(strlen(c->esp_touch_v2_key)==16);start_calls++;native_status=1;promiscuous=true;if(oom_on_start)native_allocation_error=ESP_ERR_NO_MEM;return start_error;}
int esp_smartconfig_stop_local(void *unused){assert(!unused && !locked && timer_closed && event_owned && !drain_error && !promiscuous);stop_calls++;if(!stop_error)native_status=0;return stop_error;}
int sc_aes_crypt_init(void){assert(!locked && !native_status);fence_calls++;return 0;}
