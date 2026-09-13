
static unsigned queries,protocol_reads,width_reads;
static int protocol_error,width_error;
static esp_err_t __wrap_wifi_action_tx_process(void *);
static int wifi_get_protocols_process(void *opaque) {
    action_sdk_phy_message_t *message=opaque;
    assert(ioctl_task && message->interface<=1 && message->ghz_2g && message->ghz_5g);
    ++protocol_reads;*(uint16_t *)message->ghz_2g=7;*(uint16_t *)message->ghz_5g=20;
    return protocol_error;
}
static int wifi_get_bw_process(void *opaque) {
    action_sdk_phy_message_t *message=opaque;
    assert(ioctl_task && message->interface<=1 && message->ghz_2g && message->ghz_5g);
    ++width_reads;*(wifi_bandwidth_t *)message->ghz_2g=WIFI_BW40;*(wifi_bandwidth_t *)message->ghz_5g=WIFI_BW20;
    return width_error;
}
static int esp_wifi_action_tx_req(wifi_action_tx_req_t *request) {
    uint8_t message[32]={0};memcpy(message+20,&request,sizeof(request));
    assert(!ioctl_task);++queries;ioctl_task=true;
    int error=__wrap_wifi_action_tx_process(message);ioctl_task=false;return error;
}
