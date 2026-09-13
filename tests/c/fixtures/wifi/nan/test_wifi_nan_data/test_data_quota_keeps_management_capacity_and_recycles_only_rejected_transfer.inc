
int main(void){
 assert(!esp32_mquickjs_wifi_nan_tx_open());
 uint8_t ndl[580]={2,3,4,5,6,7};data_bind(ndl,9);
 timer_node=(void*)1;timer_ndl=ndl;
 for(unsigned i=0;i<24;i++)assert(!__wrap_nan_dp_post_tx(timer_node,data_buffer(i,false)));
 assert(data_posts==24&&!data_recycles);
 assert(__wrap_nan_dp_post_tx(timer_node,data_buffer(24,false))==ESP_ERR_NO_MEM);
 assert(data_posts==24&&data_recycles==1);
 next_buffer=32;
 for(unsigned i=0;i<8;i++){
  void *b=esp32qjs_wifi_nan_alloc_action(1,(uintptr_t)ndl,3,4,5);assert(b);
  metadata[32+i][8]=9;metadata[32+i][9]=7;esp32_mquickjs_wifi_nan_tx_submitting(b);
 }
 assert(!esp32qjs_wifi_nan_alloc_action(1,(uintptr_t)ndl,3,4,5));
 assert(esp32_mquickjs_wifi_nan_tx_close()==ESP_ERR_TIMEOUT&&heap_live==1);
 for(unsigned i=0;i<24;i++)ic_ebuf_recycle_tx(buffers[i]);
 for(unsigned i=0;i<8;i++)ic_ebuf_recycle_tx(buffers[32+i]);
 assert(!esp32_mquickjs_wifi_nan_tx_close()&&!heap_live);
}
