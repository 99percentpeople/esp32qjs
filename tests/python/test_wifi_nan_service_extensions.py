"""Deferred NAN credential/vendor production ownership and native crypto failures."""
import importlib.util
import unittest

from test_idf_nan_control import NanNativeControl, ROOT, SDK
from test_wifi_nan_session import NanSessionLifecycle, PREFIX
from wireless_vm_fixture import extract


class NanServiceExtensions(unittest.TestCase):
    compile_case = NanSessionLifecycle.compile_case

    def test_security_and_vendor_copies_survive_input_change_and_wipe_on_close(self):
        self.compile_case(HELPER + r'''
int main(void){
 start(true);
 wifi_nan_discovery_security_params_t sec={.num_credentials=1,.creds={{.csid=1,.use_pmk=true}}};
 memset(sec.creds[0].pmk,0x67,32);
 uint8_t body[255];memset(body,0x34,sizeof(body));
 nan_vendor_ie_t vendor={.vendor_oui={0x50,0x6f,0x9a},.body_len=255,.body=body};
 esp32_mquickjs_wifi_nan_service_config_t cfg={.subscribe={.service_name="secure",.datapath_reqd=true,
  .security_reqd=true,.security_cfg=&sec,.vendor_ie=&vendor}};
 esp32_mquickjs_wifi_nan_discovery_t*d=NULL;
 assert(!esp32_mquickjs_wifi_nan_discovery_create(s,false,&cfg,1000,&d));
 wifi_nan_discovery_security_params_t*captured=d->config.subscribe.security_cfg;
 assert(captured!=&sec&&captured->creds[0].pmk[0]==0x67);
 assert(captured->group_data_prot&&captured->group_mgmt_prot&&d->status.security_required);
 memset(&sec,0,sizeof(sec));memset(body,0,sizeof(body));
 assert(captured->creds[0].pmk[31]==0x67&&d->vendor.body!=body&&d->vendor.body[254]==0x34);
 esp32_mquickjs_event_queue_t q={.references=1};
 assert(!esp32_mquickjs_wifi_nan_discovery_activate(d,&q));tick();assert(d->status.ready);
 wifi_nan_datapath_req_t req={.pub_id=3,.peer_mac={2,3,4,5,6,7},.confirm_required=false};
 esp32_mquickjs_wifi_nan_path_t*p=NULL;
 assert(!esp32_mquickjs_wifi_nan_path_create(d,&req,1000,&p));
 assert(esp32_mquickjs_wifi_nan_path_activate(p)==ESP_ERR_INVALID_STATE&&!path_requests);
 esp32_mquickjs_wifi_nan_path_close(p,false);esp32_mquickjs_wifi_nan_path_release(p);
 esp32_mquickjs_wifi_nan_session_close(s,false);tick();assert(d->status.retired);
 for(size_t i=0;i<sizeof(*captured);i++)assert(!((uint8_t*)captured)[i]);
 assert(d->status.credential_count==1&&d->status.vendor_bytes==255);
 esp32_mquickjs_wifi_nan_discovery_queue_closed(d);esp32_mquickjs_wifi_nan_discovery_release(d);
 esp32_mquickjs_wifi_nan_session_release(s);assert(!allocations);
}
''', security=True)

    def test_invalid_security_and_oom_do_not_reserve_an_owner(self):
        self.compile_case(HELPER + r'''
int main(void){
 start(false);wifi_nan_discovery_security_params_t sec={.num_credentials=1,.creds={{.csid=1}}};
 strcpy(sec.creds[0].passphrase,"short");
 esp32_mquickjs_wifi_nan_service_config_t cfg={.publish={.service_name="secure",.security_reqd=true,.security_cfg=&sec}};
 esp32_mquickjs_wifi_nan_discovery_t*d=NULL;unsigned refs=s->references,alloc=allocation_calls;
 assert(esp32_mquickjs_wifi_nan_discovery_create(s,true,&cfg,1000,&d)==ESP_ERR_INVALID_ARG);
 assert(!d&&!s_nan_discovery_handles&&s->references==refs&&allocation_calls==alloc);
 strcpy(sec.creds[0].passphrase,"test-only-password");sec.creds[0].csid=2;
 assert(esp32_mquickjs_wifi_nan_discovery_create(s,true,&cfg,1000,&d)==ESP_ERR_NOT_SUPPORTED);
 sec.creds[0].csid=1;fail_allocation=allocation_calls+1;
 assert(esp32_mquickjs_wifi_nan_discovery_create(s,true,&cfg,1000,&d)==ESP_ERR_NO_MEM);
 assert(!d&&!s_nan_discovery_handles&&s->references==refs);
 esp32_mquickjs_wifi_nan_session_close(s,false);tick();esp32_mquickjs_wifi_nan_session_release(s);assert(!allocations);
}
''', security=True)

    def test_security_disabled_and_vendor_send_capture(self):
        self.compile_case(HELPER + r'''
int main(void){
 start(false);wifi_nan_discovery_security_params_t sec={.num_credentials=1,.creds={{.csid=1,.use_pmk=true}}};
 esp32_mquickjs_wifi_nan_service_config_t cfg={.publish={.service_name="vendor",.security_reqd=true,.security_cfg=&sec}};
 esp32_mquickjs_wifi_nan_discovery_t*d=NULL;
 assert(esp32_mquickjs_wifi_nan_discovery_create(s,true,&cfg,1000,&d)==ESP_ERR_NOT_SUPPORTED&&!d);
 cfg.publish.security_reqd=false;cfg.publish.security_cfg=NULL;
 assert(!esp32_mquickjs_wifi_nan_discovery_create(s,true,&cfg,1000,&d));
 esp32_mquickjs_event_queue_t q={.references=1};assert(!esp32_mquickjs_wifi_nan_discovery_activate(d,&q));tick();
 uint8_t body[2]={1,2};nan_vendor_ie_t vendor={.vendor_oui={1,2,3},.body_len=2,.body=body};
 wifi_nan_followup_params_t params={.peer_inst_id=3,.peer_mac={2,3,4,5,6,7},.vendor_ie=&vendor};
 esp32_mquickjs_wifi_nan_message_t*m=NULL;
 assert(!esp32_mquickjs_wifi_nan_message_create(d,&params,1000,&m));
 body[0]=9;assert(m->params.vendor_ie!=&vendor&&m->params.vendor_ie->body[0]==1);
 esp32_mquickjs_wifi_nan_message_cancel(m,false);esp32_mquickjs_wifi_nan_message_release(m);
 vendor.body_len=256;m=NULL;
 assert(esp32_mquickjs_wifi_nan_message_create(d,&params,1000,&m)==ESP_ERR_INVALID_ARG);
 esp32_mquickjs_wifi_nan_session_close(s,false);tick();
 esp32_mquickjs_wifi_nan_discovery_queue_closed(d);esp32_mquickjs_wifi_nan_discovery_release(d);
 esp32_mquickjs_wifi_nan_session_release(s);assert(!allocations);
}
''')


