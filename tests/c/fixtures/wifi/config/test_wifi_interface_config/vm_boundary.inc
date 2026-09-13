
static int writes;
static void checked_config_free(void *p){
 if(p)for(size_t i=0;i<sizeof(wifi_config_t);i++)assert(!((uint8_t*)p)[i]);
 esp32_mquickjs_memory_payload_free(p);
}
#define esp32_mquickjs_memory_payload_free checked_config_free
static int esp32_mquickjs_wifi_radio_write_interface_config(wifi_interface_t interface,
 wifi_config_t *config,esp32_mquickjs_wifi_radio_config_result_t *result){
 assert(writes++==0);assert(config->sta.password[0]);
 if(interface==WIFI_IF_STA)config->sta.listen_interval=3;
 *result=(esp32_mquickjs_wifi_radio_config_result_t){.stage="complete",.mutation_attempted=true};return 0;
}
