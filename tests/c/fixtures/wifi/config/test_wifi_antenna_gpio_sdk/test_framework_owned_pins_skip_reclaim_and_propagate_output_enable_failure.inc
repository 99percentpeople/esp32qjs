
int main(void){
 esp_phy_ant_gpio_config_t config={0};
 for(unsigned i=0;i<4;i++){config.gpio_cfg[i].gpio_select=1;config.gpio_cfg[i].gpio_num=4+i;reserved|=UINT64_C(1)<<(4+i);}
 assert(esp_phy_set_ant_gpio(&config)==ESP_ERR_INVALID_ARG&&!writes&&!routes);
 for(int nth=1;nth<=4;nth++){
  writes=routes=enables=0;fail_enable=nth;memset(&s_phy_ant_gpio_config,0,sizeof(s_phy_ant_gpio_config));
  assert(esp32qjs_phy_set_ant_gpio_owned(&config,reserved)==-78&&writes==nth&&enables==nth&&routes==nth-1);
  for(unsigned i=0;i<4;i++)assert(!s_phy_ant_gpio_config.gpio_cfg[i].gpio_select);
 }
 writes=routes=enables=fail_enable=0;
 assert(!esp32qjs_phy_set_ant_gpio_owned(&config,reserved)&&enables==4&&routes==4);
 memset(&config,0,sizeof(config));esp32qjs_phy_ant_gpio_restore_config(&config);
 assert(!memcmp(&config,&s_phy_ant_gpio_config,sizeof(config)));
}
