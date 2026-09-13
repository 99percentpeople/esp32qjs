
int main(void){
 for(int failure=1;failure<=5;failure++){
   struct hostapd_bss_config conf={0};struct hostapd_data h={.conf=&conf};struct wps_context ctx={0};struct wps_data data={.wps=&ctx};
   fail_step=failure;step=0;
   assert(hostapd_init_wps(&h,&data,&ctx)!=ESP_OK);
   assert(!h.wps && !ctx.registrar && !registered && !registrars && !borrowed_frees);
 }
 struct hostapd_bss_config conf={0};struct hostapd_data h={.conf=&conf};struct wps_context ctx={0};struct wps_data data={.wps=&ctx};
 step=fail_step=0;assert(hostapd_init_wps(&h,&data,&ctx)==ESP_OK && h.wps==&ctx && registered==2 && registrars==1);
 wps_registrar_deinit(ctx.registrar);eap_server_unregister_methods();return 0;
}
