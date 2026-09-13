
int main(void){
 struct own_svc_info *own=&s_nan_ctx.own_svc[0];own->svc_id=7;own->type=ESP_NAN_PUBLISH;strcpy(own->svc_name,"alpha");
 esp32_mquickjs_wifi_nan_query_t q={.kind=ESP32_MQUICKJS_NAN_QUERY_SERVICE,.service_name="alpha"};
 esp32_mquickjs_wifi_nan_query_result_t out;memset(&out,0xa5,sizeof(out));
 assert(!esp32_mquickjs_wifi_nan_sdk_query(&q,&out)&&out.service_id==7&&!out.peer_count);
 assert_clear(out.peers,sizeof(out.peers));assert(locks==1&&!locked);
 q.service_name=NULL;q.service_id=8;
 assert(esp32_mquickjs_wifi_nan_sdk_query(&q,&out)==ESP_ERR_NOT_FOUND);assert_clear(&out,sizeof(out));
 q.service_id=7;q.service_name="alpha";unsigned before=locks;
 assert(esp32_mquickjs_wifi_nan_sdk_query(&q,&out)==ESP_ERR_INVALID_ARG&&locks==before);
 q.service_name=NULL;q.kind=ESP32_MQUICKJS_NAN_QUERY_PEERS;
 struct peer_svc_info peers[16]={0};
 for(unsigned i=0;i<15;i++){
  peers[i]=(struct peer_svc_info){.svc_id=i+1,.own_svc_id=7,.type=ESP_NAN_SUBSCRIBE,.peer_nmi={2,3,4,5,6,7}};
  SLIST_INSERT_HEAD(&own->peer_list,&peers[i],next);++own->num_peer_records;
 }
 s_nan_ctx.ndl[0]=(struct ndl_info){.ndp_id=9,.publisher_id=8,.own_role=ESP_NAN_PUBLISH,.peer_nmi={2,3,4,5,6,7},.peer_ndi={2,9}};
 assert(!esp32_mquickjs_wifi_nan_sdk_query(&q,&out)&&out.peer_count==15);
 for(unsigned i=0;i<15;i++){assert(!out.peers[i].ndp_id);assert_clear(out.peers[i].peer_ndi,6);}
 q.kind=ESP32_MQUICKJS_NAN_QUERY_PEER;memcpy(q.peer,peers[0].peer_nmi,6);
 assert(!esp32_mquickjs_wifi_nan_sdk_query(&q,&out)&&!out.peers[0].ndp_id);
 s_nan_ctx.ndl[1]=s_nan_ctx.ndl[0];s_nan_ctx.ndl[1].publisher_id=7;s_nan_ctx.ndl[1].ndp_id=10;
 assert(!esp32_mquickjs_wifi_nan_sdk_query(&q,&out)&&out.peers[0].ndp_id==10);
 assert(out.peers[0].peer_svc_id==15&&out.peers[0].peer_ndi[1]==9);
 own->type=ESP_NAN_SUBSCRIBE;s_nan_ctx.ndl[1].own_role=ESP_NAN_SUBSCRIBE;s_nan_ctx.ndl[1].publisher_id=15;
 for(unsigned i=0;i<15;i++)peers[i].type=ESP_NAN_PUBLISH;
 managed_match=false;assert(!esp32_mquickjs_wifi_nan_sdk_query(&q,&out)&&!out.peers[0].ndp_id);
 managed_match=true;assert(!esp32_mquickjs_wifi_nan_sdk_query(&q,&out)&&out.peers[0].ndp_id==10);
 q.service_id=0;assert(!esp32_mquickjs_wifi_nan_sdk_query(&q,&out)&&out.service_id==7);
 q.peer[5]=8;assert(esp32_mquickjs_wifi_nan_sdk_query(&q,&out)==ESP_ERR_NOT_FOUND);assert_clear(&out,sizeof(out));
 q.peer[0]=0;q.peer[5]=0;memset(q.peer,0,6);before=locks;
 assert(esp32_mquickjs_wifi_nan_sdk_query(&q,&out)==ESP_ERR_INVALID_ARG&&before==locks);
 q.kind=ESP32_MQUICKJS_NAN_QUERY_PEERS;q.service_id=7;
 --own->num_peer_records;assert(esp32_mquickjs_wifi_nan_sdk_query(&q,&out)==ESP_ERR_INVALID_STATE);assert_clear(&out,sizeof(out));
 ++own->num_peer_records;SLIST_INSERT_HEAD(&own->peer_list,&peers[15],next);
 assert(esp32_mquickjs_wifi_nan_sdk_query(&q,&out)==ESP_ERR_INVALID_STATE);assert_clear(&out,sizeof(out));
 SLIST_REMOVE_HEAD(&own->peer_list,next);peers[0].next.sle_next=&peers[14];
 assert(esp32_mquickjs_wifi_nan_sdk_query(&q,&out)==ESP_ERR_INVALID_STATE);assert_clear(&out,sizeof(out));
 peers[0].next.sle_next=NULL;memset(own->svc_name,'x',sizeof(own->svc_name));
 assert(esp32_mquickjs_wifi_nan_sdk_query(&q,&out)==ESP_ERR_INVALID_STATE);assert_clear(&out,sizeof(out));
 s_nan_data_lock=NULL;before=locks;
 assert(esp32_mquickjs_wifi_nan_sdk_query(&q,&out)==ESP_ERR_INVALID_STATE&&before==locks);
 assert(!locked);
}
