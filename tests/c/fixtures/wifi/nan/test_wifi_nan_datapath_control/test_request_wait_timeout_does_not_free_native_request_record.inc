
int main(void){
 wifi_nan_datapath_req_t req={.pub_id=3,.peer_mac={2,3,4,5,6,7}};
 assert(!esp_wifi_nan_datapath_req(&req));
 assert(!depth&&submitted&&live_ndl&&!resets);
}
