
int main(void){
 struct hostapd_bss_config conf={0};struct hostapd_data h={.conf=&conf};struct wps_context ctx={0};struct wps_data data={.wps=&ctx};
 fail_step=5;assert(hostapd_init_wps(&h,&data,&ctx)==0 && h.wps==&ctx); /* EAPOL failure discarded. */
 wps_registrar_deinit(ctx.registrar);memset(&ctx,0,sizeof(ctx));h.wps=NULL;step=registered=0;
 fail_step=2;assert(hostapd_init_wps(&h,&data,&ctx)==-1 && borrowed_frees==1);
 return 0;
}
