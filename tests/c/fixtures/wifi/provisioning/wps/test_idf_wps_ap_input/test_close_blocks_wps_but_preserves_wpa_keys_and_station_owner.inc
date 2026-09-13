
int main(void){
 setup();u8 packet[4]={1,0,0,0};closing=true;
 assert(!wpa_ap_rx_eapol(&h,&peer,packet,4) && !received);
 packet[1]=IEEE802_1X_TYPE_EAPOL_KEY;
 assert(wpa_ap_rx_eapol(&h,&peer,packet,4) && wpa_received==1 && !received);
 owner=1;packet[1]=0;
 assert(wpa_ap_rx_eapol(&h,&peer,packet,4) && wpa_received==2 && !received);
 assert(!activity && available);return 0;
}
