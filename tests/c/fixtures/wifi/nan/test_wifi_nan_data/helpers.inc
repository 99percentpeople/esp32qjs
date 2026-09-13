
static uint8_t data_descriptors[64][16],data_frames[64][48];
void *data_buffer(unsigned i,bool group){
 assert(i<64);memset(buffers[i],0,sizeof(buffers[i]));
 uint8_t *descriptor=data_descriptors[i],*frame=data_frames[i],*meta=metadata[i];
 memcpy(buffers[i]+4,&descriptor,sizeof(descriptor));memcpy(descriptor+4,&frame,sizeof(frame));
 memcpy(buffers[i]+56,&meta,sizeof(meta));uint16_t length=24;memcpy(buffers[i]+20,&length,2);
 frame[4]=group?1:2;return buffers[i];
}
uint32_t data_bind(uint8_t *ndl,uint8_t id){
 wifi_nan_datapath_req_t request={.pub_id=3};memcpy(request.peer_mac,ndl,6);
 uint32_t identity=0,bound=0;
 assert(!esp32_mquickjs_wifi_nan_ndp_begin(7,&request,&identity));
 assert(esp32_mquickjs_wifi_nan_ndp_request_enter(&request)==identity);
 assert(!esp32_mquickjs_wifi_nan_ndp_native_claim(ndl,id,&bound)&&bound==identity);
 esp32_mquickjs_wifi_nan_ndp_native_bound(identity,true);
 esp32_mquickjs_wifi_nan_ndp_request_leave(identity,0);esp32_mquickjs_wifi_nan_ndp_submitted(identity,0);
 esp32_mquickjs_wifi_nan_sdk_notice_t notice={.kind=ESP32_MQUICKJS_NAN_SDK_NDP_ACCEPTED,.ndp_id=id};
 memcpy(notice.peer,ndl,6);esp32_mquickjs_wifi_nan_ndp_notice(&notice);return identity;
}
