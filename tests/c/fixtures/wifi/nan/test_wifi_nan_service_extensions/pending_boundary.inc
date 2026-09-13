
#define NAN_NONCE_LEN 32
#define NAN_REPLAY_COUNTER_LEN 8
#define NAN_KEY_RSC_LEN 8
#define NAN_HANDSHAKE_M1_RCVD 1
#define NAN_CSIA_CAP_GTK_SUPP_MASK 3
#define NAN_CSIA_CAP_GTK_SUPP_ALL 2
#define NAN_CSIA_CAP_GTK_SUPP_POS 0
static struct{bool group_mgmt_prot;}s_nan_ctx;
static struct{bool has_csid,has_pmkid,peer_group_data;uint8_t pub_id,peer_caps,pmkid[16];uint16_t csid_bitmap;}s_pending_scia;
static struct{bool valid;uint8_t pub_id,anonce[32],rx_replay_counter[8],key_rsc[8];}s_pending_m1;
struct ndl_info{wifi_nan_security_params_t security_ctx;bool gtk_required,igtk_required,bigtk_required,rx_replay_counter_set;
 int handshake_state;uint8_t anonce[32],rx_replay_counter[8],key_rsc[8];};
static int match_index=-1;
static int nan_match_pmkid(struct own_svc_info*o,const uint8_t*p,const uint8_t*n,const uint8_t*d){
 (void)o;(void)p;(void)n;(void)d;return match_index;
}
