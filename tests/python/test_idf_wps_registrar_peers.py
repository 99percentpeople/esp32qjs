"""Deferred production EAP-WSC peer isolation and parent teardown regressions.

Only AST-parse during the Wi-Fi implementation wave. These fixtures execute
patched SDK function bodies with allocator/protocol/driver boundaries, not a
separate lifecycle model. They do not establish native queue or RF retirement.
"""
import os
from pathlib import Path
import sys
import unittest
from test_wireless_control_regression import compile_run

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from patch_idf_wps import function
from patch_idf_wps_registrar import patch_source


def peer_init(source):
    start = source.index('static void * eap_wsc_init(')
    return source[start:source.index('\n}\n', start) + 3]


class WpsRegistrarPeers(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        sdk = os.environ.get('IDF_PATH')
        if not sdk:
            raise unittest.SkipTest('Set IDF_PATH to the reviewed ESP-IDF')
        component = Path(sdk) / 'components/wpa_supplicant'
        paths = ('src/eap_server/eap_server_wsc.c', 'src/ap/wps_hostapd.c',
                 'src/ap/ieee802_1x.c')
        cls.original = {p: (component / p).read_bytes() for p in paths}
        cls.patched = {p: patch_source(p, src).decode() for p, src in cls.original.items()}

    def test_original_peers_alias_and_reset_disables_parent_inside_eap(self):
        src = self.original['src/eap_server/eap_server_wsc.c'].decode()
        code = TYPES + peer_init(src) + function(src, 'eap_wsc_reset')
        compile_run(self, code + r'''
int main(void) {
 setup();
 struct eap_wsc_data *a=eap_wsc_init(&peers[0]),*b=eap_wsc_init(&peers[1]);
 assert(a && b && a!=b && a->wps==b->wps && a->wps==owner.wps);
 peers[0].eap_method_priv=a;
 eap_wsc_reset(&peers[0],a);
 assert(disable_calls==1); /* Native disable would free the calling EAP tree. */
 eap_wsc_reset(&peers[1],b);
 assert(disable_calls==2 && live==0);
 return 0;
}
''')

    def test_peers_own_protocol_and_credential_snapshots_reset_is_local(self):
        src = self.patched['src/eap_server/eap_server_wsc.c']
        code = TYPES + peer_init(src) + function(src, 'eap_wsc_reset')
        compile_run(self, code + r'''
int main(void) {
 setup();
 struct eap_wsc_data *a=eap_wsc_init(&peers[0]),*b=eap_wsc_init(&peers[1]);
 assert(a && b && a->wps!=b->wps && a->wps!=owner.wps);
 assert(a->wps->use_cred!=b->wps->use_cred && a->wps->use_cred!=template.use_cred);
 assert(!memcmp(a->wps->peer_dev.mac_addr,peers[0].peer_addr,6));
 assert(!memcmp(b->wps->mac_addr_e,peers[1].peer_addr,6));
 assert(a->wps->use_cred->key[0]==0x55 && b->wps->use_cred->key[0]==0x55);
 template.use_cred->key[0]=0x77;
 a->wps->state=18;a->wps->use_cred->key[0]=0x99;
 assert(b->wps->state==0 && b->wps->use_cred->key[0]==0x55);
 peers[0].eap_method_priv=a;peers[1].eap_method_priv=b;
 a->in_buf=os_zalloc(sizeof(struct wpabuf));a->out_buf=os_zalloc(sizeof(struct wpabuf));
 memset(a->in_buf,0xab,sizeof(struct wpabuf));memset(a->out_buf,0xcd,sizeof(struct wpabuf));
 eap_wsc_reset(&peers[0],a);
 assert(!peers[0].eap_method_priv && peers[1].eap_method_priv==b);
 assert(!disable_calls && hapd.wps==&context && context.registrar);
 assert(b->wps->use_cred->key[0]==0x55 && template.use_cred->key[0]==0x77);
 eap_wsc_reset(&peers[1],b);eap_wsc_reset(NULL,NULL);
 assert(!live && protocol_frees==2 && buffer_clears==2 && !disable_calls);
 return 0;
}
''')

    def test_every_peer_allocation_failure_and_admission_rejection_preserves_parent(self):
        src = self.patched['src/eap_server/eap_server_wsc.c']
        code = TYPES + peer_init(src) + function(src, 'eap_wsc_reset')
        compile_run(self, code + r'''
int main(void) {
 for (int nth=1;nth<=3;nth++) {
  setup();fail_at=nth;
  assert(!eap_wsc_init(&peers[0]) && !live && !disable_calls);
  assert(hapd.wps==&context && context.registrar && template.use_cred==&credential);
 }
 for (int fault=0;fault<10;fault++) {
  setup();
  if(fault==0)peers[0].cfg=NULL;
  if(fault==1)peers[0].identity=NULL;
  if(fault==2)peers[0].identity=(u8*)WSC_ID_REGISTRAR;
  if(fault==3)owner_kind=WPS_OWNER_ENROLLEE;
  if(fault==4)owner_status=0;
  if(fault==5)owner.wps=NULL;
  if(fault==6)cfg.wps=NULL;
  if(fault==7)context.registrar=NULL;
  if(fault==8)template.use_cred=NULL;
  if(fault==9)hapd.wps=NULL;
  assert(!eap_wsc_init(&peers[0]) && !allocations && !live && !disable_calls);
 }
 setup();assert(!eap_wsc_init(NULL));
 return 0;
}
''')

    def test_parent_deinit_retires_peers_before_authenticator_registrar_methods(self):
        wsc = self.patched['src/eap_server/eap_server_wsc.c']
        host = self.patched['src/ap/wps_hostapd.c']
        auth = self.patched['src/ap/ieee802_1x.c']
        code = TYPES + peer_init(wsc) + function(wsc, 'eap_wsc_reset')
        code += TEARDOWN_BOUNDARIES
        code += function(auth, 'esp32qjs_wps_eapol_deinit')
        code += function(host, 'ap_sta_server_sm_deinit')
        code += function(host, 'esp32qjs_wps_ap_hostap_release')
        code += function(host, 'hostapd_deinit_wps')
        compile_run(self, code + r'''
int main(void) {
 setup();
 struct eap_config *owned_cfg=os_zalloc(sizeof(*owned_cfg));*owned_cfg=cfg;
 hapd.eapol_auth=os_zalloc(sizeof(*hapd.eapol_auth));hapd.eapol_auth->conf.eap_cfg=owned_cfg;
 for(int i=0;i<2;i++){peers[i].cfg=owned_cfg;peers[i].eap_method_priv=eap_wsc_init(&peers[i]);assert(peers[i].eap_method_priv);}
 hostapd_deinit_wps(&hapd);
 assert(!hapd.wps && !hapd.eapol_auth && !context.registrar && !live);
 assert(step==5 && protocol_frees==2 && methods_freed==1 && !disable_calls);
 hostapd_deinit_wps(&hapd);hostapd_deinit_wps(NULL);
 assert(step==5 && methods_freed==1 && clear_ies==0 && !live);
 methods_freed=0;esp32qjs_wps_host_methods_identity=2;
 hostapd_deinit_wps(&hapd);
 assert(esp32qjs_wps_host_methods_identity==2 && !methods_freed);
 return 0;
}
''')


TYPES = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t u8;
#define ESP_SUPPLICANT 1
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NOT_FINISHED 0x10c
#define WSC_ID_ENROLLEE "WFA-SimpleConfig-Enrollee-1-0"
#define WSC_ID_ENROLLEE_LEN (sizeof(WSC_ID_ENROLLEE)-1)
#define WSC_ID_REGISTRAR "WFA-SimpleConfig-Registrar-1-0"
#define WSC_ID_REGISTRAR_LEN (sizeof(WSC_ID_REGISTRAR)-1)
#define WPS_OWNER_REGISTRAR 2
#define WPS_OWNER_ENROLLEE 1
#define WPS_STATUS_PENDING 2
#define WSC_FRAGMENT_SIZE 1400
#define ETH_ALEN 6
#define MSG_INFO 0
#define os_memcmp memcmp
#define os_memcpy memcpy
#define wpa_hexdump_ascii(...) ((void)0)
static int allocations,fail_at,live,disable_calls,protocol_frees,buffer_clears;
static void *os_zalloc(size_t n){if(++allocations==fail_at)return NULL;void *p=calloc(1,n);assert(p);live++;return p;}
static void os_free(void *p){if(p){live--;free(p);}}
static void forced_memzero(void *p,size_t n){volatile u8 *v=p;while(n--)*v++=0;}
static void bin_clear_free(void *p,size_t n){if(p){forced_memzero(p,n);for(size_t i=0;i<n;i++)assert(!((u8*)p)[i]);os_free(p);}}
static void *os_memdup(const void *p,size_t n){void *r=os_zalloc(n);if(r)memcpy(r,p,n);return r;}
struct wpabuf{u8 bytes[16];};
static void wpabuf_free(struct wpabuf *p){os_free(p);}
static void wpabuf_clear_free(struct wpabuf *p){if(p){buffer_clears++;bin_clear_free(p,sizeof(*p));}}
struct wps_credential{u8 ssid[32],key[64];};
struct wps_context{void *registrar;};
struct wps_data{struct wps_context *wps;struct wps_credential *use_cred;int pbc,dev_pw_id,state;struct {u8 mac_addr[6];} peer_dev;u8 mac_addr_e[6];};
struct wps_sm{struct wps_data *wps;struct wps_context *wps_ctx;};
struct wps_config{struct wps_context *wps;int registrar,pbc,dev_pw_id;};
struct eap_config{struct wps_context *wps;};
struct eapol_authenticator{struct {const struct eap_config *eap_cfg;} conf;};
struct hostapd_data{struct wps_context *wps;struct eapol_authenticator *eapol_auth;};
struct eap_sm{u8 *identity;size_t identity_len;const struct eap_config *cfg;u8 peer_addr[6];void *eap_method_priv;};
struct eap_wsc_data{enum{START,MESG,FRAG_ACK,WAIT_FRAG_ACK,DONE,WSC_FAIL}state;int registrar;struct wpabuf *in_buf,*out_buf;struct wps_data *wps;size_t fragment_size;};
static struct wps_context context;
static struct wps_credential credential;
static struct wps_data template;
static struct wps_sm owner;
static struct hostapd_data hapd;
static struct eap_config cfg;
static struct eap_sm peers[2];
static int owner_kind,owner_status;
static struct wps_sm *wps_sm_get(void){return &owner;}
static struct hostapd_data *hostapd_get_hapd_data(void){return &hapd;}
static int wps_get_owner(void){return owner_kind;}
static int wps_get_status(void){return owner_status;}
static int wifi_ap_wps_disable_internal(void){disable_calls++;return 0;}
static struct wps_data *wps_init(const struct wps_config *c){assert(c->registrar==1 && c->wps==&context);struct wps_data *p=os_zalloc(sizeof(*p));if(p){p->wps=c->wps;p->pbc=c->pbc;}return p;}
static void wps_deinit(struct wps_data *p){assert(p!=&template && p->wps->registrar);protocol_frees++;bin_clear_free(p->use_cred,sizeof(*p->use_cred));bin_clear_free(p,sizeof(*p));}
static void setup(void){
 allocations=fail_at=live=disable_calls=protocol_frees=buffer_clears=0;
 context=(struct wps_context){.registrar=(void*)(uintptr_t)1};
 memset(&credential,0x55,sizeof(credential));
 template=(struct wps_data){.wps=&context,.use_cred=&credential,.pbc=1};
 owner=(struct wps_sm){.wps=&template,.wps_ctx=&context};
 cfg=(struct eap_config){.wps=&context};hapd=(struct hostapd_data){.wps=&context};
 for(int i=0;i<2;i++)peers[i]=(struct eap_sm){.identity=(u8*)WSC_ID_ENROLLEE,.identity_len=WSC_ID_ENROLLEE_LEN,.cfg=&cfg,.peer_addr={2,0,0,0,0,i+1}};
 owner_kind=WPS_OWNER_REGISTRAR;owner_status=WPS_STATUS_PENDING;
}
'''

TEARDOWN_BOUNDARIES = r'''
static uint32_t esp32qjs_wps_host_methods_identity=1;
static bool esp32qjs_wps_ap_result_deinit_allowed(void *h){return h!=NULL;}
static uint32_t esp32qjs_wps_ap_result_identity(const void *h){return h==&hapd;}
static int esp32qjs_wps_ap_eapol_drain(void *h,uint32_t id){assert(h==&hapd && id);return 0;}
static bool esp32qjs_wps_ap_eapol_drained(uint32_t id){return id!=0;}
static int step,methods_freed,clear_ies;
struct sta_info{int index;struct wpabuf *wps_ie;};
static void hostapd_wps_reenable_ap_pin(void *a,void *b){(void)a;(void)b;}
static void hostapd_wps_ap_pin_timeout(void *a,void *b){(void)a;(void)b;}
static int eloop_cancel_timeout(void (*f)(void*,void*),void *a,void *b){(void)f;(void)a;(void)b;return 0;}
static void hostapd_wps_clear_ies(struct hostapd_data *h,int d){(void)h;(void)d;clear_ies++;}
static void ieee802_1x_free_station(struct hostapd_data *h,struct sta_info *s){
 if(!peers[s->index].eap_method_priv)return;
 assert(!h->wps && h->eapol_auth && context.registrar && !methods_freed);
 eap_wsc_reset(&peers[s->index],peers[s->index].eap_method_priv);step++;
}
static int ap_for_each_sta(struct hostapd_data *h,int (*fn)(struct hostapd_data*,struct sta_info*,void*),void *ctx){
 for(int i=0;i<2;i++){struct sta_info s={.index=i};assert(fn(h,&s,ctx)==0);}return 0;
}
static void eapol_auth_deinit(struct eapol_authenticator *a){assert(step==2 && !hapd.eapol_auth && context.registrar);step++;os_free(a);}
static void wps_registrar_deinit(void *r){if(!r)return;assert(r && step==3 && live==0 && !methods_freed);step++;}
static void eap_server_unregister_methods(void){if(methods_freed)return;assert(step==4 && !context.registrar);step++;methods_freed++;}
'''