HELPER = r'''
static esp32_mquickjs_wifi_nan_session_t*s;
static void tick(void){clock_us+=100000;if(esp32_mquickjs_wifi_nan_service())run_queued(NULL);}
static void start(bool group){
 assert(!esp32_mquickjs_wifi_nan_open_runtime());
 wifi_nan_sync_config_t config={.op_channel=6,.group_mgmt_prot=group};
 assert(!esp32_mquickjs_wifi_nan_session_create(&config,1000,&s));
 assert(!esp32_mquickjs_wifi_nan_session_activate(s));tick();assert(s->status.ready);
 for(size_t i=0;i<sizeof(s->config);i++)assert(!((uint8_t*)&s->config)[i]);
 assert(s->status.group_management_protection==group);
}
'''


class NanSecurityDerivation(unittest.TestCase):
    compile_run = NanNativeControl.compile_run

    def test_group_key_install_returns_original_error_without_submitting_the_suffix(self):
        source = SDK / 'wifi_apps/nan_app/src/nan_security.c'
        if not source.exists():
            self.skipTest('reviewed SDK unavailable')
        spec = importlib.util.spec_from_file_location('nan_security_patch', ROOT / 'scripts/patch_idf_nan.py')
        patch = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(patch)
        prepared = patch.patch_security_source(source.read_text())
        self.compile_run(PREFIX + GROUP_BOUNDARY +
            extract(prepared, 'esp32_mquickjs_wifi_nan_sdk_security_install_group_keys') + r'''
int main(void){
 assert(!esp32_mquickjs_wifi_nan_sdk_security_install_group_keys()&&!installs);
 s_nan_ctx.group_mgmt_prot=true;
 mac_error=-71;assert(esp32_mquickjs_wifi_nan_sdk_security_install_group_keys()==-71&&!ensures&&!installs);
 mac_error=0;ensure_error=-72;assert(esp32_mquickjs_wifi_nan_sdk_security_install_group_keys()==-72&&!installs);
 ensure_error=0;install_error_at=1;
 assert(esp32_mquickjs_wifi_nan_sdk_security_install_group_keys()==-73&&installs==1);
 installs=0;install_error_at=2;
 assert(esp32_mquickjs_wifi_nan_sdk_security_install_group_keys()==-73&&installs==2);
 installs=0;install_error_at=0;
 assert(!esp32_mquickjs_wifi_nan_sdk_security_install_group_keys()&&installs==2);
}
''')

    def test_unresolved_pmkid_cannot_leave_a_usable_encrypted_context(self):
        source = SDK / 'wifi_apps/nan_app/src/nan_security.c'
        if not source.exists():
            self.skipTest('reviewed SDK unavailable')
        spec = importlib.util.spec_from_file_location('nan_security_patch', ROOT / 'scripts/patch_idf_nan.py')
        patch = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(patch)
        original = source.read_text()
        for prepared in (False, True):
            code = patch.patch_security_source(original) if prepared else original
            for mode in (0, 1, 2):
                self.compile_run(PREFIX + CRYPTO_BOUNDARY + PENDING_BOUNDARY +
                    extract(code, 'nan_security_apply_pending') + PENDING_MAIN.replace('MATCH_MODE', str(mode)),
                    expect_failure=not prepared and mode != 2)

    def test_pending_security_material_clear_wipes_entire_records(self):
        source = SDK / 'wifi_apps/nan_app/src/nan_security.c'
        if not source.exists():
            self.skipTest('reviewed SDK unavailable')
        spec = importlib.util.spec_from_file_location('nan_security_patch', ROOT / 'scripts/patch_idf_nan.py')
        patch = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(patch)
        prepared = patch.patch_security_source(source.read_text())
        body = extract(prepared, 'esp32_mquickjs_wifi_nan_sdk_security_clear_pending')
        self.compile_run(PREFIX + CRYPTO_BOUNDARY + r'''
static struct{bool valid;uint8_t material[80];}s_pending_m1,s_pending_scia;
''' + body + r'''
int main(void){
 memset(&s_pending_m1,0x67,sizeof(s_pending_m1));memset(&s_pending_scia,0x78,sizeof(s_pending_scia));
 esp32_mquickjs_wifi_nan_sdk_security_clear_pending();
 for(size_t i=0;i<sizeof(s_pending_m1);i++)assert(!((uint8_t*)&s_pending_m1)[i]);
 for(size_t i=0;i<sizeof(s_pending_scia);i++)assert(!((uint8_t*)&s_pending_scia)[i]);
}
''')

    def test_original_and_prepared_mac_hmac_failures_and_partial_material_cleanup(self):
        source = SDK / 'wifi_apps/nan_app/src/nan_security.c'
        if not source.exists():
            self.skipTest('reviewed SDK unavailable')
        spec = importlib.util.spec_from_file_location('nan_security_patch', ROOT / 'scripts/patch_idf_nan.py')
        patch = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(patch)
        original = source.read_text()
        for prepared in (False, True):
            code = patch.patch_security_source(original) if prepared else original
            for mode in (1, 2):
                with self.subTest(prepared=prepared, mode=mode):
                    self.compile_run(PREFIX + CRYPTO_BOUNDARY + extract(code, 'nan_derive_security_params') +
                        CRYPTO_MAIN.replace('FAILURE_MODE', str(mode)), expect_failure=not prepared)


