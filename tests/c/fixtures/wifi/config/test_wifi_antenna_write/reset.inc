
static void reset(void){
 assert(!lock_depth);esp32_mquickjs_memory_payload_free(s_antenna_gpio);s_antenna_gpio=NULL;
 memset(&stored_gpio,0,sizeof(stored_gpio));memset(&stored_config,0,sizeof(stored_config));memset(&GPIO,0,sizeof(GPIO));
 for(unsigned i=0;i<32;i++){routes[i]=(antenna_route_t){.mux=PIN_FUNC_GPIO,.output=SIG_GPIO_OUT_IDX,.rtc=100+i};pad_available[i]=true;}
 reserved=race_reserved=0;atomic_store(&s_antenna_fault,0);ble_closed=true;awake=fail_alloc=false;
 ieee154=controller=writes=reads=pin_writes=fail_pin=fail_set=fail_get=fail_rollback=0;fail_restore=-1;
}
static esp32_mquickjs_wifi_antenna_snapshot_t gpio_config(unsigned a,unsigned b){
 esp32_mquickjs_wifi_antenna_snapshot_t r={0};r.gpio.gpio_cfg[0].gpio_select=1;r.gpio.gpio_cfg[0].gpio_num=a;
 r.gpio.gpio_cfg[1].gpio_select=1;r.gpio.gpio_cfg[1].gpio_num=b;return r;
}
