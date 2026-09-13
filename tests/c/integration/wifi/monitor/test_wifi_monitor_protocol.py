"""Deferred cross-language Monitor production writer/Host decoder and JSONL tests."""
from tests.support.fixtures import fixture_text
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
from tests.c.integration.wifi.monitor.test_wifi_monitor_wire import PRELUDE, production_monitor_wire


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


WRITER = fixture_text('wifi/monitor/test_wifi_monitor_protocol/writer.inc')
