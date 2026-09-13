
int main(void){
 struct hostapd_bss_config conf={0};struct wpa_authenticator auth={.conf={WPA_PROTO_RSN,WPA_KEY_MGMT_PSK,WPA_CIPHER_TKIP,WPA_CIPHER_CCMP}};
 struct wps_context context={0};struct wps_data data={.wps=&context};struct hostapd_data hapd={.conf=&conf,.wpa_auth=&auth};
 char pass[66];memset(pass,'a',64);pass[64]=0;conf.ssid.wpa_passphrase=pass;conf.ssid.ssid_len=32;memset(conf.ssid.ssid,0xff,32);conf.ssid.ssid[1]=0;
 assert(hostapd_wps_config_ap(&hapd,NULL)==ESP_ERR_INVALID_ARG);
 assert(hostapd_wps_config_ap(&hapd,&data)==ESP_OK && data.use_cred && data.use_cred->key_len==64 && data.use_cred->ssid_len==32);
 assert(!memcmp(data.use_cred->ssid,conf.ssid.ssid,32) && data.use_cred->encr_type==WPS_ENCR_AES && data.use_cred->auth_type==WPS_AUTH_WPA2PSK);
 struct wps_credential *previous=data.use_cred;struct wps_context saved=context;struct wps_credential old=*previous;
 for(int mode=0;mode<7;mode++){
   size_t ssid_len=conf.ssid.ssid_len;int auth_mode=auth.conf.wpa_key_mgmt,cipher=auth.conf.rsn_pairwise;
   if(mode==0)oom=true;
   if(mode==1)conf.ssid.ssid_len=33;
   if(mode==2)auth.conf.wpa_key_mgmt=WPA_KEY_MGMT_SAE;
   if(mode==3)auth.conf.rsn_pairwise=0;
   if(mode==4){memset(pass,'a',65);pass[65]=0;}
   if(mode==5)pass[2]='g'; /* 64-byte keys must be hex. */
   if(mode==6)pass[7]=0;
   unsigned before=clears;assert(hostapd_wps_config_ap(&hapd,&data)!=ESP_OK && clears>before);
   assert(data.use_cred==previous && !memcmp(&context,&saved,sizeof(saved)) && !memcmp(data.use_cred,&old,sizeof(old)));
   oom=false;conf.ssid.ssid_len=ssid_len;auth.conf.wpa_key_mgmt=auth_mode;auth.conf.rsn_pairwise=cipher;memset(pass,'a',64);pass[64]=0;
 }
 conf.ssid.ssid_len=3;memcpy(conf.ssid.ssid,"new",3);strcpy(pass,"new-pass");unsigned before=freed;
 assert(hostapd_wps_config_ap(&hapd,&data)==ESP_OK && freed==before+1 && data.use_cred!=previous);
 assert(data.use_cred->ssid_len==3 && data.use_cred->key_len==8 && context.ssid_len==3);
 for(unsigned i=3;i<32;i++)assert(!data.use_cred->ssid[i] && !context.ssid[i]);
 for(unsigned i=8;i<64;i++)assert(!data.use_cred->key[i]);
 auth.conf.wpa=WPA_PROTO_WPA|WPA_PROTO_RSN;auth.conf.wpa_key_mgmt=WPA_KEY_MGMT_PSK|WPA_KEY_MGMT_SAE;
 assert(!hostapd_wps_config_ap(&hapd,&data) && data.use_cred->auth_type==(WPS_AUTH_WPAPSK|WPS_AUTH_WPA2PSK));
 assert(data.use_cred->encr_type==(WPS_ENCR_AES|WPS_ENCR_TKIP));
 bin_clear_free(data.use_cred,sizeof(*data.use_cred));return 0;
}
