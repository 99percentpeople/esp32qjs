
int main(void){
 struct hostapd_bss_config conf={0};struct hostapd_data h={.conf=&conf};
 struct wps_context ctx={0};struct wps_data data={.wps=&ctx};
 fail_step=2;assert(hostapd_init_wps(&h,&data,&ctx)==-83);
 assert(!registered && !registrars && !borrowed_frees && !ie_clears);
 return 0;
}
