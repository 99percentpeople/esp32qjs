
int main(void){
 setup(false);esp32_mquickjs_wifi_nan_path_t*p=NULL;
 wifi_nan_datapath_req_t req={.pub_id=3,.peer_mac={2,3,4,5,6,7},.confirm_required=true};
 assert(!esp32_mquickjs_wifi_nan_path_create(d,&req,250,&p));
 assert(!esp32_mquickjs_wifi_nan_path_activate(p));tick();
 clock_us+=250000;path_end_error=-73;close_error=ESP_ERR_TIMEOUT;tick();
 assert(p->status.timed_out&&p->status.closing&&!p->status.retired);
 assert(path_ends==1&&s->status.closing&&!s->status.retired);
 tick();assert(path_ends==1&&!p->status.retired);
 close_error=0;tick();assert(p->status.retired&&s->status.retired);
 esp32_mquickjs_wifi_nan_path_release(p);finish();
}
