"""Deferred native wire -> production PCAPNG, clock and file publication tests."""
import contextlib
import io
import json
import shutil
import struct
import subprocess
import tempfile
import unittest
from decimal import Decimal
from pathlib import Path
from unittest.mock import patch

from scripts.esp32qjs_monitor import main
from scripts.esp32qjs_pcapng import encode_monitor_pcapng
from scripts.esp32qjs_rx import RxProtocolError
from tests.c.integration.wifi.monitor.test_wifi_monitor_protocol import WRITER
from tests.c.integration.wifi.monitor.test_wifi_monitor_wire import PRELUDE, production_monitor_wire


def blocks(data):
    """Independent minimal reader of standard block framing, not exporter logic."""
    found = []
    offset = 0
    while offset < len(data):
        kind, length = struct.unpack_from('<II', data, offset)
        assert length >= 12 and length % 4 == 0 and offset + length <= len(data)
        assert struct.unpack_from('<I', data, offset + length - 4)[0] == length
        found.append((kind, data[offset + 8:offset + length - 4]))
        offset += length
    assert offset == len(data)
    return found


def options(data):
    result = []
    offset = 0
    while offset < len(data):
        code, length = struct.unpack_from('<HH', data, offset)
        offset += 4
        if code == 0:
            assert length == 0 and offset == len(data)
            return result
        result.append((code, data[offset:offset + length]))
        end = offset + length
        offset = (end + 3) & ~3
        assert not any(data[end:offset])
    raise AssertionError('missing option terminator')


def packet(block):
    interface, high, low, captured, original = struct.unpack_from('<IIIII', block)
    data = block[20:20 + captured]
    opt_at = 20 + ((captured + 3) & ~3)
    assert not any(block[20 + captured:opt_at])
    return interface, (high << 32) | low, captured, original, data, options(block[opt_at:])


