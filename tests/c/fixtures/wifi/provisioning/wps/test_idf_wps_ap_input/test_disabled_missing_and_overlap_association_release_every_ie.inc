
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
