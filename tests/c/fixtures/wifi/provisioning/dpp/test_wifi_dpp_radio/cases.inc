
static esp32_mquickjs_wifi_radio_lease_t dpp_owners[3];
static void setup(void){assert(!s_dpp_radio&&!worker_live);reset_vendor();memset(dpp_owners,0,sizeof(dpp_owners));
 s_radio.effective_mode=WIFI_MODE_STA;s_radio.next_operation_identity=1;
 wifi_radio_operation_lock();
 assert(!wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION,WIFI_MODE_STA,&dpp_owners[0]));
 assert(!wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA,WIFI_MODE_STA,&dpp_owners[1]));
 wifi_radio_operation_unlock();channel=6;prepares=channel_writes=closes=0;prepare_error=close_error=channel_error=0;unknown=associated=false;
 station_config=(wifi_config_t){.sta.ssid="saved",.sta.pmf_cfg.capable=true};storage=s_radio.storage=WIFI_STORAGE_FLASH;
 s_radio.storage_configured=true;s_radio.stop_required=true;config_writes=storage_writes=0;config_error=storage_error=0;
 native_started=true;s_radio.event_live=s_radio.effective_mode;hold_stop=hold_start=false;stops=starts=pmf_writes=policy_writes=0;pmf_error=start_error=0;
 power=60;power_save=WIFI_PS_MIN_MODEM;inactive[0]=60;inactive[1]=300;s_inactive_history.pending=0;}
static void pinned(void){wifi_radio_operation_lock();for(unsigned i=0;i<3;i++)if(dpp_owners[i].acquired){
 wifi_radio_release_locked(&dpp_owners[i]);assert(wifi_radio_lease_valid(&dpp_owners[i]));}wifi_radio_operation_unlock();}
