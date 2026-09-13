
int main(void){
 esp32_mquickjs_wifi_radio_config_result_t r={0};esp32_mquickjs_wifi_antenna_snapshot_t wanted={0};
 wanted.config.rx_ant_mode=ESP_PHY_ANT_MODE_AUTO;wanted.config.enabled_ant0=15;
 for(int state=0;state<4;state++){
  reset();if(state==0)awake=true;if(state==1)ble_closed=false;if(state==2)controller=1;if(state==3)ieee154=1;
  assert(esp32_mquickjs_wifi_antenna_write(false,&wanted,&r)==ESP_ERR_INVALID_STATE&&!writes&&!reads);
 }
 reset();assert(!esp32_mquickjs_wifi_antenna_write(false,&wanted,&r)&&writes==1&&antenna_config_equal(&stored_config,&wanted.config));
 reset();fail_get=2;r=(esp32_mquickjs_wifi_radio_config_result_t){0};
 assert(esp32_mquickjs_wifi_antenna_write(false,&wanted,&r)==-61&&writes==2&&r.rollback_complete&&!esp32_mquickjs_wifi_antenna_fault());
 assert(stored_config.rx_ant_mode==ESP_PHY_ANT_MODE_ANT0);
 reset();fail_set=1;fail_rollback=2;r=(esp32_mquickjs_wifi_radio_config_result_t){0};
 assert(esp32_mquickjs_wifi_antenna_write(false,&wanted,&r)==-62&&r.rollback_error==-63&&!r.rollback_complete);
 assert(esp32_mquickjs_wifi_antenna_fault()==-63);int before=writes;
 assert(esp32_mquickjs_wifi_antenna_write(false,&wanted,&r)==-63&&writes==before);reset();
}
