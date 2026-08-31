#!/usr/bin/env python3
"""Parse the little-endian ``esp32qjs-csi/1`` batch stream."""

from __future__ import annotations

import argparse
import json
import struct
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Sequence


MAGIC = b"E32QCSI1"
VERSION = 1
HEADER_BYTES = 24
DIRECTORY_BYTES = 16
METADATA_BYTES = 192
SEGMENT_BYTES = 40
SEGMENT_BASE = 60

PHY_FORMATS = (
    "legacy",
    "ht",
    "vht",
    "he-su",
    "he-mu",
    "he-er-su",
    "he-tb",
    "unknown",
)
SECONDARY_CHANNELS = ("none", "above", "below")
SAMPLE_ENCODINGS = (
    "unknown",
    "signed-int8",
    "signed-int12-le",
    "signed-int12-packed",
)
LAYOUT_SCHEMAS = ("unknown", "wifi-csi-legacy-layout/1", "wifi-csi-he-layout/1")
SEGMENT_TYPES = (
    "unknown",
    "lltf",
    "ht-ltf",
    "stbc-ht-ltf2",
    "vht-ltf",
    "he-ltf1",
    "he-ltf2",
    "mixed",
)


class CsiProtocolError(ValueError):
    """The byte stream is not a valid ``esp32qjs-csi/1`` batch."""


@dataclass(frozen=True)
class CsiSubcarrierRange:
    start: int
    end: int


@dataclass(frozen=True)
class CsiSegment:
    type: str
    offset_bytes: int
    length_bytes: int
    iq_pair_count: int
    subcarrier_ranges: tuple[CsiSubcarrierRange, ...]
    null_subcarriers: tuple[int, ...]


@dataclass(frozen=True)
class CsiLayout:
    schema: str
    component_order: str
    sample_encoding: str
    sample_bits: int | None
    byte_length: int
    iq_pair_count: int
    trailing_padding_bytes: int
    known: bool
    segments: tuple[CsiSegment, ...]


@dataclass(frozen=True)
class CsiMetadata:
    sequence: int
    timestamp_us: int
    rx_sequence: int
    generation: int
    source_mac: str
    destination_mac: str | None
    rssi: int
    noise_floor: int | None
    channel: int
    secondary_channel: str
    antenna: int | None
    phy_format: str
    bandwidth_mhz: int | None
    mcs: int | None
    stbc: bool | None
    first_word_invalid: bool
    channel_estimate_valid: bool | None
    layout: CsiLayout


@dataclass(frozen=True)
class CsiFrame:
    metadata: CsiMetadata
    payload: memoryview


@dataclass(frozen=True)
class CsiBatch:
    frames: tuple[CsiFrame, ...]
    metadata_bytes: int
    payload_bytes: int


def _mac(value: memoryview) -> str:
    return ":".join(f"{byte:02x}" for byte in value)


