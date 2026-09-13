
int main(void){
 assert(esp32_mquickjs_wifi_nan_tx_open()==ESP_OK);
 assert(esp32_mquickjs_wifi_nan_tx_pairing_enter(41,7,true)==ESP_OK);
 assert(esp32_mquickjs_wifi_nan_tx_pairing_enter(42,8,true)==ESP_ERR_INVALID_STATE);
 void *first=buffers[55];
 assert(__wrap_ieee80211_mgmt_output((void*)3,first,0xb0)==ESP_OK);
 esp32_mquickjs_wifi_nan_tx_pairing_leave(42); /* stale leave cannot clear scope */
 assert(!esp32_mquickjs_wifi_nan_tx_pairing_drained(41));
 esp32_mquickjs_wifi_nan_tx_pairing_leave(41);
 assert(!esp32_mquickjs_wifi_nan_tx_pairing_drained(41));
 assert(esp32_mquickjs_wifi_nan_tx_pairing_drained(42));
 assert(esp32_mquickjs_wifi_nan_tx_pairing_drained(0));
 assert(!esp32_mquickjs_wifi_nan_tx_service_drained(7));
 assert(esp32_mquickjs_wifi_nan_tx_service_drained(8));
 uint32_t old=esp32_mquickjs_wifi_nan_tx_recycling(first);
 assert(old);esp32_mquickjs_wifi_nan_tx_recycled(old);
 assert(esp32_mquickjs_wifi_nan_tx_pairing_drained(41));
 /* A nested recycler cannot end the producer scope or reuse its entry. */
 assert(esp32_mquickjs_wifi_nan_tx_pairing_enter(41,7,true)==ESP_OK);
 recycle_in_output=true;
 assert(__wrap_ieee80211_mgmt_output((void*)3,first,0xb0)==ESP_OK);
 esp32_mquickjs_wifi_nan_tx_pairing_leave(41);
 assert(esp32_mquickjs_wifi_nan_tx_pairing_drained(41));
 recycle_in_output=false;
 assert(esp32_mquickjs_wifi_nan_tx_pairing_enter(42,7,false)==ESP_OK);
 void *followup=esp32qjs_wifi_nan_alloc_sdf(1,2,3,4,5);
 assert(followup);uint32_t callback=esp32_mquickjs_wifi_nan_tx_callback_enter(followup);
 assert(callback);esp32_mquickjs_wifi_nan_tx_pairing_leave(42);
 ic_ebuf_recycle_tx(followup);
 assert(!esp32_mquickjs_wifi_nan_tx_pairing_drained(42));
 esp32_mquickjs_wifi_nan_tx_callback_leave(callback,true);
 assert(esp32_mquickjs_wifi_nan_tx_pairing_drained(42));
 esp_err_t result_error;int64_t completed;
 assert(esp32_mquickjs_wifi_nan_tx_pairing_settled(42,&result_error,&completed)&&!result_error&&completed==100);
 assert(esp32_mquickjs_wifi_nan_tx_pairing_enter(44,7,false)==ESP_OK);
 followup=esp32qjs_wifi_nan_alloc_sdf(1,2,3,4,5);assert(followup);
 esp32_mquickjs_wifi_nan_tx_pairing_leave(44);ic_ebuf_recycle_tx(followup);
 assert(esp32_mquickjs_wifi_nan_tx_pairing_drained(44));
 assert(!esp32_mquickjs_wifi_nan_tx_pairing_settled(44,&result_error,&completed));
 assert(esp32_mquickjs_wifi_nan_tx_pairing_enter(45,7,false)==ESP_OK);
 followup=esp32qjs_wifi_nan_alloc_sdf(1,2,3,4,5);assert(followup);
 callback=esp32_mquickjs_wifi_nan_tx_callback_enter(followup);assert(callback);
 esp32_mquickjs_wifi_nan_tx_pairing_leave(45);ic_ebuf_recycle_tx(followup);
 esp32_mquickjs_wifi_nan_tx_callback_leave(callback,false);
 assert(esp32_mquickjs_wifi_nan_tx_pairing_settled(45,&result_error,&completed)&&result_error==ESP_FAIL);
 esp32_mquickjs_wifi_nan_tx_callback_leave(callback,true);
 assert(esp32_mquickjs_wifi_nan_tx_pairing_settled(45,&result_error,&completed)&&result_error==ESP_FAIL);
 /* Auth cannot bypass the same 32-slot bound or send an untracked frame. */
 assert(esp32_mquickjs_wifi_nan_tx_pairing_enter(43,7,true)==ESP_OK);
 for(unsigned i=0;i<NAN_TX_CAPACITY;i++)assert(!__wrap_ieee80211_mgmt_output((void*)3,buffers[i],0xb0));
 unsigned sent=pairing_outputs,recycled=data_recycles;
 assert(__wrap_ieee80211_mgmt_output((void*)3,buffers[50],0xb0)==ESP_ERR_NO_MEM);
 assert(pairing_outputs==sent&&data_recycles==recycled+1);
 esp32_mquickjs_wifi_nan_tx_pairing_leave(43);
 esp32_mquickjs_wifi_nan_tx_recycled(old); /* old ticket cannot retire a reused address */
 assert(!esp32_mquickjs_wifi_nan_tx_pairing_drained(43));
 assert(esp32_mquickjs_wifi_nan_tx_close()==ESP_ERR_TIMEOUT);
 for(unsigned i=0;i<NAN_TX_CAPACITY;i++)ic_ebuf_recycle_tx(buffers[i]);
 assert(esp32_mquickjs_wifi_nan_tx_close()==ESP_OK&&!heap_live);
 return 0;
}
