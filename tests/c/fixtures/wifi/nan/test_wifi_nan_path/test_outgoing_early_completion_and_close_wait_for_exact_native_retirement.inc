
int main(void){
 setup(false);path_inline=true;esp32_mquickjs_wifi_nan_path_t*p=NULL;
 wifi_nan_datapath_req_t req={.pub_id=3,.peer_mac={2,3,4,5,6,7},.confirm_required=true};
 assert(!esp32_mquickjs_wifi_nan_path_create(d,&req,1000,&p)&&!path_requests);
 assert(!esp32_mquickjs_wifi_nan_path_activate(p));tick();
 esp32_mquickjs_wifi_nan_path_status_t state;esp32_mquickjs_wifi_nan_path_status(p,&state);
 assert(state.ready&&state.native.accepted&&state.native.identity==100&&path_requests==1);
 esp32_mquickjs_wifi_nan_path_close(p,false);tick();assert(path_ends==1&&!p->status.retired);
 path_native[0].native_deleted=true;tick();assert(!p->status.retired&&path_releases==1);
 path_native[0].frames_retired=true;tick();assert(p->status.retired&&path_releases==2);
 assert(!s->status.closing&&!s->paths[0]&&!s->paths[1]);
 esp32_mquickjs_wifi_nan_path_release(p);finish();
}
