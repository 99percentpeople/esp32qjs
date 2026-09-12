"""Deferred production CSI target normalizers with explicit SDK input boundaries.

The SDK-shaped input intentionally omits mac/dmac/rx_seq: publication must get
those facts from a proven header, never the conditionally initialized SDK fields.
The real target normalizer, signal decoder and CSI layout implementation run here.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from wireless_vm_fixture import extract

ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / 'components/esp32_mquickjs'
CSI = BASE / 'src/modules/wifi_csi'


class WiFiCsiMetadata(unittest.TestCase):
    def test_target_availability_reserved_formats_and_header_facts(self):
        for he in (False, True):
            with self.subTest(he=he), tempfile.TemporaryDirectory() as directory:
                source = (CSI / ('esp32_mquickjs_wifi_csi_target_he.c' if he else
                                 'esp32_mquickjs_wifi_csi_target_legacy.c')).read_text()
                helpers = ('he_phy', 'he_secondary') if he else ('legacy_secondary',)
                code = BOUNDARIES + '\n'.join(extract(source, name) for name in
                    (*helpers, 'esp32_mquickjs_wifi_csi_target_normalize_metadata'))
                code += ('#define TEST_HE 1\n' if he else '#define TEST_HE 0\n') + MAIN
                path = Path(directory) / 'metadata.c'; path.write_text(code)
                binary = Path(directory) / 'metadata'
                sources = [CSI / name for name in ('esp32_mquickjs_wifi_csi_target_config.c',
                                                   'esp32_mquickjs_wifi_csi_target_layout.c')]
                result = subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-I' + str(BASE / 'internal'), str(path), *map(str, sources), '-o', str(binary)],
                    capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stderr)
                result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stderr)


BOUNDARIES = r'''
#include "esp32_mquickjs_wifi_csi_target.h"
#include <assert.h>
#include <string.h>
/* Field-shaped input boundary, not a substitute for target ABI/build/RF checks. */
typedef struct {
    int8_t rssi,noise_floor;
    uint8_t channel,secondary_channel,second,ant,stbc,sig_mode,mcs,cwb,sgi;
    uint8_t fec_coding,aggregation,smoothing,not_sounding,ampdu_cnt,cur_bb_format;
    uint32_t timestamp,he_siga1;
    uint16_t he_siga2;
    bool rx_channel_estimate_info_vld;
} wifi_pkt_rx_ctrl_t;
typedef struct {wifi_pkt_rx_ctrl_t rx_ctrl;uint16_t len;bool first_word_invalid;} wifi_csi_info_t;
enum { RX_BB_FORMAT_11B, RX_BB_FORMAT_11G, RX_BB_FORMAT_HT, RX_BB_FORMAT_VHT,
    RX_BB_FORMAT_HE_SU, RX_BB_FORMAT_HE_MU, RX_BB_FORMAT_HE_ERSU,
    RX_BB_FORMAT_HE_TB, RX_BB_FORMAT_VHT_MU };
'''
MAIN = r'''
int main(void){
    esp32_mquickjs_wifi_csi_metadata_t m;
    esp32_mquickjs_wifi_csi_capture_config_t config={0};
    config.schema=TEST_HE?ESP32_MQUICKJS_WIFI_CSI_SCHEMA_HE:ESP32_MQUICKJS_WIFI_CSI_SCHEMA_LEGACY;
    if(TEST_HE){config.config.he.enable=true;config.config.he.enable_legacy=true;config.config.he.ht20=true;config.config.he.lltf_bits=8;}
    else config.config.legacy.lltf=true;
    wifi_csi_info_t info={.len=128,.rx_ctrl={.rssi=-45,.noise_floor=-95,.channel=6,.sig_mode=1,
        .mcs=5,.cwb=1,.stbc=1,.sgi=1,.fec_coding=1,.aggregation=1,.smoothing=1,.ampdu_cnt=7,
        .cur_bb_format=RX_BB_FORMAT_HT,.he_siga1=5U|(1U<<7)|(1U<<24)|(1U<<27)|(1U<<28)|(1U<<30)|(1U<<31)}};
    memset(&m,0xff,sizeof(m));
    esp32_mquickjs_wifi_csi_target_normalize_metadata(&info,&config,&m);
    assert(m.phy==ESP32_MQUICKJS_WIFI_CSI_PHY_HT&&m.mcs_available&&m.mcs==5);
    assert(m.bandwidth_available&&m.bandwidth_mhz==40&&m.guard_interval_ns==400);
    assert(m.address_mask==0&&m.rx_sequence==UINT32_MAX&&!m.frame_subtype_available);
    assert(m.frame_type==ESP32_MQUICKJS_WIFI_PACKET_UNKNOWN);
    assert(m.phy_flags & ESP32_MQUICKJS_WIFI_RX_WIRE_LDPC);
    assert(m.phy_flags & ESP32_MQUICKJS_WIFI_RX_WIRE_SOUNDING);
    assert(m.phy_flags & ESP32_MQUICKJS_WIFI_RX_WIRE_AGGREGATION);
    assert(m.antenna_available==!TEST_HE&&m.ampdu_count_available==!TEST_HE);
    info.rx_ctrl.mcs=127;info.rx_ctrl.he_siga1=(info.rx_ctrl.he_siga1&~127U)|127U;
    esp32_mquickjs_wifi_csi_target_normalize_metadata(&info,&config,&m);assert(!m.mcs_available);
    info.rx_ctrl.sig_mode=2;info.rx_ctrl.cur_bb_format=255;
    info.rx_ctrl.second=info.rx_ctrl.secondary_channel=3;
    esp32_mquickjs_wifi_csi_target_normalize_metadata(&info,&config,&m);
    assert(m.phy==ESP32_MQUICKJS_WIFI_CSI_PHY_UNKNOWN&&!m.mcs_available&&!m.bandwidth_available);
    assert((unsigned)m.secondary>2&&!m.layout.known);
    assert(!m.phy_flags&&!m.ampdu_count_available&&m.layout.byte_length==128);
    info.rx_ctrl.sig_mode=0;info.rx_ctrl.cur_bb_format=RX_BB_FORMAT_11G;
    info.rx_ctrl.second=info.rx_ctrl.secondary_channel=0;
    esp32_mquickjs_wifi_csi_target_normalize_metadata(&info,&config,&m);
    assert(m.phy==ESP32_MQUICKJS_WIFI_CSI_PHY_LEGACY&&m.bandwidth_available&&m.bandwidth_mhz==20);
    assert(!m.phy_flags&&!m.mcs_available&&!m.ampdu_count_available);
}
'''
