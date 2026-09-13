
static unsigned pairing_outputs;
static bool recycle_in_output;
bool current_task_is_wifi_task(void){return true;}
int __real_ieee80211_mgmt_output(void *node,void *buffer,uintptr_t subtype){
 assert(!locked&&node==(void*)3&&subtype==0xb0);++pairing_outputs;
 if(recycle_in_output){
  ic_ebuf_recycle_tx(buffer);
  assert(!esp32_mquickjs_wifi_nan_tx_pairing_drained(41));
 }
 return ESP_OK;
}