def _parse_metadata(record: memoryview) -> CsiMetadata:
    sequence = struct.unpack_from("<I", record, 0)[0]
    timestamp_us = struct.unpack_from("<Q", record, 4)[0]
    rx_sequence, generation = struct.unpack_from("<II", record, 12)
    rssi = struct.unpack_from("<b", record, 32)[0]
    noise_floor_raw = struct.unpack_from("<b", record, 33)[0]
    channel, secondary, antenna, phy, bandwidth, mcs = struct.unpack_from(
        "<BBBBBB", record, 34
    )
    flags = struct.unpack_from("<H", record, 40)[0]
    encoding, sample_bits = struct.unpack_from("<BB", record, 42)
    byte_length, iq_pair_count = struct.unpack_from("<II", record, 44)
    trailing_padding_bytes = struct.unpack_from("<H", record, 52)[0]
    segment_count, schema, component_order, known = struct.unpack_from(
        "<BBBB", record, 54
    )

    if secondary >= len(SECONDARY_CHANNELS):
        raise CsiProtocolError(f"invalid secondary-channel value {secondary}")
    if phy >= len(PHY_FORMATS):
        raise CsiProtocolError(f"invalid PHY-format value {phy}")
    if encoding >= len(SAMPLE_ENCODINGS):
        raise CsiProtocolError(f"invalid sample-encoding value {encoding}")
    if schema >= len(LAYOUT_SCHEMAS):
        raise CsiProtocolError(f"invalid layout-schema value {schema}")
    if component_order != 0:
        raise CsiProtocolError(f"invalid component-order value {component_order}")
    if known not in (0, 1):
        raise CsiProtocolError(f"invalid layout-known value {known}")
    if segment_count > 3:
        raise CsiProtocolError(f"invalid segment count {segment_count}")

    segments: list[CsiSegment] = []
    expected_offset = 0
    total_iq_pairs = 0
    for segment_index in range(segment_count):
        segment_offset = SEGMENT_BASE + segment_index * SEGMENT_BYTES
        segment_type, range_count, null_count = struct.unpack_from(
            "<BBB", record, segment_offset
        )
        if segment_type >= len(SEGMENT_TYPES):
            raise CsiProtocolError(
                f"invalid segment-type value {segment_type}"
            )
        if range_count > 2:
            raise CsiProtocolError(f"invalid subcarrier-range count {range_count}")
        if null_count > 3:
            raise CsiProtocolError(f"invalid null-subcarrier count {null_count}")
        offset_bytes, length_bytes, segment_iq_pairs = struct.unpack_from(
            "<III", record, segment_offset + 4
        )
        if offset_bytes != expected_offset:
            raise CsiProtocolError(
                f"segment {segment_index} has non-contiguous byte offset"
            )
        ranges = tuple(
            CsiSubcarrierRange(
                *struct.unpack_from(
                    "<hh", record, segment_offset + 16 + range_index * 4
                )
            )
            for range_index in range(range_count)
        )
        nulls = tuple(
            struct.unpack_from(
                "<h", record, segment_offset + 24 + null_index * 2
            )[0]
            for null_index in range(null_count)
        )
        segments.append(
            CsiSegment(
                SEGMENT_TYPES[segment_type],
                offset_bytes,
                length_bytes,
                segment_iq_pairs,
                ranges,
                nulls,
            )
        )
        expected_offset += length_bytes
        total_iq_pairs += segment_iq_pairs
    if expected_offset + trailing_padding_bytes != byte_length:
        raise CsiProtocolError("layout segment lengths do not match payload length")
    if total_iq_pairs != iq_pair_count:
        raise CsiProtocolError("layout IQ-pair counts do not match metadata")
    if known and (
        encoding == 0 or schema == 0 or segment_count == 0 or sample_bits == 0
    ):
        raise CsiProtocolError("known layout has incomplete encoding metadata")
    expected_sample_bits = {1: 8, 2: 12, 3: 12}.get(encoding)
    if expected_sample_bits is not None and sample_bits != expected_sample_bits:
        raise CsiProtocolError("sample bits do not match sample encoding")
    layout = CsiLayout(
        schema=LAYOUT_SCHEMAS[schema],
        component_order="imaginary-real",
        sample_encoding=SAMPLE_ENCODINGS[encoding],
        sample_bits=sample_bits or None,
        byte_length=byte_length,
        iq_pair_count=iq_pair_count,
        trailing_padding_bytes=trailing_padding_bytes,
        known=bool(known),
        segments=tuple(segments),
    )
    return CsiMetadata(
        sequence=sequence,
        timestamp_us=timestamp_us,
        rx_sequence=rx_sequence,
        generation=generation,
        source_mac=_mac(record[20:26]),
        destination_mac=_mac(record[26:32]) if flags & (1 << 0) else None,
        rssi=rssi,
        noise_floor=noise_floor_raw if flags & (1 << 1) else None,
        channel=channel,
        secondary_channel=SECONDARY_CHANNELS[secondary],
        antenna=antenna if flags & (1 << 2) else None,
        phy_format=PHY_FORMATS[phy],
        bandwidth_mhz=bandwidth if flags & (1 << 4) else None,
        mcs=mcs if flags & (1 << 3) else None,
        stbc=bool(flags & (1 << 6)) if flags & (1 << 5) else None,
        first_word_invalid=bool(flags & (1 << 7)),
        channel_estimate_valid=(
            bool(flags & (1 << 9)) if flags & (1 << 8) else None
        ),
        layout=layout,
    )


