
int main(void){
 reset();antenna_route_t original4=routes[4],original5=routes[5],original6=routes[6];
 esp32_mquickjs_wifi_radio_config_result_t r={0};esp32_mquickjs_wifi_antenna_snapshot_t a=gpio_config(4,5);
 assert(!esp32_mquickjs_wifi_antenna_write(true,&a,&r)&&live==1&&reserved==((1ULL<<4)|(1ULL<<5)));
 a=gpio_config(5,4);assert(!esp32_mquickjs_wifi_antenna_write(true,&a,&r)&&live==1);
 assert(routes[5].output==3&&routes[4].output==8);
 a=gpio_config(6,5);assert(!esp32_mquickjs_wifi_antenna_write(true,&a,&r));
 assert(antenna_route_equal(&routes[4],&original4)&&!(reserved&(1ULL<<4)));
 memset(&a,0,sizeof(a));a.gpio.gpio_cfg[3].gpio_num=127;
 assert(!esp32_mquickjs_wifi_antenna_write(true,&a,&r)&&!reserved&&!live&&!s_antenna_gpio);
 assert(antenna_route_equal(&routes[5],&original5)&&antenna_route_equal(&routes[6],&original6));
 assert(stored_gpio.gpio_cfg[3].gpio_num==127);reset();
}
