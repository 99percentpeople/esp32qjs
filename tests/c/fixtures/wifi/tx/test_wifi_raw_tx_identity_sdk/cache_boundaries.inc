/* Real post/output/copy/recycle path, with OS and raw RF dispatch substituted. */
extern int ieee80211_output_process(void);
extern u32 s_tx_cacheq[2];
extern int ieee80211_output_init(void);
static int (*registered_output)(void);
void ic_register_net80211_tx_cb(int (*callback)(void)) {registered_output=callback;}
void fixture_amsdu_noop(void *descriptor) {(void)descriptor;}
extern void ieee80211_copy_eb_header(void *,void *);
u32 g_hmac_cnt[256];
void *s_netstack_free, *s_encap_amsdu_func;
int config_is_cache_tx_buf_enabled(void) {return 1;}
int chm_is_at_home_channel(void) {return 1;}
int pp_post(int event,int argument) {(void)argument;if(event!=5)__builtin_trap();return 0;}
void *fixture_search_node(int interface,const void *bytes,int *error) {
    (void)bytes;if(interface!=0)__builtin_trap();*error=0;return 0;
}
void fixture_unexpected_path(void) {__builtin_trap();}
void esp32qjs_raw_tx_copy_header(void *destination,void *source) {
    ieee80211_copy_eb_header(destination,source);
    if(source!=bound || source==destination)__builtin_trap();
    bound=destination;identity_transfers++;
}
void ic_ebuf_recycle_tx(void *descriptor) {
    if(descriptor!=eb)__builtin_trap();
    cache_recycles++;
}
int fixture_raw_process(int interface,void *descriptor) {
    if(interface!=0 || descriptor!=replacement_eb || cache_recycles!=posted+1)__builtin_trap();
    fixture_post_hmac_tx(descriptor);
    return 0;
}
