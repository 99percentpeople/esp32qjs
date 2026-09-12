"""Deferred production Monitor snapshot/PHY/wire tests using recorded SDK enums."""
import json
import re
import unittest
from test_wifi_rx_target import ROOT, INTERNAL, COMMON, unit
from test_wireless_control_regression import compile_run

MONITOR = ROOT / 'components/esp32_mquickjs/src/modules/wifi_monitor'


def production_monitor_wire(he=False):
    body = '#define CONFIG_ESP32_MQUICKJS_WIFI_RADIO 1\n'
    body += '#define CONFIG_SOC_WIFI_HE_SUPPORT ' + str(int(he)) + '\n'
    body += 'typedef int wifi_promiscuous_pkt_type_t;\n'
    if he:
        symbols = json.loads((ROOT / 'docs/idf-wifi-api-inventory.json').read_text())['variants']['esp32c5/representative']['symbols']
        body += next(v['declaration'] for k, v in symbols.items() if k.endswith('::wifi_rx_bb_format_t')) + ';\n'
    for name in ['esp32_mquickjs_wifi_rx.h', 'esp32_mquickjs_wifi_rx_target.h',
                 'esp32_mquickjs_wifi_rx_wire.h', 'esp32_mquickjs_wifi_csi_layout.h',
                 'esp32_mquickjs_wifi_rx_wire_metadata.h']:
        body += unit(INTERNAL / name)
    resources = (INTERNAL / 'esp32_mquickjs_wifi_monitor_resources.h').read_text()
    body += re.search(r'typedef struct \{\n    esp32_mquickjs_wifi_rx_driver_metadata_t driver;.*?\} esp32_mquickjs_wifi_monitor_info_t;',
                      resources, re.S).group(0)
    body += unit(INTERNAL / 'esp32_mquickjs_wifi_rx_vht_signal.h')
    body += unit(INTERNAL / 'esp32_mquickjs_wifi_rx_he_signal.h')
    body += unit(INTERNAL / 'esp32_mquickjs_wifi_monitor_wire.h')
    for name in ['esp32_mquickjs_wifi_rx.c', 'esp32_mquickjs_wifi_rx_wire.c', 'esp32_mquickjs_wifi_rx_wire_metadata.c']:
        body += unit(COMMON / name)
    return body + unit(MONITOR / 'esp32_mquickjs_wifi_monitor_wire.c')


class WiFiMonitorWire(unittest.TestCase):
    def test_legacy_and_he_snapshot_facts_lengths_phy_and_atomic_rejection(self):
        for he in [False, True]:
            with self.subTest(he=he):
                compile_run(self, PRELUDE + production_monitor_wire(he) + MAIN)


