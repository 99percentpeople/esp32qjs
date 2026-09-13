
static void close_inside_post(void *buffer){
 (void)buffer;assert(esp32_mquickjs_wifi_nan_tx_close()==ESP_ERR_TIMEOUT&&heap_live==1);
}
int main(void){
 assert(!esp32_mquickjs_wifi_nan_tx_open());
 data_post_error=-73;void *b=data_buffer(0,true);
 assert(__wrap_nan_dp_post_tx(NULL,b)==-73&&!data_recycles);
 assert(s_nan_tx->entries[0].ticket&&!s_nan_tx->entries[0].recycled);
 ic_ebuf_recycle_tx(b);assert(!s_nan_tx->entries[0].ticket);
 data_post_error=0;data_post_recycles=true;data_post_hook=close_inside_post;
 assert(!__wrap_nan_dp_post_tx(NULL,data_buffer(1,true))&&data_posts==2);
 assert(!esp32_mquickjs_wifi_nan_tx_close()&&!heap_live);
}
