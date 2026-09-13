
int main(void){
 esp_phy_ant_gpio_config_t config={0};
 for(unsigned i=0;i<4;i++){config.gpio_cfg[i].gpio_select=1;config.gpio_cfg[i].gpio_num=4+i;}
 for(int nth=1;nth<=4;nth++){
  writes=routes=0;fail_write=nth;memset(&s_phy_ant_gpio_config,0,sizeof(s_phy_ant_gpio_config));
  assert(esp_phy_set_ant_gpio(&config)==-77&&writes==nth&&routes==nth-1);
  for(unsigned i=0;i<4;i++)assert(!s_phy_ant_gpio_config.gpio_cfg[i].gpio_select);
 }
 writes=routes=fail_write=0;
 assert(!esp_phy_set_ant_gpio(&config)&&writes==4&&routes==4);
 assert(!memcmp(&config,&s_phy_ant_gpio_config,sizeof(config)));
}