class WiFiMonitorPcapng(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('cc')
        if compiler is None:
            raise unittest.SkipTest('C compiler unavailable')
        with tempfile.TemporaryDirectory() as tmp:
            source, binary = Path(tmp) / 'writer.c', Path(tmp) / 'writer'
            source.write_text('#include <stdio.h>\n' + PRELUDE + production_monitor_wire() + WRITER)
            subprocess.run([compiler, '-std=c11', '-Wall', '-Wextra', '-Werror', str(source), '-o', str(binary)],
                           capture_output=True, text=True, timeout=30, check=True)
            cls.wire = subprocess.check_output([str(binary)], timeout=30)

    def encode(self, data=None, **clock):
        return encode_monitor_pcapng(self.wire if data is None else data, boot_id='device-a/boot-42', **clock)

    def test_native_wire_utc_blocks_metadata_only_and_radiotap(self):
        parsed = blocks(self.encode(utc_anchor_us=1700000000000000, monotonic_anchor_us=0x100000000))
        self.assertEqual([kind for kind, body in parsed], [0x0a0d0d0a, 1, 6])
        self.assertEqual(struct.unpack_from('<IHHq', parsed[0][1]), (0x1a2b3c4d, 1, 0, -1))
        comments = [json.loads(value) for code, value in options(parsed[0][1][16:]) if code == 1]
        self.assertEqual(comments[0]['packets'], 1)
        self.assertEqual(comments[0]['observations'], 2)
        self.assertEqual(comments[0]['clock']['mode'], 'utc-anchor')
        self.assertEqual(comments[1]['metadataOnly']['metadata']['sequence'], 43)
        self.assertEqual(struct.unpack_from('<HHI', parsed[1][1]), (127, 0, 0))
        self.assertIn((9, b'\x06'), options(parsed[1][1][8:]))
        interface, timestamp, captured, original, data, opts = packet(parsed[2][1])
        self.assertEqual((interface, timestamp, captured, original), (0, 1700000000000020, 11, 11))
        self.assertEqual(data, b'\x00\x00\x0a\x00\x60\x00\x00\x00\xc3\xa0\x08')
        observation = json.loads(dict(opts)[1])
        self.assertEqual(observation['metadata']['packet']['driver_packet_length'], 5)
        self.assertEqual(observation['packetReadableLength'], 1)
        self.assertEqual(observation['metadata']['timestamp_accuracy'], 'callback-time')
        self.assertEqual(observation['bootId'], 'device-a/boot-42')

    def test_metadata_only_batch_contains_no_fabricated_packet(self):
        data = bytearray(self.wire[:592])
        struct.pack_into('<I', data, 24, 592)
        data[80:336] = data[336:592]
        struct.pack_into('<I', data, 80, 42)
        struct.pack_into('<IIIIHH', data, 36, 0, 0, 0, 0, 0, 0)
        parsed = blocks(self.encode(data, relative=True))
        self.assertEqual([kind for kind, body in parsed], [0x0a0d0d0a])
        comments = [json.loads(value) for code, value in options(parsed[0][1][16:]) if code == 1]
        self.assertEqual(comments[0]['packets'], 0)
        self.assertEqual(len(comments), 3)

    def test_relative_origin_includes_metadata_only_and_large_gap(self):
        data = bytearray(self.wire)
        struct.pack_into('<Q', data, 336 + 4, 10)  # earlier metadata-only observation
        parsed = blocks(self.encode(data, relative=True))
        comment = json.loads(options(parsed[0][1][16:])[0][1])
        self.assertEqual(comment['clock']['mode'], 'relative')
        self.assertIsNone(comment['clock']['utcAnchorUs'])
        self.assertEqual(comment['clock']['monotonicAnchorUs'], 10)
        self.assertEqual(packet(parsed[2][1])[1], 0x100000014 - 10)

    def test_distinct_sessions_get_distinct_interfaces_and_order_is_preserved(self):
        data = bytearray(self.wire[:592]) + b'\x08\x00\x00\x00\x18\x00\x00\x00'
        struct.pack_into('<I', data, 24, len(data))
        data[336:592] = data[80:336]
        struct.pack_into('<IIIIHH', data, 60, 596, 1, 0, 1, 0, 19)
        struct.pack_into('<I', data, 336, 43)
        struct.pack_into('<Q', data, 340, 0x100000013)  # no sorting or epoch inference
        struct.pack_into('<II', data, 352, 14, 18)
        parsed = blocks(self.encode(data, relative=True))
        self.assertEqual([kind for kind, body in parsed], [0x0a0d0d0a, 1, 6, 1, 6])
        self.assertEqual([packet(body)[:2] for kind, body in parsed if kind == 6], [(0, 1), (1, 0)])
        self.assertEqual([packet(body)[4][-1] for kind, body in parsed if kind == 6], [8, 24])

    def test_truncation_uses_readable_span_not_raw_driver_report(self):
        data = bytearray(self.wire)
        struct.pack_into('<I', data, 48, 10)
        struct.pack_into('<H', data, 54, 19)
        struct.pack_into('<I', data, 80 + 108, 1000)
        flags = struct.unpack_from('<I', data, 80 + 116)[0]
        struct.pack_into('<I', data, 80 + 116, flags | 4)
        epb = packet(blocks(self.encode(data, relative=True))[-1][1])
        self.assertEqual(epb[2:4], (11, 20))
        self.assertEqual(len(epb[4]), 11)
        self.assertEqual(json.loads(dict(epb[5])[1])['metadata']['packet']['driver_packet_length'], 1000)

    def test_original_length_with_radiotap_must_fit_uint32(self):
        data = bytearray(self.wire)
        struct.pack_into('<I', data, 48, (1 << 32) - 1)
        struct.pack_into('<H', data, 54, 19)
        flags = struct.unpack_from('<I', data, 80 + 116)[0]
        struct.pack_into('<I', data, 80 + 116, flags | 4)
        with self.assertRaises(RxProtocolError):
            self.encode(data, relative=True)

    def test_ht_known_bits_boolean_stbc_and_he_are_not_guessed(self):
        data = bytearray(self.wire)
        data[80 + 60:80 + 64] = bytes((1, 1, 40, 7))
        rx = 1 | 2 | 4 | 8 | 16 | (1 << 11) | (1 << 12) | (1 << 13) | (1 << 14)
        struct.pack_into('<I', data, 80 + 64, rx)
        raw = packet(blocks(self.encode(data, relative=True))[-1][1])[4]
        self.assertEqual(struct.unpack_from('<BBHI', raw), (0, 0, 14, (1 << 5) | (1 << 6) | (1 << 11) | (1 << 19)))
        self.assertEqual(raw[11:14], bytes((55, 21, 7)))
        struct.pack_into('<I', data, 80 + 64, rx | 32)
        raw = packet(blocks(self.encode(data, relative=True))[-1][1])[4]
        self.assertEqual(raw[11], 23)  # true does not prove number of STBC streams
        data[80 + 61] = 3
        raw = packet(blocks(self.encode(data, relative=True))[-1][1])[4]
        self.assertEqual(struct.unpack_from('<I', raw, 4)[0], (1 << 5) | (1 << 6) | (1 << 11))

    def test_fcs_unknown_absent_and_impossible_full_capture(self):
        data = bytearray(self.wire)
        data[80 + 98] = 1  # explicitly absent
        raw = packet(blocks(self.encode(data, relative=True))[-1][1])[4]
        self.assertEqual(struct.unpack_from('<I', raw, 4)[0], (1 << 1) | (1 << 5) | (1 << 6))
        self.assertEqual(raw[8], 0)
        data[80 + 98] = 2
        # A truncated prefix does not carry the reported original packet's FCS.
        raw = packet(blocks(self.encode(data, relative=True))[-1][1])[4]
        self.assertEqual(raw[8], 0)
        # Now claim a complete one-byte packet: it cannot contain an FCS.
        struct.pack_into('<I', data, 80 + 108, 1)
        struct.pack_into('<H', data, 54, 17)
        flags = struct.unpack_from('<I', data, 80 + 116)[0]
        struct.pack_into('<I', data, 80 + 116, flags & ~4)
        with self.assertRaises(RxProtocolError):
            self.encode(data, relative=True)

    def test_clock_identity_overflow_and_invalid_wire(self):
        for identity in ('', '\x00', 'x' * 257, 'line\nbreak', '\ud800'):
            with self.subTest(identity=identity), self.assertRaises(RxProtocolError):
                encode_monitor_pcapng(self.wire, boot_id=identity, relative=True)
        for clock in ({}, {'utc_anchor_us': 1}, {'monotonic_anchor_us': 1},
                      {'relative': True, 'utc_anchor_us': 1}, {'relative': 1},
                      {'utc_anchor_us': -1, 'monotonic_anchor_us': 0},
                      {'utc_anchor_us': True, 'monotonic_anchor_us': 0},
                      {'utc_anchor_us': 1.5, 'monotonic_anchor_us': 0},
                      {'utc_anchor_us': (1 << 64) - 1, 'monotonic_anchor_us': 0},
                      {'utc_anchor_us': 0, 'monotonic_anchor_us': 1 << 63}):
            with self.subTest(clock=clock), self.assertRaises(RxProtocolError):
                self.encode(**clock)
        for end in range(len(self.wire)):
            with self.subTest(end=end), self.assertRaises(RxProtocolError):
                self.encode(self.wire[:end], relative=True)

    def test_cli_atomic_output_preserves_existing_file_on_failure(self):
        with tempfile.TemporaryDirectory() as tmp:
            source, output = Path(tmp) / 'capture.bin', Path(tmp) / 'capture.pcapng'
            source.write_bytes(self.wire)
            output.write_bytes(b'previous capture')
            args = [str(source), '--format', 'pcapng', '--output', str(output), '--boot-id', 'device-a/boot-42']
            with self.assertRaises(RxProtocolError):
                main(args)
            self.assertEqual(output.read_bytes(), b'previous capture')
            with patch('scripts.esp32qjs_monitor.os.replace', side_effect=OSError('replace failed')):
                with self.assertRaises(OSError):
                    main(args + ['--relative'])
            self.assertEqual(output.read_bytes(), b'previous capture')
            self.assertFalse(list(Path(tmp).glob('.monitor-*')))
            self.assertEqual(main(args + ['--relative']), 0)
            self.assertEqual(output.read_bytes(), self.encode(relative=True))
            with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                main([str(source), '--format', 'pcapng', '--output', str(source), '--boot-id', 'boot', '--relative'])
            self.assertEqual(source.read_bytes(), self.wire)

    def test_tshark_reads_the_generated_capture(self):
        tshark = shutil.which('tshark')
        if tshark is None:
            self.skipTest('tshark unavailable; external PCAPNG qualification remains not-run')
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'capture.pcapng'
            path.write_bytes(self.encode(utc_anchor_us=1700000000000000, monotonic_anchor_us=0x100000000))
            result = subprocess.run([tshark, '-n', '-r', str(path), '-T', 'fields', '-e', 'frame.time_epoch',
                                     '-e', 'frame.cap_len', '-e', 'frame.len', '-e', 'radiotap.dbm_antsignal'],
                                    capture_output=True, text=True, timeout=30, check=True)
            timestamp, captured, original, rssi = result.stdout.strip().split('\t')
            self.assertEqual(Decimal(timestamp) * 1000000, Decimal(1700000000000020))
            self.assertEqual((captured, original, rssi), ('11', '11', '-61'))
