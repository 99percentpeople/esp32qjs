
int main(void){
 assert(!esp32_mquickjs_wifi_nan_tx_open());void *b=data_buffer(0,true);
 assert(!__wrap_nan_dp_post_tx(NULL,b));
 uint32_t old=esp32_mquickjs_wifi_nan_tx_recycling(b);assert(old);
 assert(!__wrap_nan_dp_post_tx(NULL,b)); /* recycler returned address to native allocator */
 esp32_mquickjs_wifi_nan_tx_recycled(old);
 assert(data_posts==2&&!data_recycles);
 esp32_mquickjs_wifi_nan_tx_status_t state;esp32_mquickjs_wifi_nan_tx_status(&state);
 assert(state.tracked==1&&!state.error);
 assert(__wrap_nan_dp_post_tx(NULL,b)==ESP_ERR_INVALID_STATE);
 assert(data_posts==2&&!data_recycles);
 ic_ebuf_recycle_tx(b);assert(!esp32_mquickjs_wifi_nan_tx_close()&&!heap_live);
}
