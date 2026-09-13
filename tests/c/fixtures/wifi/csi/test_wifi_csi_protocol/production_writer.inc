
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp32_mquickjs_wifi_csi_wire.h"
int main(int argc,char **argv) {
    int scenario=argc>1?atoi(argv[1]):0;
    uint8_t payload[]={1,2};
    esp32_mquickjs_wifi_csi_slot_t slot={.session_generation=40,.sequence=10,.length=2,.payload=payload};
    slot.metadata=(esp32_mquickjs_wifi_csi_metadata_t){.addresses={{2,0,0,0,0,1},{255,255,255,255,255,255}},.address_mask=3,
        .timestamp_us=UINT64_C(0x100000014),.radio_generation=50,.rx_sequence=30,
        .rssi=-61,.noise_floor=-96,.noise_floor_available=true,.channel=6,.secondary=1,
        .antenna=0,.antenna_available=true,.phy=1,.bandwidth_mhz=40,.bandwidth_available=true,
        .mcs=3,.mcs_available=true,.stbc_available=true,.stbc=true,.first_word_invalid=true,
        .channel_estimate_valid_available=true,.channel_estimate_valid=true,
        .layout={.schema=1,.sample_encoding=1,.sample_bits=8,.byte_length=2,.iq_pair_count=1,.segment_count=1,.known=true,
            .segments={{.type=2,.length_bytes=2,.iq_pair_count=1,.subcarrier_range_count=1,.subcarrier_ranges={{-1,-1}}}}}};
    if(scenario==1) slot.metadata.guard_interval_ns=400;
    if(scenario>=2 && scenario<=4) {
        slot.metadata.phy=scenario==4?ESP32_MQUICKJS_WIFI_CSI_PHY_HE_MU:ESP32_MQUICKJS_WIFI_CSI_PHY_HE_SU;
        slot.metadata.guard_interval_ns=scenario==2?800:scenario==3?1600:3200;
        slot.metadata.he_ltf_size=scenario==3?2:4;
        slot.metadata.dcm_state=scenario==2?1:scenario==3?2:0;
        slot.metadata.stbc=false;
    }
    uint8_t packet_bytes[40]={8};esp32_mquickjs_wifi_csi_packet_t packet;
    if(scenario>=5){
        esp32_mquickjs_wifi_csi_packet_options_t options={.mode=scenario==6?ESP32_MQUICKJS_WIFI_CSI_PACKET_HEADER:ESP32_MQUICKJS_WIFI_CSI_PACKET_FULL,.snap_length=37};
        esp32_mquickjs_wifi_csi_packet_input_t input={packet_bytes,scenario==7?30:40,40,29};
        assert(esp32_mquickjs_wifi_csi_packet_prepare(&options,&input,&packet)==ESP32_MQUICKJS_WIFI_CSI_PACKET_OK);
        slot.packet=&packet;
    }
    esp32_mquickjs_wifi_csi_wire_snapshot_t s;
    assert(esp32_mquickjs_wifi_csi_wire_snapshot(&slot,&s));
    esp32_mquickjs_wifi_rx_wire_layout_t layout;
    esp32_mquickjs_wifi_rx_wire_offsets_t offsets;
    assert(esp32_mquickjs_wifi_rx_wire_layout(ESP32_MQUICKJS_WIFI_RX_WIRE_CSI,&s.frame,1,&layout));
    assert(esp32_mquickjs_wifi_rx_wire_offsets(ESP32_MQUICKJS_WIFI_RX_WIRE_CSI,&s.frame,1,0,&offsets));
    uint8_t bytes[512]={0};assert(layout.total_bytes<=sizeof(bytes));
    assert(esp32_mquickjs_wifi_rx_wire_write_control(ESP32_MQUICKJS_WIFI_RX_WIRE_CSI,&s.frame,1,bytes,sizeof(bytes)));
    assert(esp32_mquickjs_wifi_rx_wire_write_metadata(ESP32_MQUICKJS_WIFI_RX_WIRE_CSI,&s.frame,&s.metadata,bytes+72,256));
    memcpy(bytes+328,payload,2);
    if(slot.packet)memcpy(bytes+offsets.packet_offset,slot.packet->bytes,slot.packet->length);
    assert(fwrite(bytes,1,layout.total_bytes,stdout)==layout.total_bytes);
}
