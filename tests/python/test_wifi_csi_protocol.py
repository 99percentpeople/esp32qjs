"""Deferred sole-v1 CSI parser and actual C writer/Host interoperability tests."""
import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

from scripts.esp32qjs_csi import RxProtocolError, parse_batch
from scripts.esp32qjs_rx import parse_rx_batch

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = Path(__file__).with_name("fixtures") / "esp32qjs-csi-v1.hex"


def make_batch(payloads: list[bytes]) -> bytearray:
    count = len(payloads)
    meta_base = 32 + count * 40
    cursor = meta_base + count * 256
    total = cursor + sum((len(p) + 3) & ~3 for p in payloads)
    out = bytearray(total)
    struct.pack_into('<8sHHIHHIII', out, 0, b'E32QCSI1', 1, 32, count, 40, 256, 1, total, 0)
    for i, payload in enumerate(payloads):
        m = meta_base + i * 256
        struct.pack_into('<IIIIIHHIIII', out, 32 + i * 40, m, cursor, len(payload), 0, 0, 0, 0, 0, 0, i + 10, 0)
        struct.pack_into('<IQIII', out, m, i + 10, 0x100000014 + i, 30, 40, 50)
        out[m + 24:m + 30] = bytes.fromhex('020000000001')
        out[m + 30:m + 36] = b'\xff' * 6
        struct.pack_into('<HbbBBBBBBII', out, m + 54, 3, -61, -96, 6, 1, 0, 1, 40, 3, 0x1803f, 7)
        struct.pack_into('<BBBBIIHBB', out, m + 72, 1, 8, 1, 0, len(payload), len(payload) // 2, 0, 1, 2)
        out[m + 97] = 255
        out[m + 120:m + 124] = b'\xff' * 4
        struct.pack_into('<BBBBIIIhh', out, m + 128, 2, 1, 0, 0, 0, len(payload), len(payload) // 2, -1, len(payload) // 2 - 2)
        out[cursor:cursor + len(payload)] = payload
        cursor += (len(payload) + 3) & ~3
    return out


class WiFiCsiProtocolTests(unittest.TestCase):
    def test_golden_and_two_frame_payload_views(self):
        golden = bytes.fromhex(FIXTURE.read_text())
        self.assertEqual(bytes(make_batch([b'\x01\x02'])), golden)
        parsed = parse_batch(golden)
        self.assertEqual(parsed.frames[0].metadata.timestamp_accuracy, 'callback-time')
        self.assertEqual(parsed.frames[0].metadata.radio_generation, 50)
        self.assertEqual(parsed.total_bytes, 332)
        source = make_batch([b'\x01\x02\x03\x04', b'\x80\x7f'])
        batch = parse_batch(source)
        self.assertEqual((len(batch.frames), batch.metadata_bytes, batch.csi_bytes, batch.packet_bytes, batch.total_bytes), (2, 512, 6, 0, 632))
        m = batch.frames[0].metadata
        self.assertEqual((m.sequence, m.timestamp_us, m.source_mac, m.destination_mac), (10, 0x100000014, '02:00:00:00:00:01', 'ff:ff:ff:ff:ff:ff'))
        self.assertEqual((m.rssi, m.noise_floor, m.secondary_channel, m.phy_format, m.bandwidth_mhz, m.mcs), (-61, -96, 'above', 'ht', 40, 3))
        self.assertEqual((m.guard_interval_ns, m.he_ltf_size, m.dcm), (None, None, None))
        self.assertTrue(m.stbc and m.channel_estimate_valid and m.first_word_invalid and m.csi_data_valid)
        self.assertEqual((m.layout.sample_encoding, m.layout.iq_pair_count, m.layout.segments[0].type), ('signed-int8', 2, 'ht-ltf'))
        self.assertEqual(m.layout.segments[0].subcarrier_ranges[0].start, -1)
        self.assertIsNone(m.packet.driver_payload_length)
        self.assertEqual(bytes(batch.frames[1].csi), b'\x80\x7f')
        self.assertEqual(len(batch.frames[1].packet), 0)
        source[-4] = 0x11
        self.assertEqual(batch.frames[1].csi[0], 0x11)  # Views borrow input; callers must keep it stable.

    def test_strict_sizes_offsets_flags_enums_reserved_and_padding(self):
        base = make_batch([b'\x01\x02'])
        mutations = [
            (0, 'B', ord('X')), (8, 'H', 2), (10, 'H', 24), (12, 'I', 0), (12, 'I', 129),
            (16, 'H', 16), (18, 'H', 192), (20, 'I', 0), (20, 'I', 3), (24, 'I', len(base) - 1),
            (28, 'I', 1), (32, 'I', 73), (36, 'I', 0xffffffff), (40, 'I', 1), (44, 'I', 328),
            (54, 'H', 32), (64, 'I', 99), (68, 'I', 1),
            (72 + 54, 'H', 32), (72 + 59, 'B', 3), (72 + 61, 'B', 7), (72 + 62, 'B', 0),
            (72 + 64, 'I', 1 << 18), (72 + 68, 'I', 8), (72 + 72, 'B', 99), (72 + 73, 'B', 12),
            (72 + 75, 'B', 1), (72 + 80, 'I', 2), (72 + 84, 'H', 4), (72 + 86, 'B', 4),
            (72 + 87, 'B', 3), (72 + 97, 'B', 0), (72 + 98, 'B', 4), (72 + 102, 'H', 1),
            (72 + 104, 'I', 1), (72 + 108, 'I', 1), (72 + 116, 'I', 1 << 19),
            (72 + 124, 'B', 1), (72 + 128, 'B', 99), (72 + 131, 'B', 1), (72 + 132, 'I', 1),
            (72 + 148, 'B', 1), (72 + 154, 'B', 1), (72 + 168, 'B', 1), (72 + 248, 'B', 1),
            (331, 'B', 1),
        ]
        for offset, fmt, value in mutations:
            with self.subTest(offset=offset, value=value):
                data = bytearray(base); struct.pack_into('<' + fmt, data, offset, value)
                with self.assertRaises(RxProtocolError): parse_batch(data)
        for size in range(len(base)):
            with self.subTest(size=size):
                with self.assertRaises(RxProtocolError): parse_batch(base[:size])
        with self.assertRaises(RxProtocolError): parse_batch(base + b'\x00')
        with self.assertRaises(RxProtocolError): parse_batch(memoryview(base)[::2])

    def test_gi_ltf_dcm_parser_bounds_and_unknowns(self):
        base = make_batch([b'\x01\x02'])
        base[72 + 61] = 3
        flags = struct.unpack_from('<I', base, 72 + 64)[0] | (1 << 18)
        struct.pack_into('<I', base, 72 + 64, flags)
        struct.pack_into('<HB', base, 72 + 124, 800, 4)
        parsed = parse_batch(base).frames[0].metadata
        self.assertEqual((parsed.guard_interval_ns, parsed.he_ltf_size, parsed.dcm), (800, 4, False))
        for offset, fmt, value in [(124, 'H', 1), (124, 'H', 400), (126, 'B', 3),
                                   (127, 'B', 1), (61, 'B', 1), (61, 'B', 255),
                                   (64, 'I', (flags & ~(1 << 18)) | (1 << 19)),
                                   (64, 'I', flags | (1 << 20)),
                                   (64, 'I', flags | (3 << 13))]:
            with self.subTest(offset=offset, value=value):
                changed = bytearray(base)
                struct.pack_into('<' + fmt, changed, 72 + offset, value)
                with self.assertRaises(RxProtocolError): parse_batch(changed)

    def test_monitor_uses_same_metadata_rules_and_distinct_magic(self):
        data = bytearray(312)
        struct.pack_into('<8sHHIHHIII', data, 0, b'E32QMON1', 1, 32, 1, 24, 256, 1, 312, 0)
        struct.pack_into('<I', data, 32, 56)
        struct.pack_into('<I', data, 56 + 12, 0xffffffff)
        for offset in [59, 60, 61, 63, 97, 120, 121, 122, 123]: data[56 + offset] = 255
        data[56 + 87] = 2
        parsed = parse_rx_batch(data, kind='monitor')
        self.assertEqual(parsed.frames[0].metadata.phy_format, 'unknown')
        self.assertIsNone(parsed.frames[0].metadata.rx_sequence)
        with self.assertRaises(RxProtocolError): parse_batch(data)
        data[56 + 68] = 2
        with self.assertRaises(RxProtocolError): parse_rx_batch(data, kind='monitor')

    def test_production_c_encoder_matches_golden_and_host_parser(self):
        compiler = shutil.which('cc')
        if compiler is None: self.skipTest('C compiler unavailable')
        internal = ROOT / 'components/esp32_mquickjs/internal'
        modules = ROOT / 'components/esp32_mquickjs/src/modules'
        with tempfile.TemporaryDirectory() as tmp:
            c = Path(tmp) / 'writer.c'; c.write_text(PRODUCTION_WRITER)
            executable = Path(tmp) / 'writer'
            sources = [modules / 'wifi_common/esp32_mquickjs_wifi_rx_wire.c',
                       modules / 'wifi_common/esp32_mquickjs_wifi_rx_wire_metadata.c',
                       modules / 'wifi_csi/esp32_mquickjs_wifi_csi_wire.c',
                       modules / 'wifi_csi/esp32_mquickjs_wifi_csi_packet.c',
                       modules / 'wifi_common/esp32_mquickjs_wifi_rx.c']
            built = subprocess.run([compiler, '-std=c11', '-I', str(internal), str(c), *map(str, sources), '-o', str(executable)], capture_output=True)
            self.assertEqual(built.returncode, 0, built.stderr.decode())
            result = subprocess.run([str(executable)], capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr.decode())
            self.assertEqual(result.stdout, bytes.fromhex(FIXTURE.read_text()))
            parsed = parse_batch(result.stdout)
            self.assertEqual(bytes(parsed.frames[0].csi), b'\x01\x02')
            self.assertEqual(parsed.frames[0].metadata.timestamp_accuracy, 'callback-time')
            for scenario, expected in [(1, (400, None, None)), (2, (800, 4, False)),
                                       (3, (1600, 2, True)), (4, (3200, 4, None))]:
                result = subprocess.run([str(executable), str(scenario)], capture_output=True)
                self.assertEqual(result.returncode, 0, result.stderr.decode())
                metadata = parse_batch(result.stdout).frames[0].metadata
                self.assertEqual((metadata.guard_interval_ns, metadata.he_ltf_size, metadata.dcm), expected)
                if scenario == 2: self.assertFalse(metadata.stbc)

            for scenario, length, mode, readable in [(5, 37, "full", 40), (6, 24, "header", 40), (7, 30, "full", 30)]:
                result = subprocess.run([str(executable), str(scenario)], capture_output=True)
                self.assertEqual(result.returncode, 0, result.stderr.decode())
                frame = parse_batch(result.stdout).frames[0]
                self.assertEqual(bytes(frame.packet), bytes([8]) + bytes(length - 1))
                self.assertEqual(frame.packet_readable_length, readable)
                self.assertEqual(frame.captured_header_length, 24)
                self.assertEqual(frame.metadata.packet.capture_mode, mode)
                self.assertEqual(frame.metadata.packet.driver_payload_length, 29)
                self.assertEqual(frame.metadata.packet.driver_packet_length, 40)
                self.assertEqual(frame.metadata.packet.fcs, "unknown")
                self.assertEqual(frame.metadata.packet.flags["truncated"], mode == "full")
                # Corrupt both copies of the flag so cross-record agreement alone
                # cannot hide a driver-report/captured-length mismatch.
                broken = bytearray(result.stdout)
                record_flags = struct.unpack_from('<H', broken, 32 + 26)[0]
                struct.pack_into('<H', broken, 32 + 26, record_flags ^ 2)
                flags = struct.unpack_from('<I', broken, 72 + 116)[0]
                struct.pack_into('<I', broken, 72 + 116, flags ^ 4)
                with self.assertRaises(RxProtocolError): parse_batch(broken)



PRODUCTION_WRITER = r'''
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
'''
