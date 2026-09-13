
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
