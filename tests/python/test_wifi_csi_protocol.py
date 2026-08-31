import struct
import unittest
from pathlib import Path

from scripts.esp32qjs_csi import CsiProtocolError, parse_batch


FIXTURE = Path(__file__).with_name("fixtures") / "esp32qjs-csi-v1.hex"


def make_batch(payloads: list[bytes]) -> bytearray:
    frame_count = len(payloads)
    metadata_bytes = frame_count * 192
    payload_bytes = sum(map(len, payloads))
    metadata_base = 24 + frame_count * 16
    payload_base = metadata_base + metadata_bytes
    output = bytearray(payload_base + payload_bytes)
    struct.pack_into(
        "<8sHHIII",
        output,
        0,
        b"E32QCSI1",
        1,
        24,
        frame_count,
        metadata_bytes,
        payload_bytes,
    )
    payload_offset = payload_base
    for index, payload in enumerate(payloads):
        metadata_offset = metadata_base + index * 192
        struct.pack_into(
            "<IIIH",
            output,
            24 + index * 16,
            metadata_offset,
            payload_offset,
            len(payload),
            192,
        )
        struct.pack_into("<IQII", output, metadata_offset, index + 10,
                         0x100000014 + index, 30, 40)
        output[metadata_offset + 20 : metadata_offset + 26] = bytes.fromhex(
            "020000000001"
        )
        output[metadata_offset + 26 : metadata_offset + 32] = bytes.fromhex(
            "ffffffffffff"
        )
        struct.pack_into("<b", output, metadata_offset + 32, -61)
        struct.pack_into("<b", output, metadata_offset + 33, -96)
        output[metadata_offset + 34] = 6
        output[metadata_offset + 35] = 1
        output[metadata_offset + 36] = 0
        output[metadata_offset + 37] = 1
        output[metadata_offset + 38] = 40
        output[metadata_offset + 39] = 3
        flags = sum(1 << bit for bit in (0, 1, 2, 3, 4, 5, 6, 7, 8, 9))
        struct.pack_into("<H", output, metadata_offset + 40, flags)
        output[metadata_offset + 42] = 1
        output[metadata_offset + 43] = 8
        struct.pack_into("<II", output, metadata_offset + 44,
                         len(payload), len(payload) // 2)
        struct.pack_into("<HBBBB", output, metadata_offset + 52,
                         0, 1, 1, 0, 1)
        output[metadata_offset + 60] = 2
        output[metadata_offset + 61] = 1
        struct.pack_into("<III", output, metadata_offset + 64,
                         0, len(payload), len(payload) // 2)
        struct.pack_into("<hh", output, metadata_offset + 76,
                         -1, len(payload) // 2 - 2)
        output[payload_offset : payload_offset + len(payload)] = payload
        payload_offset += len(payload)
    return output


class WiFiCsiProtocolTests(unittest.TestCase):
    def test_fixed_golden_vector(self):
        expected = bytes.fromhex(FIXTURE.read_text(encoding="ascii"))
        generated = bytes(make_batch([b"\x01\x02"]))

        self.assertEqual(generated, expected)
        parsed = parse_batch(expected)
        self.assertEqual(parsed.frames[0].metadata.timestamp_us, 0x100000014)
        self.assertEqual(parsed.frames[0].metadata.layout.byte_length, 2)

    def test_round_trip_known_little_endian_batch(self):
        source = make_batch([b"\x01\x02\x03\x04", b"\x80\x7f"])
        batch = parse_batch(source)

        self.assertEqual(len(batch.frames), 2)
        self.assertEqual(batch.metadata_bytes, 384)
        self.assertEqual(batch.payload_bytes, 6)
        self.assertEqual(batch.frames[0].metadata.sequence, 10)
        self.assertEqual(batch.frames[0].metadata.timestamp_us, 0x100000014)
        self.assertEqual(batch.frames[0].metadata.source_mac, "02:00:00:00:00:01")
        self.assertEqual(batch.frames[0].metadata.destination_mac, "ff:ff:ff:ff:ff:ff")
        self.assertEqual(batch.frames[0].metadata.rssi, -61)
        self.assertEqual(batch.frames[0].metadata.noise_floor, -96)
        self.assertEqual(batch.frames[0].metadata.secondary_channel, "above")
        self.assertEqual(batch.frames[0].metadata.phy_format, "ht")
        self.assertEqual(batch.frames[0].metadata.bandwidth_mhz, 40)
        self.assertEqual(batch.frames[0].metadata.mcs, 3)
        self.assertIs(batch.frames[0].metadata.stbc, True)
        self.assertIs(batch.frames[0].metadata.channel_estimate_valid, True)
        self.assertEqual(
            batch.frames[0].metadata.layout.sample_encoding, "signed-int8"
        )
        self.assertEqual(batch.frames[0].metadata.layout.iq_pair_count, 2)
        self.assertEqual(batch.frames[0].metadata.layout.segments[0].type, "ht-ltf")
        self.assertEqual(
            batch.frames[0].metadata.layout.segments[0].subcarrier_ranges[0].start,
            -1,
        )
        self.assertEqual(bytes(batch.frames[1].payload), b"\x80\x7f")

    def test_rejects_wrong_magic_and_truncation(self):
        source = make_batch([b"\x01\x02"])
        source[0] = ord("X")
        with self.assertRaisesRegex(CsiProtocolError, "magic"):
            parse_batch(source)

        source = make_batch([b"\x01\x02"])
        with self.assertRaisesRegex(CsiProtocolError, "expected"):
            parse_batch(source[:-1])

    def test_rejects_directory_and_metadata_length_mismatch(self):
        source = make_batch([b"\x01\x02"])
        struct.pack_into("<I", source, 24 + 8, 1)
        with self.assertRaisesRegex(CsiProtocolError, "metadata length"):
            parse_batch(source)

        source = make_batch([b"\x01\x02"])
        struct.pack_into("<I", source, 24 + 4, len(source) + 1)
        with self.assertRaisesRegex(CsiProtocolError, "non-contiguous payload"):
            parse_batch(source)

    def test_rejects_invalid_layout_enums_and_offsets(self):
        source = make_batch([b"\x01\x02"])
        metadata_offset = 24 + 16
        source[metadata_offset + 42] = 99
        with self.assertRaisesRegex(CsiProtocolError, "sample-encoding"):
            parse_batch(source)

        source = make_batch([b"\x01\x02"])
        source[metadata_offset + 60] = 99
        with self.assertRaisesRegex(CsiProtocolError, "segment-type"):
            parse_batch(source)

        source = make_batch([b"\x01\x02"])
        struct.pack_into("<I", source, metadata_offset + 64, 1)
        with self.assertRaisesRegex(CsiProtocolError, "non-contiguous byte offset"):
            parse_batch(source)


if __name__ == "__main__":
    unittest.main()
