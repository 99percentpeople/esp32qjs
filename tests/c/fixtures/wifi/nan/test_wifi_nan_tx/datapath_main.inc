
static void *action(const uint8_t peer[6],uint8_t id){
 void *b=esp32qjs_wifi_nan_alloc_action(1,(uintptr_t)peer,3,4,5);if(!b)return NULL;
 uint8_t*m;memcpy(&m,(uint8_t*)b+56,sizeof(m));m[8]=id;m[9]=7;
 esp32_mquickjs_wifi_nan_tx_submitting(b);return b;
}
int main(void){
 uint8_t peer[6]={2,3,4,5,6,7},other[6]={2,3,4,5,6,8};
 assert(!esp32_mquickjs_wifi_nan_tx_open());
 uint32_t context=0;assert(!esp32_mquickjs_wifi_nan_tx_message_begin(7,&context));
 /* NDP IDs do not alias discovery service IDs or the active message lane. */
 void*b=action(peer,7);assert(b&&!s_nan_tx->message.status.ticket);
 assert(!esp32_mquickjs_wifi_nan_tx_datapath_drained(7,peer));
 assert(esp32_mquickjs_wifi_nan_tx_datapath_drained(8,peer));
 assert(esp32_mquickjs_wifi_nan_tx_datapath_drained(7,other));
 uint32_t callback=esp32_mquickjs_wifi_nan_tx_callback_enter(b);assert(callback);
 uint32_t freed=esp32_mquickjs_wifi_nan_tx_recycling(b);assert(freed==callback);
 esp32_mquickjs_wifi_nan_tx_recycled(freed);
 assert(!esp32_mquickjs_wifi_nan_tx_datapath_drained(7,peer));
 /* A new frame can reuse the EB address while the previous callback exits. */
 next_buffer=0;void*newer=action(peer,7);assert(newer==b);
 esp32_mquickjs_wifi_nan_tx_callback_leave(callback,true);
 assert(!esp32_mquickjs_wifi_nan_tx_datapath_drained(7,peer));
 esp32_mquickjs_wifi_nan_tx_recycled(freed);
 assert(!esp32_mquickjs_wifi_nan_tx_datapath_drained(7,peer));
 uint32_t newer_ticket=esp32_mquickjs_wifi_nan_tx_recycling(newer);assert(newer_ticket!=freed);
 esp32_mquickjs_wifi_nan_tx_recycled(newer_ticket);
 assert(esp32_mquickjs_wifi_nan_tx_datapath_drained(7,peer));
 fail_native=true;assert(!action(peer,7));fail_native=false;
 assert(esp32_mquickjs_wifi_nan_tx_datapath_drained(7,peer));
 assert(!esp32_mquickjs_wifi_nan_tx_message_release(7));
 void*frames[32];for(unsigned i=0;i<32;++i){frames[i]=action(peer,7);assert(frames[i]);}
 assert(!esp32qjs_wifi_nan_alloc_sdf(1,2,3,4,5));
 assert(esp32_mquickjs_wifi_nan_tx_close()==ESP_ERR_TIMEOUT);
 assert(!action(peer,7));
 for(unsigned i=0;i<32;++i){uint32_t t=esp32_mquickjs_wifi_nan_tx_recycling(frames[i]);assert(t);esp32_mquickjs_wifi_nan_tx_recycled(t);}
 assert(!esp32_mquickjs_wifi_nan_tx_close()&&!heap_live&&!locked);
}
