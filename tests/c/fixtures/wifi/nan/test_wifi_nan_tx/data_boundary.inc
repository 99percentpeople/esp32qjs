
static unsigned data_posts,data_recycles;
static int data_post_error;
static bool data_post_recycles;
static void (*data_post_hook)(void *buffer);
void ic_ebuf_recycle_tx(void *buffer){
 assert(!locked&&buffer);++data_recycles;
 uint32_t ticket=esp32_mquickjs_wifi_nan_tx_recycling(buffer);
 esp32_mquickjs_wifi_nan_tx_recycled(ticket);
}
int __real_nan_dp_post_tx(void *node,void *buffer){
 assert(!locked&&buffer);(void)node;++data_posts;
 if(data_post_recycles)ic_ebuf_recycle_tx(buffer);
 if(data_post_hook)data_post_hook(buffer);
 return data_post_error;
}
