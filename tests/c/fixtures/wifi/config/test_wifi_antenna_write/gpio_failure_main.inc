
int main(void){
 for(int bad=0;bad<8;bad++){
  reset();esp32_mquickjs_wifi_antenna_snapshot_t a=gpio_config(4,5);esp32_mquickjs_wifi_radio_config_result_t r={0};
  if(bad==0)reserved=1ULL<<5;if(bad==1)routes[5].pin=1;if(bad==2)routes[5].enabled=true;
  if(bad==3)pad_available[5]=false;if(bad==4)a.gpio.gpio_cfg[1].gpio_num=4;
  if(bad==5)fail_alloc=true;if(bad==6)race_reserved=1ULL<<5;if(bad==7)a.gpio.gpio_cfg[1].gpio_num=127;
  assert(esp32_mquickjs_wifi_antenna_write(true,&a,&r)!=0&&!writes&&!live&&!s_antenna_gpio);
  assert(reserved==((bad==0||bad==6)?1ULL<<5:0));
 }
 for(int nth=1;nth<=2;nth++){
  reset();antenna_route_t before4=routes[4],before5=routes[5];fail_pin=nth;
  esp32_mquickjs_wifi_antenna_snapshot_t a=gpio_config(4,5);esp32_mquickjs_wifi_radio_config_result_t r={0};
  assert(esp32_mquickjs_wifi_antenna_write(true,&a,&r)==-72&&r.rollback_complete&&!live&&!reserved);
  assert(antenna_route_equal(&routes[4],&before4)&&antenna_route_equal(&routes[5],&before5));
 }
 reset();esp32_mquickjs_wifi_antenna_snapshot_t a=gpio_config(4,5);esp32_mquickjs_wifi_radio_config_result_t r={0};
 assert(!esp32_mquickjs_wifi_antenna_write(true,&a,&r));routes[4].output=99;int before=writes;
 assert(esp32_mquickjs_wifi_antenna_write(true,&a,&r)==ESP_ERR_INVALID_STATE&&writes==before&&routes[4].output==99);
 reset();a=gpio_config(4,5);fail_pin=1;fail_restore=4;r=(esp32_mquickjs_wifi_radio_config_result_t){0};
 assert(esp32_mquickjs_wifi_antenna_write(true,&a,&r)==-72&&r.rollback_error==-71&&!r.rollback_complete);
 assert(esp32_mquickjs_wifi_antenna_fault()==-71&&reserved==((1ULL<<4)|(1ULL<<5)));reset();
}
