
int main(void){
 esp_phy_ant_gpio_config_t config={0};config.gpio_cfg[0].gpio_select=1;config.gpio_cfg[0].gpio_num=4;
 reserved=UINT64_C(1)<<4;
 assert(esp_phy_set_ant_gpio(&config)==ESP_ERR_INVALID_ARG&&!writes&&!routes);
 assert(last_mask==reserved);
 reserved=UINT64_C(1)<<2;
 assert(!esp_phy_set_ant_gpio(&config)&&writes==1&&routes==1);
 assert(last_mask==(UINT64_C(1)<<4)&&s_phy_ant_gpio_config.gpio_cfg[0].gpio_num==4);
 for(int invalid=0;invalid<4;invalid++){
  writes=routes=0;reserved=0;config.gpio_cfg[1].gpio_select=1;
  config.gpio_cfg[1].gpio_num=invalid==0?127:invalid==1?46:invalid==2?4:7;
  if(invalid==3)reserved=UINT64_C(1)<<7;
  assert(esp_phy_set_ant_gpio(&config)==ESP_ERR_INVALID_ARG&&!writes&&!routes);
 }
 assert(esp_phy_set_ant_gpio(NULL)==ESP_ERR_INVALID_ARG);
}
