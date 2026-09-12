"""Deferred production AP input pin/IE ownership fixtures; AST only."""
from pathlib import Path
import sys
import unittest
from test_wireless_control_regression import compile_run
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from patch_idf_wps_ap_input import ACQUIRE,RX,ASSOC


class WpsAPInput(unittest.TestCase):
    def run_case(self,main):
        compile_run(self,TYPES+ACQUIRE+RX+ASSOC+BOUNDARIES+main)

    def test_stale_driver_pointer_busy_peer_and_callback_pin(self):
        self.run_case(r'''
int main(void){
 setup();u8 packet[4]={1,0,0,0};
 native=false;assert(!wpa_ap_rx_eapol(&h,&peer,packet,4) && !received && available);native=true;
 assert(!wpa_ap_rx_eapol(&h,(void*)0xdead,packet,sizeof(packet)) && !received && !wpa_received);
 available=false;assert(!wpa_ap_rx_eapol(&h,&peer,packet,sizeof(packet)) && !received);
 available=true;assert(wpa_ap_rx_eapol(&h,&peer,packet,sizeof(packet)));
 assert(received==1 && !activity && available && !table_lock);
 assert(!wpa_ap_rx_eapol(&h,&peer,packet,3));return 0;
}
''')

    def test_close_blocks_wps_but_preserves_wpa_keys_and_station_owner(self):
        self.run_case(r'''
int main(void){
 setup();u8 packet[4]={1,0,0,0};closing=true;
 assert(!wpa_ap_rx_eapol(&h,&peer,packet,4) && !received);
 packet[1]=IEEE802_1X_TYPE_EAPOL_KEY;
 assert(wpa_ap_rx_eapol(&h,&peer,packet,4) && wpa_received==1 && !received);
 owner=1;packet[1]=0;
 assert(wpa_ap_rx_eapol(&h,&peer,packet,4) && wpa_received==2 && !received);
 assert(!activity && available);return 0;
}
''')

    def test_disabled_missing_and_overlap_association_release_every_ie(self):
        self.run_case(r'''
int main(void){
 setup();peer.wps_ie=new_ie();peer.eapol_sm=(void*)1;
 owner=1;assert(check_n_add_wps_sta(&h,&peer,ie,sizeof(ie),NULL,0)==0);
 assert(!peer.wps_ie && !peer.eapol_sm && !live && !concat_calls);
 owner=WPS_OWNER_REGISTRAR;overlap=true;
 assert(check_n_add_wps_sta(&h,&peer,ie,sizeof(ie),NULL,0)==-1 && !live && !activity);
 overlap=false;peer.wps_ie=new_ie();peer.eapol_sm=(void*)1;
 assert(check_n_add_wps_sta(&h,&peer,NULL,0,NULL,0)==0 && !live && !peer.eapol_sm);
 return 0;
}
''')

    def test_replacement_oom_malformed_input_and_response_failure(self):
        self.run_case(r'''
int main(void){
 setup();peer.wps_ie=new_ie();peer.eapol_sm=(void*)1;
 struct wpabuf *old=peer.wps_ie;oom=true;
 assert(check_n_add_wps_sta(&h,&peer,ie,sizeof(ie),NULL,0)==-1);
 assert(peer.wps_ie==old && peer.eapol_sm && live==1 && reported==ESP_ERR_NO_MEM);
 oom=false;u8 bad[2]={221,99};
 assert(check_n_add_wps_sta(&h,&peer,bad,sizeof(bad),NULL,0)==-1);
 assert(peer.wps_ie==old && live==1 && reported==ESP_ERR_INVALID_SIZE);
 assert(check_n_add_wps_sta(&h,&peer,ie,sizeof(ie),NULL,0)==0 && live==1 && peer.wps_ie!=old);
 response_error=-91;
 assert(check_n_add_wps_sta(&h,&peer,ie,sizeof(ie),NULL,0)==-1);
 assert(!peer.wps_ie && !peer.eapol_sm && !live && !activity);
 eap_oom=true;response_error=0;
 assert(check_n_add_wps_sta(&h,&peer,ie,sizeof(ie),NULL,0)==-1 && !live && !peer.wps_ie);
 return 0;
}
''')

