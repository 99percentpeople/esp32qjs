
int main(void) {
 struct hostapd_config conf={0};struct hostapd_data h={.conf=&conf,.wpa_auth=&conf};global_hapd=&h;
 close_error=0x456;
 bool result=hostap_deinit(&h);
 assert(result==!PATCHED && frees==(PATCHED?0:1));
 if(PATCHED){
  assert(!unset && h.conf==&conf && h.wpa_auth==&conf && global_hapd==&h);
  hostapd_cleanup(&h);assert(!frees && h.conf==&conf);
  close_error=0;assert(hostap_deinit(&h) && frees==1 && !global_hapd);
  global_hapd=&h;owner=WPS_OWNER_ENROLLEE;bound=false;int before=disable_calls;
  assert(hostap_deinit(&h) && disable_calls==before && frees==2);
 }
 assert(!public_disable);return 0;
}
