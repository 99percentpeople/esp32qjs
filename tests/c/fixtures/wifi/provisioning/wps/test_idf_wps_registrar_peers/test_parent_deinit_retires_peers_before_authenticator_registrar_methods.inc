
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