def parse_batch(data: bytes | bytearray | memoryview) -> CsiBatch:
    """Parse one complete batch and retain zero-copy payload slices."""

    view = memoryview(data).cast("B")
    if len(view) < HEADER_BYTES:
        raise CsiProtocolError("batch is shorter than the fixed header")
    magic, version, header_bytes, frame_count, metadata_bytes, payload_bytes = (
        struct.unpack_from("<8sHHIII", view, 0)
    )
    if magic != MAGIC:
        raise CsiProtocolError("invalid batch magic")
    if version != VERSION:
        raise CsiProtocolError(f"unsupported batch version {version}")
    if header_bytes != HEADER_BYTES:
        raise CsiProtocolError(f"invalid header size {header_bytes}")
    expected_metadata_bytes = frame_count * METADATA_BYTES
    if metadata_bytes != expected_metadata_bytes:
        raise CsiProtocolError("metadata byte count does not match frame count")

    directory_end = HEADER_BYTES + frame_count * DIRECTORY_BYTES
    metadata_end = directory_end + metadata_bytes
    expected_total = metadata_end + payload_bytes
    if expected_total != len(view):
        raise CsiProtocolError(
            f"batch length is {len(view)}, expected {expected_total}"
        )

    frames: list[CsiFrame] = []
    payload_total = 0
    for index in range(frame_count):
        directory_offset = HEADER_BYTES + index * DIRECTORY_BYTES
        metadata_offset, payload_offset, payload_length, record_length = (
            struct.unpack_from("<IIIH", view, directory_offset)
        )
        expected_metadata_offset = directory_end + index * METADATA_BYTES
        if metadata_offset != expected_metadata_offset:
            raise CsiProtocolError(
                f"frame {index} has non-canonical metadata offset"
            )
        if record_length != METADATA_BYTES:
            raise CsiProtocolError(f"frame {index} has invalid metadata size")
        if payload_offset != metadata_end + payload_total:
            raise CsiProtocolError(f"frame {index} has non-contiguous payload")
        payload_end = payload_offset + payload_length
        if payload_end > len(view):
            raise CsiProtocolError(f"frame {index} payload is out of bounds")

        record = view[metadata_offset : metadata_offset + record_length]
        metadata = _parse_metadata(record)
        if metadata.layout.byte_length != payload_length:
            raise CsiProtocolError(
                f"frame {index} metadata length does not match its payload"
            )
        frames.append(CsiFrame(metadata, view[payload_offset:payload_end]))
        payload_total += payload_length

    if payload_total != payload_bytes:
        raise CsiProtocolError("directory payload lengths do not match header")
    return CsiBatch(tuple(frames), metadata_bytes, payload_bytes)


def _summary(batch: CsiBatch) -> dict[str, object]:
    return {
        "protocol": "esp32qjs-csi/1",
        "frameCount": len(batch.frames),
        "metadataBytes": batch.metadata_bytes,
        "payloadBytes": batch.payload_bytes,
        "frames": [
            {**asdict(frame.metadata), "payload_bytes": len(frame.payload)}
            for frame in batch.frames
        ],
    }


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Validate and summarize one esp32qjs-csi/1 batch"
    )
    parser.add_argument("input", type=Path)
    args = parser.parse_args(argv)
    batch = parse_batch(args.input.read_bytes())
    print(json.dumps(_summary(batch), indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
