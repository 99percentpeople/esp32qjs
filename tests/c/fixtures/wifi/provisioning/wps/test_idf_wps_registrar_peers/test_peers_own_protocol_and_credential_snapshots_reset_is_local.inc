
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