CRYPTO_BOUNDARY = r'''
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOG_BUFFER_HEXDUMP(...) ((void)0)
#define NAN_PMK_NAME_LABEL "NAN PMK Name"
#define NAN_PMK_NAME_LABEL_LEN 12
#define ESP_WIFI_NAN_NDP_PMKID_LEN 16
#define WIFI_NAN_SECURITY_ENCRYPTED 1
#define NAN_CSID_GTK_DEFAULT 32
#define WIFI_IF_NAN 2
typedef struct {int type;uint16_t csid_bitmap;bool group_data_prot,group_mgmt_prot;uint8_t nd_pmk[32],nd_pmkid[16];}wifi_nan_security_params_t;
struct own_svc_info{wifi_nan_discovery_security_params_t user_cfg;wifi_nan_security_params_t derived_security[4];};
static struct own_svc_info own;
static unsigned hmac_calls;static int failure_mode;
static void forced_memzero(void*p,size_t n){volatile uint8_t*b=p;while(n--)*b++=0;}
static bool nan_compute_service_id(const char*n,uint8_t*out){(void)n;memset(out,1,6);return true;}
static struct own_svc_info*nan_find_own_svc_by_name(const char*n){(void)n;return &own;}
static int esp_wifi_get_mac(int iface,uint8_t*out){(void)iface;memset(out,2,6);return failure_mode==1?-77:0;}
static int nan_derive_nd_pmk_from_passphrase(const char*p,uint8_t c,const uint8_t*s,const uint8_t*m,uint8_t*out){
 (void)p;(void)c;(void)s;(void)m;memset(out,3,32);return 0;
}
static int hmac(const unsigned char*k,int n,int count,const unsigned char**a,int*l,unsigned char*out){
 (void)k;(void)n;(void)count;(void)a;(void)l;memset(out,0x5a,32);
 return ++hmac_calls==2&&failure_mode==2?-88:0;
}
static struct{int(*hmac_sha256_vector)(const unsigned char*,int,int,const unsigned char**,int*,unsigned char*);}g_wifi_default_wpa_crypto_funcs={hmac};
'''
CRYPTO_MAIN = r'''
int main(void){
 failure_mode=FAILURE_MODE;
 wifi_nan_discovery_security_params_t cfg={.num_credentials=2,.creds={{.csid=1,.use_pmk=true},{.csid=1,.use_pmk=true}}};
 memset(cfg.creds[0].pmk,0x67,32);memset(cfg.creds[1].pmk,0x78,32);
 wifi_nan_security_params_t derived[4];memset(derived,0xa5,sizeof(derived));
 assert(nan_derive_security_params("test-only",&cfg,derived)==ESP_FAIL);
 for(size_t i=0;i<sizeof(derived);i++)assert(!((uint8_t*)derived)[i]);
 assert(!own.derived_security[0].type);
}
'''

