
struct peer_svc_info {SLIST_ENTRY(peer_svc_info) next;uint8_t svc_id,own_svc_id,type,peer_nmi[6];};
struct own_svc_info {char svc_name[256];uint8_t svc_id,type,num_peer_records;SLIST_HEAD(,peer_svc_info) peer_list;};
struct ndl_info {uint8_t ndp_id,peer_ndi[6],peer_nmi[6],publisher_id,own_role;};
static struct {struct own_svc_info own_svc[2];struct ndl_info ndl[2];}s_nan_ctx;
static void *s_nan_data_lock=(void *)1;
static unsigned locks;
static bool locked,managed_match=true;
#define NAN_DATA_LOCK() do{assert(!locked);locked=true;++locks;}while(0)
#define NAN_DATA_UNLOCK() do{assert(locked);locked=false;}while(0)
static bool esp32_mquickjs_wifi_nan_ndp_peer_service(const uint8_t peer[6],uint8_t publisher,uint8_t service){
 assert(locked&&peer[0]==2&&publisher&&service);return managed_match;
}
static void assert_clear(const void *value,size_t n){for(size_t i=0;i<n;i++)assert(!((const uint8_t*)value)[i]);}