TYPES=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t u8;
#define CONFIG_WPS_REGISTRAR 1
#define CONFIG_SAE 1
#define ETH_ALEN 6
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_SIZE 0x104
#define WPS_OWNER_REGISTRAR 2
#define WPS_TYPE_PBC 1
#define WPS_TYPE_PIN 2
#define WPS_DEV_OUI_WFA 0x0050f204u
#define WLAN_EID_VENDOR_SPECIFIC 221
#define WLAN_STATUS_SUCCESS 0
#define IEEE802_1X_TYPE_EAPOL_KEY 3
#define WPA_GET_BE32(p) (((uint32_t)(p)[0]<<24)|((uint32_t)(p)[1]<<16)|((uint32_t)(p)[2]<<8)|(p)[3])
#define os_memcmp memcmp
struct ieee802_1x_hdr{u8 version,type,length[2];};
struct wpabuf{int value;};
struct sta_info{struct sta_info *next;void *lock,*eapol_sm,*wpa_sm;struct wpabuf *wps_ie;u8 addr[6];};
struct hostapd_data{struct sta_info *sta_list;void *wps,*wpa_auth;};
static struct hostapd_data h;
static struct sta_info peer;
static bool native=true,available=true,table_lock,closing,oom,overlap,eap_oom;
static int owner=WPS_OWNER_REGISTRAR,type=WPS_TYPE_PBC,activity,received,wpa_received,live,concat_calls,reported,response_error;
static u8 ie[]={221,4,0,0x50,0xf2,4};
#define HOSTAPD_STA_LIST_LOCK(hapd) do{(void)hapd;assert(!table_lock);table_lock=true;}while(0)
#define HOSTAPD_STA_LIST_UNLOCK(hapd) do{(void)hapd;assert(table_lock);table_lock=false;}while(0)
bool current_task_is_wifi_task(void){return native;}
static struct hostapd_data *hostapd_get_hapd_data(void){return &h;}
static bool os_semphr_take(void *p,int wait){(void)wait;assert(table_lock);bool got=*(bool*)p;if(got)*(bool*)p=false;return got;}
static uint32_t esp32qjs_wps_ap_result_identity(const void *p){return p==&h?7:0;}
static void *esp32qjs_wps_ap_result_activity_enter(uint32_t id){if(id!=7 || closing)return NULL;activity++;return &h;}
static void esp32qjs_wps_ap_result_callback_leave(uint32_t id){assert(id==7 && activity);activity--;}
static void esp32qjs_wps_ap_result_peer_error(uint32_t id,int e,const u8 *a){assert(id==7 && a && activity);reported=e;}
static int wps_get_owner(void){return owner;}
static int esp_wifi_get_wps_type_internal(void){return type;}
static void esp32qjs_wps_ap_receive_pinned(void *p,void *s,const u8 *data,size_t len){assert(p==&h && s==&peer && data && len>=4 && !available && !table_lock && activity);received++;}
static void wpa_receive(void *a,void *s,u8 *data,size_t len){(void)a;(void)s;assert(!available && !table_lock && data && len>=4);wpa_received++;}
static void esp32qjs_wps_ap_peer_release(void *p,void *s){assert(p==&h && s==&peer && !available);available=true;}
static struct wpabuf *ieee802_11_vendor_ie_concat(const u8*,size_t,unsigned);
static void wpabuf_free(struct wpabuf *p){if(p){live--;free(p);}}
static bool esp_wps_registrar_check_pbc_overlap(void *p){assert(p==&h);return overlap;}
static void ieee802_1x_free_station(struct hostapd_data *p,struct sta_info *s){assert(p==&h);s->eapol_sm=NULL;}
static void *ieee802_1x_alloc_eapol_sm(struct hostapd_data *p,struct sta_info *s){assert(p==&h && s==&peer);return eap_oom?NULL:(void*)1;}
static int esp_send_assoc_resp(struct hostapd_data *p,const u8 *a,int status,bool wps,int subtype){(void)subtype;assert(p==&h && a && !status && wps);return response_error;}
'''
BOUNDARIES=r'''
static void setup(void){peer.lock=&available;h.sta_list=&peer;h.wps=&h;}
static struct wpabuf *new_ie(void){struct wpabuf *p=calloc(1,sizeof(*p));assert(p);live++;return p;}
static struct wpabuf *ieee802_11_vendor_ie_concat(const u8 *data,size_t len,unsigned oui){assert(data && len==sizeof(ie) && oui==WPS_DEV_OUI_WFA);concat_calls++;return oom?NULL:new_ie();}
'''
