
int main(void){
 struct hostapd_bss_config conf={0};struct hostapd_data h={.conf=&conf};struct wps_context ctx={0};struct wps_data data={.wps=&ctx};
 h.wps=&ctx;sm.wps=&data;sm.wps_ctx=&ctx;ctx.ssid_len=3;memcpy(ctx.ssid,"old",3);conf.ssid.ssid_len=3;memcpy(conf.ssid.ssid,"new",3);
 hostapd_update_wps(&h);assert(captures==1 && null_captures==(PATCHED?0:1) && ie_updates==1);
 if(PATCHED){
   unsigned old_updates=ie_updates;capture_error=ESP_ERR_NO_MEM;hostapd_update_wps(&h);assert(ie_updates==old_updates && !memcmp(ctx.ssid,"old",3));
   unsigned old_captures=captures;sm.wps_ctx=NULL;hostapd_update_wps(&h);assert(captures==old_captures);
 }
 return 0;
}
