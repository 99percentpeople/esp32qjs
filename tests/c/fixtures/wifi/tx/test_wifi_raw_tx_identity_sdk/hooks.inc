extern void ieee80211_freedom_inside_cb(void *);
static void *bound;
static unsigned allocated_hooks, completed_hooks, public_callbacks;
void *esp32qjs_raw_tx_alloc(const void *bytes,int kind,int length) {
    bound=ic_ebuf_alloc(bytes,kind,length);if(bound)allocated_hooks++;return bound;
}
void esp32qjs_raw_tx_info(void *descriptor,void *info) {
    if(descriptor!=bound) __builtin_trap();
    completed_hooks++;memset(info,0,28);
}
static void completed(void *info) {
    (void)info;if(completed_hooks!=public_callbacks+1)__builtin_trap();public_callbacks++;
}
void fixture_post_hmac_tx(void *buffer) {
    if(buffer!=bound || allocated_hooks!=posted+1)__builtin_trap();
    posted++;ieee80211_freedom_inside_cb(buffer);
}