int main(void){setup();esp32_mquickjs_wifi_dpp_worker_options_t o={.channels="6"};
 esp32_mquickjs_wifi_radio_operation_t token={0};esp32_mquickjs_wifi_dpp_radio_status_t status;
 o.channels[0]=0;assert(esp32_mquickjs_wifi_radio_dpp_begin(dpp_owners,false,&o,&token,&status)==ESP_ERR_INVALID_ARG&&!prepares&&!token.identity);
 memcpy(o.channels,"11",3);assert(esp32_mquickjs_wifi_radio_dpp_begin(dpp_owners,false,&o,&token,&status)==ESP_ERR_INVALID_ARG&&!prepares&&!worker_live);
 memcpy(o.channels,"6",2);assert(!esp32_mquickjs_wifi_radio_dpp_begin(dpp_owners,false,&o,&token,&status));pinned();
 /* Invalid-channel admission above already closed its unpublished worker. */
 assert(closes==1);unsigned before_stale_close=closes;
 esp32_mquickjs_wifi_radio_operation_t stale=token;stale.generation++;
 assert(esp32_mquickjs_wifi_radio_dpp_close(&stale,&status)==ESP_ERR_INVALID_STATE&&closes==before_stale_close);
 esp32_mquickjs_wifi_radio_end_operation(&token);assert(token.identity);pinned();
 esp_dpp_config_data_t row;assert(esp32_mquickjs_wifi_radio_dpp_config(&token,0,&row)==ESP_ERR_INVALID_STATE);
 assert(!esp32_mquickjs_wifi_radio_dpp_listen(&token,&status)&&channel==11);
 close_error=ESP_ERR_NOT_FINISHED;assert(esp32_mquickjs_wifi_radio_dpp_finish_capture(&token,&status)==ESP_ERR_NOT_FINISHED&&!channel_writes);pinned();
 close_error=0;channel_error=88;assert(esp32_mquickjs_wifi_radio_dpp_finish_capture(&token,&status)==88&&token.identity);pinned();
 channel_error=0;assert(!esp32_mquickjs_wifi_radio_dpp_finish_capture(&token,&status)&&channel==6&&status.channel_restored);
 unsigned writes=channel_writes;assert(!esp32_mquickjs_wifi_radio_dpp_finish_capture(&token,&status)&&channel_writes==writes);
 assert(!esp32_mquickjs_wifi_radio_dpp_config(&token,0,&row)&&row.secret[0]==1);
 assert(!esp32_mquickjs_wifi_radio_dpp_close(&token,&status)&&!token.identity&&!s_dpp_radio&&!worker_live);setup();
 /* Connect keeps the same Radio token. Native close is forbidden until the
  * exact Station generation retires, and restoration retries only its suffix. */
 assert(!esp32_mquickjs_wifi_radio_dpp_begin(dpp_owners,false,&o,&token,&status));
 assert(!esp32_mquickjs_wifi_radio_dpp_listen(&token,&status));
 assert(!esp32_mquickjs_wifi_radio_dpp_finish_capture(&token,&status));
 memset(&row,1,sizeof(row));wifi_config_t selected_config;esp32_mquickjs_wifi_dpp_auth_t selected;
 station_config.sta.pmf_cfg.capable=false;
 assert(esp32_mquickjs_wifi_radio_dpp_select(&token,&row,0,false,&selected,&selected_config,&status)==ESP_ERR_NOT_SUPPORTED&&!storage_writes);
 assert(!selected&&!selected_config.sta.ssid[0]);station_config.sta.pmf_cfg.capable=true;
 assert(!esp32_mquickjs_wifi_radio_dpp_select(&token,&row,0,false,&selected,&selected_config,&status)&&storage==WIFI_STORAGE_RAM);
 assert(!esp32_mquickjs_wifi_radio_dpp_connection_begin(&token));
 assert(esp32_mquickjs_wifi_radio_dpp_connection_begin(&token)==ESP_ERR_INVALID_STATE);
 associated=true;station_config=selected_config;channel=11;unsigned closed=closes;
 const uint8_t bssid[6]={2,3,4,5,6,7};assert(!esp32_mquickjs_wifi_radio_dpp_check_connection(&token,selected,bssid));
 assert(esp32_mquickjs_wifi_radio_dpp_close(&token,&status)==ESP_ERR_INVALID_STATE&&closes==closed);pinned();
 stale=token;stale.lease_identity++;
 assert(esp32_mquickjs_wifi_radio_dpp_connection_end(&stale)==ESP_ERR_INVALID_STATE);
 associated=false;assert(!esp32_mquickjs_wifi_radio_dpp_connection_end(&token));
 config_error=77;assert(esp32_mquickjs_wifi_radio_dpp_close(&token,&status)==77&&token.identity&&storage==WIFI_STORAGE_RAM);pinned();
 config_error=0;storage_error=88;assert(esp32_mquickjs_wifi_radio_dpp_close(&token,&status)==88&&token.identity);
 assert(!strcmp((char*)station_config.sta.ssid,"saved")&&channel==6&&status.config_restored&&!status.storage_restored);pinned();
 writes=config_writes;storage_error=0;assert(!esp32_mquickjs_wifi_radio_dpp_close(&token,&status));
 assert(config_writes==writes&&storage==WIFI_STORAGE_FLASH&&!token.identity);setup();
 /* Eligible disabled PMF: exact dpp_owners stay pinned across delayed STOP,
  * partial PMF failure, delayed START and policy/storage restoration. */
 station_config.sta.pmf_cfg.capable=false;station_config.sta.disable_wpa3_compatible_mode=true;
 assert(!esp32_mquickjs_wifi_radio_dpp_begin(dpp_owners,false,&o,&token,&status));
 assert(!esp32_mquickjs_wifi_radio_dpp_listen(&token,&status));assert(!esp32_mquickjs_wifi_radio_dpp_finish_capture(&token,&status));
 assert(!esp32_mquickjs_wifi_radio_dpp_select(&token,&row,0,false,&selected,&selected_config,&status)&&status.restore_requires_restart);
 assert(!esp32_mquickjs_wifi_radio_dpp_connection_begin(&token));station_config=selected_config;
 assert(!esp32_mquickjs_wifi_radio_dpp_connection_end(&token));hold_stop=true;
 /* A foreign or changed owner must fail before physical STOP. */
 s_radio.leases[WIFI_RADIO_MAX_LEASES-1].identity=500;
 assert(esp32_mquickjs_wifi_radio_dpp_close(&token,&status)==ESP_ERR_INVALID_STATE&&!stops);
 s_radio.leases[WIFI_RADIO_MAX_LEASES-1].identity=0;
 wifi_radio_live_lease_t *station_owner=wifi_radio_promiscuous_owner(dpp_owners[1].identity);
 station_owner->client=ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION;
 assert(esp32_mquickjs_wifi_radio_dpp_close(&token,&status)==ESP_ERR_INVALID_STATE&&!stops);
 station_owner->client=ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA;
 assert(esp32_mquickjs_wifi_radio_dpp_close(&token,&status)==ESP_ERR_NOT_FINISHED&&stops==1&&!config_writes);pinned();
 assert(esp32_mquickjs_wifi_radio_dpp_close(&token,&status)==ESP_ERR_NOT_FINISHED&&stops==1);pinned();
 hold_stop=false;pmf_error=77;
 assert(esp32_mquickjs_wifi_radio_dpp_close(&token,&status)==77&&status.restore_stopped&&config_writes==1&&pmf_writes==1&&!starts);pinned();
 pmf_error=0;hold_start=true;
 assert(esp32_mquickjs_wifi_radio_dpp_close(&token,&status)==ESP_ERR_NOT_FINISHED&&starts==1&&config_writes==1&&pmf_writes==1);pinned();
 assert(esp32_mquickjs_wifi_radio_dpp_close(&token,&status)==ESP_ERR_NOT_FINISHED&&starts==1);hold_start=false;
 storage_error=88;
 assert(esp32_mquickjs_wifi_radio_dpp_close(&token,&status)==88&&status.restore_started&&token.identity);pinned();
 unsigned restored_policies=policy_writes;
 assert(esp32_mquickjs_wifi_radio_dpp_close(&token,&status)==88&&policy_writes==restored_policies&&stops==1&&starts==1);
 storage_error=0;assert(!esp32_mquickjs_wifi_radio_dpp_close(&token,&status)&&!token.identity);
 assert(native_started&&!station_config.sta.pmf_cfg.capable&&storage==WIFI_STORAGE_FLASH&&channel==6);
 assert(power==60&&power_save==WIFI_PS_MIN_MODEM&&inactive[0]==60&&stops==1&&starts==1);setup();
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
 s_radio.effective_mode=WIFI_MODE_APSTA;wifi_radio_operation_lock();
 assert(!wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP,WIFI_MODE_AP,&dpp_owners[2]));wifi_radio_operation_unlock();
 station_config.sta.pmf_cfg.capable=false;station_config.sta.disable_wpa3_compatible_mode=true;
 assert(!esp32_mquickjs_wifi_radio_dpp_begin(dpp_owners,true,&o,&token,&status));
 assert(!esp32_mquickjs_wifi_radio_dpp_listen(&token,&status));assert(!esp32_mquickjs_wifi_radio_dpp_finish_capture(&token,&status));
 assert(esp32_mquickjs_wifi_radio_dpp_select(&token,&row,0,false,&selected,&selected_config,&status)==ESP_ERR_INVALID_STATE&&!storage_writes);
 assert(!esp32_mquickjs_wifi_radio_dpp_select(&token,&row,0,true,&selected,&selected_config,&status));
 assert(!esp32_mquickjs_wifi_radio_dpp_connection_begin(&token));station_config=selected_config;
 assert(!esp32_mquickjs_wifi_radio_dpp_connection_end(&token));hold_stop=true;
 assert(esp32_mquickjs_wifi_radio_dpp_close(&token,&status)==ESP_ERR_NOT_FINISHED);pinned();
 hold_stop=false;assert(!esp32_mquickjs_wifi_radio_dpp_close(&token,&status)&&inactive[1]==300&&stops==1&&starts==1);setup();
