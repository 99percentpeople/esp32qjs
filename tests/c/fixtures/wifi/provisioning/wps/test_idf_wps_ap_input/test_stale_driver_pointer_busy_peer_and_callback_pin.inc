
int main(void){
 setup();u8 packet[4]={1,0,0,0};
 native=false;assert(!wpa_ap_rx_eapol(&h,&peer,packet,4) && !received && available);native=true;
 assert(!wpa_ap_rx_eapol(&h,(void*)0xdead,packet,sizeof(packet)) && !received && !wpa_received);
 available=false;assert(!wpa_ap_rx_eapol(&h,&peer,packet,sizeof(packet)) && !received);
 available=true;assert(wpa_ap_rx_eapol(&h,&peer,packet,sizeof(packet)));
 assert(received==1 && !activity && available && !table_lock);
 assert(!wpa_ap_rx_eapol(&h,&peer,packet,3));return 0;
}