PRELUDE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
'''
MAIN = r'''
typedef esp32_mquickjs_wifi_monitor_info_t info_t;
typedef esp32_mquickjs_wifi_monitor_wire_snapshot_t snapshot_t;
static uint32_t u32(const uint8_t *p) {return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static info_t observation(void) {
    uint8_t packet[32]={0x08,0x09};
    memset(packet+4,0x11,6);memset(packet+10,0x22,6);memset(packet+16,0x33,6);
    packet[22]=0x34;packet[23]=0x12;
    info_t info={0};
    assert(esp32_mquickjs_wifi_rx_parse_header(packet,sizeof(packet),&info.header)==ESP32_MQUICKJS_WIFI_RX_PARSED);
    info.driver=(esp32_mquickjs_wifi_rx_driver_metadata_t){.available=true,
        .type=ESP32_MQUICKJS_WIFI_PACKET_DATA,.rssi=-61,.noise_floor=-94,
        .primary=6,.secondary_raw=1,.raw_format=1,.timestamp_us=100,
        .ht_fields_available=true,.mcs=7,.bandwidth_mhz=40,.antenna_available=true,.antenna=1,
        .ldpc=true,.stbc=true,.short_gi=true,.aggregation=true,.smoothing=false,.sounding=true,
        .ampdu_count=3,.driver_length=36};
#if CONFIG_SOC_WIFI_HE_SUPPORT
    info.driver.he_layout=true;info.driver.raw_format=RX_BB_FORMAT_HE_SU;
    info.driver.ht_fields_available=info.driver.antenna_available=false;
#endif
    info.callback_time_us=UINT64_C(4294967396);info.readable_length=32;
    info.captured_length=1;info.truncated=true;info.header_type_matches=true;
    return info;
}
static void rejected(const info_t *info,uint32_t sequence,uint32_t session,uint32_t radio) {
    snapshot_t out,before;memset(&out,0xa5,sizeof(out));memcpy(&before,&out,sizeof(out));
    assert(!esp32_mquickjs_wifi_monitor_wire_snapshot(info,sequence,session,radio,&out));
    assert(!memcmp(&out,&before,sizeof(out)));
}
int main(void) {
    info_t info=observation();snapshot_t s;
    assert(esp32_mquickjs_wifi_monitor_wire_snapshot(&info,42,13,17,&s));
    assert(s.frame.sequence==42 && !s.frame.csi_length && s.frame.packet_length==1);
    assert(s.frame.packet_readable_length==32 && s.frame.captured_header_length==1 && s.frame.flags==27);
    assert(!s.frame.driver_payload_length && !s.metadata.driver_payload_length_available);
    assert(s.metadata.driver_packet_length==36 && s.metadata.driver_packet_length_available);
    assert(s.metadata.timestamp_us==UINT64_C(4294967396) && s.metadata.timestamp_accuracy==2);
    assert(s.metadata.rx_sequence==UINT32_MAX && s.metadata.session_generation==13 && s.metadata.radio_generation==17);
    assert(s.metadata.csi_layout==NULL && s.metadata.rssi==-61 && s.metadata.noise_floor==-94);
    assert(s.metadata.address_mask==31 && s.metadata.addresses[0][0]==0x22 && s.metadata.addresses[1][0]==0x33);
    assert(s.metadata.header_length==24 && s.metadata.frame_control==0x0908 && s.metadata.sequence_control==0x1234);
    assert(s.metadata.legacy_rate==255 && s.metadata.signal_mode==255 && s.metadata.fcs_state==0);
#if CONFIG_SOC_WIFI_HE_SUPPORT
    assert(s.metadata.phy==3 && s.metadata.mcs==0 && s.metadata.antenna==255);
    assert(s.metadata.rx_flags==(ESP32_MQUICKJS_WIFI_RX_WIRE_NOISE_AVAILABLE |
        ESP32_MQUICKJS_WIFI_RX_WIRE_MCS_AVAILABLE | ESP32_MQUICKJS_WIFI_RX_WIRE_BANDWIDTH_AVAILABLE |
        ESP32_MQUICKJS_WIFI_RX_WIRE_STBC_AVAILABLE | ESP32_MQUICKJS_WIFI_RX_WIRE_FEC_AVAILABLE |
        ESP32_MQUICKJS_WIFI_RX_WIRE_DCM_AVAILABLE));
    assert(s.metadata.bandwidth_mhz==20 && s.metadata.ampdu_count==255);
    assert(s.metadata.guard_interval_ns==800 && s.metadata.he_ltf_size==1);
#else
    assert(s.metadata.phy==1 && s.metadata.mcs==7 && s.metadata.antenna==1 && s.metadata.bandwidth_mhz==40);
    assert(s.metadata.ampdu_count==3);
    assert(s.metadata.guard_interval_ns==400 && !s.metadata.he_ltf_size);
    assert((s.metadata.rx_flags & ESP32_MQUICKJS_WIFI_RX_WIRE_SMOOTHING_AVAILABLE)!=0);
    assert((s.metadata.rx_flags & ESP32_MQUICKJS_WIFI_RX_WIRE_SMOOTHING)==0);
#endif
    uint8_t control[312],encoded[256];
    assert(esp32_mquickjs_wifi_rx_wire_write_control(ESP32_MQUICKJS_WIFI_RX_WIRE_MONITOR,&s.frame,1,control,sizeof(control)));
    assert(esp32_mquickjs_wifi_rx_wire_write_metadata(ESP32_MQUICKJS_WIFI_RX_WIRE_MONITOR,&s.frame,&s.metadata,encoded,sizeof(encoded)));
    assert(!u32(control+44) && u32(control+48)==32 && !u32(encoded+104) && u32(encoded+108)==36);
    assert(u32(encoded+112)==1 && u32(encoded+116)==0x5a13d);
    /* All layout slots/reserved tail remain zero for Monitor. */
    for(unsigned i=128;i<256;i++)assert(!encoded[i]);
    /* Copying the entire readable span still truncates a longer driver report. */
    info.captured_length=32;
    assert(esp32_mquickjs_wifi_monitor_wire_snapshot(&info,42,13,17,&s));
    assert(s.frame.flags==27);
    info.truncated=false;rejected(&info,42,13,17);
    info.driver.driver_length=32;
    assert(esp32_mquickjs_wifi_monitor_wire_snapshot(&info,42,13,17,&s));
    assert(s.frame.captured_header_length==24 && s.frame.flags==25);
    info=observation();info.header_type_matches=false;info.driver.type=ESP32_MQUICKJS_WIFI_PACKET_MANAGEMENT;
    assert(esp32_mquickjs_wifi_monitor_wire_snapshot(&info,42,13,17,&s));
    assert(s.frame.flags==19 && s.metadata.address_mask==0 && s.metadata.sequence_available && s.metadata.packet_type==1);
    info=observation();info.metadata_only=true;info.driver.type=ESP32_MQUICKJS_WIFI_PACKET_MISC;
    info.captured_length=info.readable_length=0;info.truncated=false;
    /* Ignore all stale header storage for a metadata-only record. */
    assert(esp32_mquickjs_wifi_monitor_wire_snapshot(&info,42,13,17,&s));
    assert(!s.frame.flags && !s.frame.packet_length && !s.frame.packet_readable_length && !s.metadata.address_mask);
    assert(s.metadata.packet_type==4 && !s.metadata.capture_mode && !s.metadata.frame_control_available);
    assert(esp32_mquickjs_wifi_rx_wire_write_metadata(ESP32_MQUICKJS_WIFI_RX_WIRE_MONITOR,&s.frame,&s.metadata,encoded,sizeof(encoded)));
    assert(u32(encoded+116)==(1U<<18) && encoded[97]==255 && u32(encoded+108)==36);
    info.captured_length=1;rejected(&info,42,13,17);
    info=observation();info.driver.secondary_raw=3;info.driver.raw_format=99;
    assert(esp32_mquickjs_wifi_monitor_wire_snapshot(&info,42,13,17,&s));
    assert(s.metadata.phy==255 && s.metadata.secondary==255);

    uint8_t one[1]={0x08},two[2]={0x08,0x00};
    info=observation();info.readable_length=info.captured_length=1;info.truncated=true;info.header_type_matches=false;
    esp32_mquickjs_wifi_rx_parse_header(one,1,&info.header);
    assert(esp32_mquickjs_wifi_monitor_wire_snapshot(&info,42,13,17,&s));
    assert(s.frame.flags==19 && !s.frame.captured_header_length && !s.metadata.frame_control_available);
    info.readable_length=info.captured_length=2;
    esp32_mquickjs_wifi_rx_parse_header(two,2,&info.header);
    assert(esp32_mquickjs_wifi_monitor_wire_snapshot(&info,42,13,17,&s));
    assert(s.frame.captured_header_length==2 && s.metadata.header_length==24 && !s.metadata.duration_available);

    esp32_mquickjs_wifi_rx_driver_metadata_t driver={0};
    assert(esp32_mquickjs_wifi_monitor_phy_format(NULL)==255);
    for(unsigned raw=0;raw<16;raw++) {
        driver.raw_format=raw;driver.he_layout=false;
        assert(esp32_mquickjs_wifi_monitor_phy_format(&driver)==(raw==0?0:raw==1?1:255));
        driver.he_layout=true;
#if CONFIG_SOC_WIFI_HE_SUPPORT
        static const uint8_t expected[]={0,0,1,2,3,4,5,6,255,255,255,2,255,255,255,255};
        assert(esp32_mquickjs_wifi_monitor_phy_format(&driver)==expected[raw]);
#else
        assert(esp32_mquickjs_wifi_monitor_phy_format(&driver)==255);
#endif
    }
    info=observation();rejected(NULL,42,13,17);rejected(&info,0,13,17);rejected(&info,42,0,17);rejected(&info,42,13,0);
    assert(!esp32_mquickjs_wifi_monitor_wire_snapshot(&info,42,13,17,NULL));
    info.driver.available=false;rejected(&info,42,13,17);
    info=observation();info.driver.type=99;rejected(&info,42,13,17);
    info=observation();info.captured_length=33;rejected(&info,42,13,17);
    info=observation();info.truncated=false;rejected(&info,42,13,17);
    info=observation();info.driver.driver_length=31;rejected(&info,42,13,17);
    info=observation();info.captured_length=0;rejected(&info,42,13,17);
    info=observation();info.header.header_length=33;rejected(&info,42,13,17);
    union {info_t input;snapshot_t output;} alias;
    alias.input=observation();
    assert(esp32_mquickjs_wifi_monitor_wire_snapshot(&alias.input,42,13,17,&alias.output));
    assert(alias.output.frame.sequence==42 && alias.output.metadata.driver_packet_length==36);
}
'''