#endif
 /* Failed prepare never authorizes channel writes over a foreign owner. */
 prepare_error=77;assert(esp32_mquickjs_wifi_radio_dpp_begin(dpp_owners,false,&o,&token,&status)==77&&token.identity);pinned();
 channel=11;assert(!esp32_mquickjs_wifi_radio_dpp_close(&token,&status)&&channel==11&&!channel_writes);setup();
 assert(!esp32_mquickjs_wifi_radio_dpp_begin(dpp_owners,false,&o,&token,&status));unknown=true;
 assert(esp32_mquickjs_wifi_radio_dpp_close(&token,&status)==ESP_ERR_INVALID_STATE&&token.identity);pinned();
 unknown=false;assert(!esp32_mquickjs_wifi_radio_dpp_close(&token,&status));setup();
 /* An SDK START error retains the original fault and every helper owner.
  * Removing the injected error is not authorization to repeat the mutation. */
 station_config.sta.pmf_cfg.capable=false;station_config.sta.disable_wpa3_compatible_mode=true;
 assert(!esp32_mquickjs_wifi_radio_dpp_begin(dpp_owners,false,&o,&token,&status));
 assert(!esp32_mquickjs_wifi_radio_dpp_listen(&token,&status));assert(!esp32_mquickjs_wifi_radio_dpp_finish_capture(&token,&status));
 assert(!esp32_mquickjs_wifi_radio_dpp_select(&token,&row,0,false,&selected,&selected_config,&status));
 assert(!esp32_mquickjs_wifi_radio_dpp_connection_begin(&token));station_config=selected_config;
 assert(!esp32_mquickjs_wifi_radio_dpp_connection_end(&token));start_error=99;
 assert(esp32_mquickjs_wifi_radio_dpp_close(&token,&status)==99&&stops==1&&starts==1&&token.identity);
 assert(s_radio.driver_state==ESP32_MQUICKJS_WIFI_RADIO_FAULTED&&s_radio.fault_error==99&&
     !strcmp(s_radio.fault_stage,"dpp-restore-start")&&!status.restore_started);pinned();
 start_error=0;
 assert(esp32_mquickjs_wifi_radio_dpp_close(&token,&status)==99&&starts==1&&stops==1&&storage==WIFI_STORAGE_RAM);pinned();
 stale=token;stale.generation++;
 assert(esp32_mquickjs_wifi_radio_dpp_recover(&stale,&status)==ESP_ERR_INVALID_STATE);
 s_radio.fault_stage="another-source";
 assert(esp32_mquickjs_wifi_radio_dpp_recover(&token,&status)==ESP_ERR_INVALID_STATE);
 s_radio.fault_stage="dpp-restore-start";unknown=true;
 assert(esp32_mquickjs_wifi_radio_dpp_recover(&token,&status)==ESP_ERR_INVALID_STATE);unknown=false;
 s_dpp_radio->restore_recovery_attempts=UINT32_MAX;
 assert(esp32_mquickjs_wifi_radio_dpp_recover(&token,&status)==ESP_ERR_NO_MEM&&!s_dpp_radio->restore_recovery_pending);
 s_dpp_radio->restore_recovery_attempts=0;
 assert(!esp32_mquickjs_wifi_radio_dpp_recover(&token,&status)&&status.restore_recovery_pending&&status.restore_recovery_attempts==1);
 assert(!esp32_mquickjs_wifi_radio_dpp_recover(&token,&status)&&status.restore_recovery_attempts==1&&stops==1&&starts==1);
 hold_stop=true;unsigned before_recovery_ap_reads=ap_info_reads;
 assert(esp32_mquickjs_wifi_radio_dpp_close(&token,&status)==ESP_ERR_NOT_FINISHED&&stops==2&&starts==1&&s_radio.fault_error==99);pinned();
 assert(ap_info_reads==before_recovery_ap_reads); /* Failed START is retired by STOP/fence, not a live-link query. */
 assert(!esp32_mquickjs_wifi_radio_dpp_recover(&token,&status)&&status.restore_recovery_attempts==1);
 hold_stop=false;start_error=98;
 assert(esp32_mquickjs_wifi_radio_dpp_close(&token,&status)==98&&stops==2&&starts==2&&s_radio.fault_error==98);
 assert(!status.restore_recovery_pending&&status.restore_start_failed&&status.restore_start_error==98);pinned();
 start_error=0;assert(esp32_mquickjs_wifi_radio_dpp_close(&token,&status)==98&&starts==2);
 assert(!esp32_mquickjs_wifi_radio_dpp_recover(&token,&status)&&status.restore_recovery_attempts==2);
 assert(!esp32_mquickjs_wifi_radio_dpp_close(&token,&status)&&!token.identity&&stops==3&&starts==3);
 assert(!s_radio.fault_stage&&!s_radio.cleanup_stage&&native_started&&storage==WIFI_STORAGE_FLASH&&
     !station_config.sta.pmf_cfg.capable&&power==60&&inactive[0]==60);
 assert(!critical&&!locks);return 0;
}