PENDING_BOUNDARY = r'''
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
'''
PENDING_MAIN = r'''
int main(void){
 int mode=MATCH_MODE;struct ndl_info ndl={0};uint8_t peer[6]={2,3,4,5,6,7};
 own.user_cfg.num_credentials=1;own.derived_security[0].csid_bitmap=2;
 memset(own.derived_security[0].nd_pmk,0x67,32);match_index=mode==2?0:-1;
 s_pending_scia.pub_id=7;s_pending_scia.has_csid=true;s_pending_scia.has_pmkid=mode!=1;s_pending_scia.csid_bitmap=2;
 s_pending_m1.valid=true;s_pending_m1.pub_id=7;
 nan_security_apply_pending(&ndl,&own,7,peer,peer);
 if(mode==2)assert(ndl.security_ctx.type==WIFI_NAN_SECURITY_ENCRYPTED&&ndl.security_ctx.nd_pmk[0]==0x67&&ndl.handshake_state==1);
 else assert(!ndl.security_ctx.type&&!ndl.handshake_state);
}
'''

GROUP_BOUNDARY = r'''
#define WIFI_IF_NAN 2
#define NAN_ND_GTK_LEN 16
#define NAN_WIFI_WPA_ALG_BIP_CMAC_128 6
#define NAN_KEY_ND_IGTK 3
#define NAN_KEY_ND_BIGTK 4
static struct{bool group_mgmt_prot,own_igtk_set,own_bigtk_set;uint8_t own_igtk_keyid,own_bigtk_keyid;
 uint8_t own_igtk[16],own_bigtk[16],own_igtk_ipn[6],own_bigtk_bipn[6];}s_nan_ctx;
static int mac_error,ensure_error,install_error_at,installs,ensures;
static int esp_wifi_get_mac(int iface,uint8_t*out){(void)iface;memset(out,2,6);return mac_error;}
static int nan_ensure_own_igtk(void){++ensures;s_nan_ctx.own_igtk_set=!ensure_error;return ensure_error;}
static int nan_ensure_own_bigtk(void){++ensures;s_nan_ctx.own_bigtk_set=!ensure_error;return ensure_error;}
static int esp_wifi_set_nan_key_internal(int alg,const uint8_t*mac,int id,int tx,const uint8_t*rsc,int rsc_len,
 const uint8_t*key,int length,int type){
 (void)alg;(void)mac;(void)id;(void)tx;(void)rsc;(void)rsc_len;(void)key;(void)length;(void)type;
 return ++installs==install_error_at?-73:0;
}
'''
