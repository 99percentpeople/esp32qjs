
int main(void) {
    uint8_t packet=8;
    esp32_mquickjs_wifi_monitor_info_t info={.driver={.available=true,
        .type=ESP32_MQUICKJS_WIFI_PACKET_DATA,.driver_length=5,.primary=6,.rssi=-61,.noise_floor=-96},
        .callback_time_us=UINT64_C(4294967316),.readable_length=1,.captured_length=1,.truncated=true};
    esp32_mquickjs_wifi_rx_parse_header(&packet,1,&info.header);
    esp32_mquickjs_wifi_monitor_wire_snapshot_t records[2];
    assert(esp32_mquickjs_wifi_monitor_wire_snapshot(&info,42,13,17,&records[0]));
    info=(esp32_mquickjs_wifi_monitor_info_t){.driver={.available=true,
        .type=ESP32_MQUICKJS_WIFI_PACKET_MISC,.primary=6},.metadata_only=true,.callback_time_us=UINT64_C(4294967317)};
    assert(esp32_mquickjs_wifi_monitor_wire_snapshot(&info,43,13,17,&records[1]));
    esp32_mquickjs_wifi_rx_wire_frame_t frames[2]={records[0].frame,records[1].frame};
    uint8_t wire[596]={0};
    assert(esp32_mquickjs_wifi_rx_wire_write_control(ESP32_MQUICKJS_WIFI_RX_WIRE_MONITOR,frames,2,wire,sizeof(wire)));
    for(unsigned i=0;i<2;i++)assert(esp32_mquickjs_wifi_rx_wire_write_metadata(ESP32_MQUICKJS_WIFI_RX_WIRE_MONITOR,
        &frames[i],&records[i].metadata,wire+80+i*256,256));
    wire[592]=packet;
    assert(fwrite(wire,1,sizeof(wire),stdout)==sizeof(wire));return 0;
}
