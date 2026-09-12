"""Deferred cross-language Monitor production writer/Host decoder and JSONL tests."""
import contextlib
import io
import json
import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

from scripts.esp32qjs_monitor import main, parse_batch, summaries
from scripts.esp32qjs_rx import RxProtocolError
from test_wifi_monitor_wire import PRELUDE, production_monitor_wire


class WiFiMonitorProtocol(unittest.TestCase):
    def test_production_writer_to_host_jsonl_and_strict_rejection(self):
        compiler = shutil.which('cc')
        if compiler is None:
            self.skipTest('C compiler unavailable')
        with tempfile.TemporaryDirectory() as tmp:
            source, binary, capture = (Path(tmp) / name for name in ('writer.c', 'writer', 'monitor.bin'))
            source.write_text('#include <stdio.h>\n' + PRELUDE + production_monitor_wire() + WRITER)
            built = subprocess.run([compiler, '-std=c11', '-Wall', '-Wextra', '-Werror',
                                    str(source), '-o', str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(built.returncode, 0, built.stderr)
            result = subprocess.run([str(binary)], capture_output=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)
            data = bytearray(result.stdout)
            self.assertEqual(len(data), 596)  # two controls, one packet byte and three zero padding
            batch = parse_batch(data)
            self.assertEqual(batch.kind, 'monitor')
            self.assertEqual(len(batch.frames), 2)
            self.assertEqual(batch.packet_bytes, 1)
            self.assertEqual(batch.csi_bytes, 0)
            first, second = batch.frames
            self.assertEqual(bytes(first.packet), b'\x08')
            self.assertEqual(first.packet_readable_length, 1)
            self.assertEqual(first.metadata.timestamp_us, 0x100000014)
            self.assertEqual(first.metadata.timestamp_accuracy, 'callback-time')
            self.assertEqual(first.metadata.generation, 13)
            self.assertEqual(first.metadata.radio_generation, 17)
            self.assertEqual(first.metadata.sequence, 42)
            self.assertEqual(first.metadata.packet.driver_packet_length, 5)
            self.assertIsNone(first.metadata.packet.driver_payload_length)
            self.assertEqual(len(second.packet), 0)
            self.assertEqual(second.metadata.sequence, 43)
            data[592] = 0x18
            self.assertEqual(bytes(first.packet), b'\x18')  # actual borrowed payload view
            data[592] = 0x08
            capture.write_bytes(data)
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                self.assertEqual(main([str(capture)]), 0)
            records = [json.loads(line) for line in output.getvalue().splitlines()]
            self.assertEqual(records, json.loads(json.dumps(summaries(batch))))
            self.assertEqual(records[0]['packetBytes'], 1)
            self.assertEqual(records[1]['packetBytes'], 0)
            for end in range(len(data)):
                with self.subTest(truncated=end), self.assertRaises(RxProtocolError):
                    parse_batch(data[:end])
            for offset in [0, 20, 28, 32, 36, 40, 44, 48, 52, 54, 56, 60, 64, 80 + 128, 593]:
                corrupted = bytearray(data)
                corrupted[offset] ^= 0x80
                with self.subTest(offset=offset), self.assertRaises(RxProtocolError):
                    parse_batch(corrupted)
            corrupted = bytearray(data)
            struct.pack_into('<I', corrupted, 24, len(data) + 1)
            capture.write_bytes(corrupted)
            output = io.StringIO()
            with contextlib.redirect_stdout(output), self.assertRaises(RxProtocolError):
                main([str(capture)])
            self.assertEqual(output.getvalue(), '')


WRITER = r'''
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
'''
