
static bool esp32qjs_dpp_command_allowed_locked(void){return true;}
static struct dpp_config_store store;
static struct{bool dpp_deinit_pending;struct dpp_config_store *dpp_config_store;}s_dpp_ctx={false,&store};
static atomic_bool s_dpp_init_done=true,dpp_shutting_down;
static uint64_t s_dpp_config_revision;
static int fail_at,calls,live,notifies;static bool locked,driver_ready;
static int dpp_api_lock(void){assert(!locked);locked=true;return 0;}
static int dpp_api_unlock(void){assert(locked);locked=false;return 0;}
static void forced_memzero(void *p,size_t n){volatile uint8_t *v=p;while(n--)*v++=0;}
static void *os_malloc(size_t n){if(++calls==fail_at)return NULL;void *p=malloc(n);assert(p);live++;return p;}
static void *os_zalloc(size_t n){void *p=os_malloc(n);if(p)memset(p,0,n);return p;}
static void bin_clear_free(void *p,size_t n){if(p){forced_memzero(p,n);free(p);live--;}}
static struct wpabuf *wpabuf_alloc_copy(const void *data,size_t n){struct wpabuf *b=os_malloc(sizeof(*b)+n);if(b){b->n=n;memcpy(b->bytes,data,n);}return b;}
static void wpabuf_clear_free(struct wpabuf *b){if(b)bin_clear_free(b,sizeof(*b)+b->n);}
static size_t wpabuf_len(const struct wpabuf *b){return b->n;}
static const void *wpabuf_head(const struct wpabuf *b){return b->bytes;}
static void dpp_clear_confs(struct dpp_conf *c){if(c){if(c->connector)bin_clear_free(c->connector,strlen(c->connector)+1);wpabuf_clear_free(c->net_access_key);wpabuf_clear_free(c->c_sign_key);bin_clear_free(c,sizeof(*c));}}
static int dpp_akm_dpp(enum dpp_akm a){return a==DPP_AKM_DPP || a==DPP_AKM_SAE_DPP || a==DPP_AKM_PSK_SAE_DPP;}
static int dpp_akm_legacy(enum dpp_akm a){return a==DPP_AKM_PSK || a==DPP_AKM_SAE || a==DPP_AKM_PSK_SAE;}
static int dpp_akm_ver2(enum dpp_akm a){return a==DPP_AKM_SAE_DPP || a==DPP_AKM_PSK_SAE_DPP;}
static void esp_wifi_sta_notify_dpp_config_set_internal(bool value){assert(locked);driver_ready=value;notifies++;}
static size_t os_strlcpy(char *dst,const char *src,size_t n){size_t len=strlen(src);if(n){size_t m=len<n-1?len:n-1;memcpy(dst,src,m);dst[m]=0;}return len;}
static int wpa_snprintf_hex(char *out,size_t cap,const uint8_t *in,size_t n){if(cap<2*n+1)return -1;for(size_t i=0;i<n;i++)snprintf(out+2*i,cap-2*i,"%02x",in[i]);return n*2;}
