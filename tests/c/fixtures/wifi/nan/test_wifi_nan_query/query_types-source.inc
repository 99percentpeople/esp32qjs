
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
#define NAN_MAX_PEERS_RECORD 15
#define ESP_NAN_PUBLISH 2
#define ESP_NAN_SUBSCRIBE 1
struct nan_peer_record {
 uint8_t peer_svc_id,own_svc_id,peer_nmi[6],peer_svc_type,ndp_id,peer_ndi[6];
};
#endif
