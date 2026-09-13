
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
