"""Deferred shared production VHT decoding, CSI and Monitor wire consumers.

SDK bitfield construction supplies independent inputs. No test state machine,
RF claims or fixture execution before the Wi-Fi staged validation.
"""
import os
import re
import unittest
from pathlib import Path
from test_wifi_monitor_wire import PRELUDE, production_monitor_wire
from test_wifi_rx_target import ROOT, INTERNAL
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import extract


class WiFiVhtSignal(unittest.TestCase):
    def test_sdk_layout_su_mu_reserved_mcs_and_nominal_width(self):
        sdk = Path(os.environ.get('IDF_PATH', '/home/zach/esp/esp-idf'))
        private = (sdk / 'components/esp_wifi/include/esp_private/esp_wifi_he_types_private.h').read_text()
        declaration = re.search(r'typedef struct \{[^}]+\} __attribute__\(\(packed\)\) esp_wifi_vht_siga1_t;', private).group(0)
        resources = (INTERNAL / 'esp32_mquickjs_wifi_csi_resources.h').read_text()
        metadata = re.search(r'typedef struct \{[^}]+\} esp32_mquickjs_wifi_csi_metadata_t;', resources).group(0)
        csi = (ROOT / 'components/esp32_mquickjs/src/modules/wifi_csi/esp32_mquickjs_wifi_csi_target_config.c').read_text()
        body = PRELUDE + production_monitor_wire(he=True) + declaration + metadata
        body += extract(csi, 'esp32_mquickjs_wifi_csi_target_decode_he_signal')
        compile_run(self, body + MAIN)


MAIN = r'''
int main(void) {
    for(unsigned width=0;width<4;width++) for(unsigned group=0;group<64;group++)
    for(unsigned mcs=0;mcs<16;mcs++) for(unsigned flags=0;flags<8;flags++)
    for(unsigned mu=0;mu<2;mu++) {
        esp_wifi_vht_siga1_t sdk={.cbw=width,.group_id=group,.su_mcs=mcs,
            .stbc=!!(flags&1),.sgi=!!(flags&2),.su_coding=!!(flags&4)};
        _Static_assert(sizeof(sdk)==sizeof(uint32_t),"SDK VHT SIG storage");
        uint32_t signal;memcpy(&signal,&sdk,sizeof(signal));
        bool su=!mu && (group==0 || group==63),known_mcs=su && mcs<=9;
        esp32_mquickjs_wifi_csi_metadata_t csi={.phy=ESP32_MQUICKJS_WIFI_CSI_PHY_VHT};
        esp32_mquickjs_wifi_csi_target_decode_he_signal(&csi,signal,0,mu);
        assert(csi.bandwidth_available && csi.bandwidth_mhz==(20U<<width));
        assert(csi.mcs_available==known_mcs && csi.mcs==(known_mcs?mcs:0));
        assert(csi.stbc_available && csi.stbc==!!(flags&1));
        esp32_mquickjs_wifi_monitor_info_t info={.metadata_only=true,.callback_time_us=100,
            .driver={.available=true,.he_layout=true,.type=ESP32_MQUICKJS_WIFI_PACKET_MISC,
                .raw_format=mu?RX_BB_FORMAT_VHT_MU:RX_BB_FORMAT_VHT,.signal_word1=signal}};
        esp32_mquickjs_wifi_monitor_wire_snapshot_t out;
        assert(esp32_mquickjs_wifi_monitor_wire_snapshot(&info,1,2,3,&out));
        assert(out.metadata.phy==2 && out.metadata.bandwidth_mhz==csi.bandwidth_mhz);
        assert(out.metadata.mcs==(known_mcs?mcs:UINT8_MAX));
        assert(out.metadata.ampdu_count==UINT8_MAX && out.metadata.antenna==UINT8_MAX);
        uint32_t r=out.metadata.rx_flags;
        assert(!!(r&ESP32_MQUICKJS_WIFI_RX_WIRE_MCS_AVAILABLE)==known_mcs);
        assert(!!(r&ESP32_MQUICKJS_WIFI_RX_WIRE_STBC)==!!(flags&1));
        assert(!!(r&ESP32_MQUICKJS_WIFI_RX_WIRE_SGI)==!!(flags&2));
        assert(!!(r&ESP32_MQUICKJS_WIFI_RX_WIRE_FEC_AVAILABLE)==su);
        assert(!!(r&ESP32_MQUICKJS_WIFI_RX_WIRE_LDPC)==(su && !!(flags&4)));
        assert(!(r&ESP32_MQUICKJS_WIFI_RX_WIRE_AGGREGATION_AVAILABLE));
        uint8_t bytes[256];
        assert(esp32_mquickjs_wifi_rx_wire_write_metadata(ESP32_MQUICKJS_WIFI_RX_WIRE_MONITOR,
            &out.frame,&out.metadata,bytes,sizeof(bytes)));
        assert(bytes[61]==2 && bytes[62]==(20U<<width) && bytes[63]==(known_mcs?mcs:255));
    }
}
'''
