"""Strict shared decoder for the sole-v1 CSI/Monitor RX wire contract."""
from __future__ import annotations

import struct
from dataclasses import dataclass

HEADER_BYTES = 32
METADATA_BYTES = 256
SEGMENT_BYTES = 40
SEGMENT_BASE = 128
MAX_FRAMES = 128
UINT32_MAX = (1 << 32) - 1
PHY = {0: "legacy", 1: "ht", 2: "vht", 3: "he-su", 4: "he-mu", 5: "he-er-su", 6: "he-tb", 255: "unknown"}
SECONDARY = {0: "none", 1: "above", 2: "below", 255: "unknown"}
ENCODINGS = ("unknown", "signed-int8", "signed-int12-le", "signed-int12-packed")
SCHEMAS = ("unknown", "wifi-csi-legacy-layout/1", "wifi-csi-he-layout/1")
SEGMENTS = ("unknown", "lltf", "ht-ltf", "stbc-ht-ltf2", "vht-ltf", "he-ltf1", "he-ltf2", "mixed")


class RxProtocolError(ValueError):
    """Noncanonical, inconsistent or unsupported RX wire input."""


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise RxProtocolError(message)


def _zero(view: memoryview, message: str = "nonzero reserved bytes") -> None:
    _require(not any(view), message)


def _u16(view: memoryview, offset: int) -> int:
    return struct.unpack_from("<H", view, offset)[0]


def _u32(view: memoryview, offset: int) -> int:
    return struct.unpack_from("<I", view, offset)[0]


def _add(left: int, right: int) -> int:
    total = left + right
    _require(total <= UINT32_MAX, "uint32 offset overflow")
    return total


def _align(offset: int) -> int:
    return _add(offset, (-offset) & 3)


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
class RxPacket:
    type: str
    subtype: int | None
    fcs: str
    capture_mode: str
    header_length: int
    driver_payload_length: int | None
    driver_packet_length: int | None
    captured_length: int
    frame_control: int | None
    duration_id: int | None
    sequence_control: int | None
    qos_control: int | None
    flags: dict[str, bool | None]


@dataclass(frozen=True)
class RxMetadata:
    sequence: int
    timestamp_us: int
    timestamp_accuracy: str
    rx_sequence: int | None
    generation: int
    radio_generation: int
    source_mac: str | None
    destination_mac: str | None
    transmitter_mac: str | None
    receiver_mac: str | None
    bssid: str | None
    rssi: int
    noise_floor: int | None
    channel: int
    secondary_channel: str
    antenna: int | None
    phy_format: str
    bandwidth_mhz: int | None
    mcs: int | None
    stbc: bool | None
    smoothing: bool | None
    sounding: bool | None
    aggregation: bool | None
    ldpc: bool | None
    short_gi: bool | None
    guard_interval_ns: int | None
    he_ltf_size: int | None
    dcm: bool | None
    first_word_invalid: bool
    csi_data_valid: bool
    channel_estimate_valid: bool | None
    legacy_rate: int | None
    signal_mode: int | None
    ampdu_count: int | None
    rx_state: int | None
    layout: CsiLayout
    packet: RxPacket


@dataclass(frozen=True)
class RxFrame:
    metadata: RxMetadata
    csi: memoryview
    packet: memoryview
    packet_readable_length: int
    captured_header_length: int


@dataclass(frozen=True)
class RxBatch:
    kind: str
    frames: tuple[RxFrame, ...]
    metadata_bytes: int
    csi_bytes: int
    packet_bytes: int
    total_bytes: int


def _layout(record: memoryview) -> CsiLayout:
    flags = _u32(record, 68)
    _require(flags & ~7 == 0, "invalid CSI flags or truncated CSI")
    encoding, bits, schema, order = record[72:76]
    _require(encoding < len(ENCODINGS), "invalid sample-encoding")
    _require(schema < len(SCHEMAS) and order == 0, "invalid layout schema/component order")
    _require(bits == (0, 8, 12, 12)[encoding], "sample bits do not match encoding")
    length, iq = _u32(record, 76), _u32(record, 80)
    padding, count = _u16(record, 84), record[86]
    _require(count <= 3 and padding <= 3, "invalid layout dimensions")
    if not length:
        _zero(record[68:87], "absent CSI has nonzero layout")
    else:
        _require(count > 0, "nonempty CSI has no segment")
    known = bool(flags & 4)
    _require(not known or (encoding != 0 and schema != 0), "known layout has unknown encoding/schema")
    offset = pairs = 0
    segments = []
    for index in range(count):
        seg = record[SEGMENT_BASE + index * SEGMENT_BYTES:SEGMENT_BASE + (index + 1) * SEGMENT_BYTES]
        typ, range_count, null_count = seg[:3]
        _require(typ < len(SEGMENTS), "invalid segment-type")
        _require(range_count <= 2 and null_count <= 3, "invalid segment counts")
        _zero(seg[3:4]); _zero(seg[30:40])
        start, size, num_pairs = _u32(seg, 4), _u32(seg, 8), _u32(seg, 12)
        _require(start == offset, "non-contiguous byte offset")
        _require(size > 0, "empty CSI segment")
        offset = _add(offset, size); pairs = _add(pairs, num_pairs)
        _require(offset <= length, "segment exceeds CSI length")
        _require(not encoding or num_pairs * (0, 2, 4, 3)[encoding] == size, "segment IQ byte length mismatch")
        ranges = tuple(CsiSubcarrierRange(*struct.unpack_from("<hh", seg, 16 + i * 4)) for i in range(range_count))
        _require(all(r.start <= r.end for r in ranges), "invalid subcarrier range")
        if len(ranges) == 2:
            a, b = ranges
            _require(a.end < b.start or b.end < a.start, "overlapping subcarrier ranges")
        _require(not known or (typ != 0 and sum(r.end - r.start + 1 for r in ranges) == num_pairs), "known segment ranges/IQ mismatch")
        nulls = tuple(struct.unpack_from("<h", seg, 24 + i * 2)[0] for i in range(null_count))
        _require(len(set(nulls)) == len(nulls), "duplicate null subcarrier")
        _zero(seg[16 + range_count * 4:24]); _zero(seg[24 + null_count * 2:30])
        segments.append(CsiSegment(SEGMENTS[typ], start, size, num_pairs, ranges, nulls))
    _zero(record[SEGMENT_BASE + count * SEGMENT_BYTES:248])
    _require(_add(offset, padding) == length and pairs == iq, "layout byte/IQ totals mismatch")
    return CsiLayout(SCHEMAS[schema], "imaginary-real", ENCODINGS[encoding], bits or None,
                     length, iq, padding, known, tuple(segments))


def _metadata(record: memoryview, kind: str, directory: tuple[int, int, int, int, int, int, int]) -> RxMetadata:
    # Directory supplies actual CSI/packet/readable lengths, copied header, record flags,
    # driver payload report and CSI sequence (Monitor obtains sequence from metadata).
    csi_length, captured, readable, header_prefix, record_flags, payload_report, sequence = directory
    _require(record_flags & ~31 == 0, "invalid directory flags")
    _zero(record[102:104]); _zero(record[127:128]); _zero(record[248:256])
    layout = _layout(record)
    _require(layout.byte_length == csi_length, "metadata length does not match CSI directory")
    _require(_u32(record, 0) == sequence, "directory/metadata sequence mismatch")
    address_mask = _u16(record, 54)
    _require(address_mask & ~31 == 0, "invalid address flags")
    addresses = []
    for i in range(5):
        mac = record[24 + i * 6:30 + i * 6]
        if address_mask & (1 << i):
            addresses.append(":".join(f"{b:02x}" for b in mac))
        else:
            _zero(mac, "unavailable MAC has nonzero storage"); addresses.append(None)
    primary, secondary, antenna, phy, bandwidth, mcs = record[58:64]
    _require(secondary in SECONDARY and phy in PHY, "invalid secondary/PHY enum")
    rx = _u32(record, 64)
    _require(rx & ~0xfffff == 0, "invalid RX flags")
    def optional_bool(value: int, available: int) -> bool | None:
        _require(not (rx & (1 << value)) or bool(rx & (1 << available)), "RX value lacks availability")
        return bool(rx & (1 << value)) if rx & (1 << available) else None
    stbc, smoothing, sounding, aggregation, ldpc, sgi, estimate = (
        optional_bool(v, a) for v, a in ((5, 4), (6, 17), (8, 7), (10, 9), (12, 11), (14, 13), (16, 15)))
    dcm = optional_bool(19, 18)
    gi, ltf = _u16(record, 124), record[126]
    _require(gi in (0, 400, 800, 1600, 3200) and ltf in (0, 1, 2, 4), "invalid GI/LTF value")
    he = 3 <= phy <= 6
    _require(not (ltf or dcm is not None) or he, "HE fields on non-HE PHY")
    if gi:
        _require((he and gi != 400) or (phy in (1, 2) and gi <= 800), "GI incompatible with PHY")
        _require(sgi is None or gi == (400 if sgi else 800), "GI and short-GI disagree")
    def scalar(value: int, available: int, sentinel: int) -> int | None:
        if rx & (1 << available):
            _require(sentinel == 0 or value != sentinel, "available scalar uses unknown sentinel")
            return value
        _require(value == sentinel, "unavailable scalar is not canonical")
        return None
    noise = scalar(struct.unpack_from("<b", record, 57)[0], 0, 0)
    ant = scalar(antenna, 1, 255); mc = scalar(mcs, 2, 255); bw = scalar(bandwidth, 3, 0)
    _require(bw is None or bw in (20, 40, 80, 160), "invalid bandwidth")
    accuracy = record[87]
    _require(accuracy <= 2, "invalid timestamp accuracy")
    packet_flags = _u32(record, 116)
    _require(packet_flags & ~0x7ffff == 0, "invalid packet flags")
    expected = ((record_flags & 1) | ((record_flags & 4) >> 1) | ((record_flags & 2) << 1)
                | ((record_flags & 16) >> 1) | ((record_flags & 8) << 1))
    _require(packet_flags & 31 == expected, "directory/metadata packet flags mismatch")
    typ, subtype, fcs, mode = record[96:100]
    _require(typ <= 4 and fcs <= 3 and mode <= 2, "invalid packet enum")
    header = _u16(record, 100)
    _require(header_prefix == min(header, captured), "captured header prefix mismatch")
    _require(captured == _u32(record, 112) and captured <= readable, "captured/readable length mismatch")
    _require(payload_report == _u32(record, 104), "driver payload length mismatch")
    present, truncated, header_only, parsed, pointer = (bool(record_flags & (1 << i)) for i in range(5))
    _require(present == bool(captured) and (not parsed or pointer), "invalid packet presence/parse flags")
    _require(not present or pointer, "captured packet has no proven pointer")
    if present:
        _require(mode != 0 and header_only == (mode == 1), "packet capture mode mismatch")
        if header_only:
            _require(parsed and not truncated and captured == header, "incomplete header-only capture")
        else:
            _require(captured >= readable or truncated, "packet truncation mismatch")
    else:
        _require(not truncated and not header_only and header_prefix == 0, "absent packet has capture flags")
    words = [_u16(record, offset) for offset in (88, 90, 92, 94)]
    available = [bool(packet_flags & (1 << bit)) for bit in (15, 16, 13, 14)]
    for value, is_available in zip(words, available):
        _require(is_available or value == 0, "unavailable header word is nonzero")
    fc, duration, seq, qos = words
    if available[0]:
        _require(pointer and readable >= 2 and subtype == ((fc >> 4) & 15), "frame control/subtype mismatch")
        _require((packet_flags >> 5) & 255 == fc >> 8, "frame-control flags mismatch")
    else:
        _require(subtype == 255 and packet_flags & 0x1fe0 == 0 and not header, "unknown frame control has decoded facts")
    for is_available, minimum in zip(available[1:], (4, 24, 26)):
        _require(not is_available or (available[0] and readable >= minimum), "header field outside proven span")
    if parsed:
        _require(available[0] and available[1] and 0 < header <= readable and fc & 3 == 0
                 and 1 <= typ <= 3 and typ == ((fc >> 2) & 3) + 1, "invalid parsed header")
    reports = []
    for offset, bit in ((104, 17), (108, 18)):
        value = _u32(record, offset)
        known = bool(packet_flags & (1 << bit))
        _require(known or value == 0, "unavailable driver report is nonzero")
        reports.append(value if known else None)
    _require(reports[1] is None or readable <= reports[1], "readable span exceeds driver report")
    if present and not header_only:
        complete_length = reports[1] if reports[1] is not None else readable
        _require(truncated == (captured < complete_length), "packet truncation mismatch")
    if kind == "monitor":
        _require(csi_length == 0 and rx & 0x18000 == 0, "Monitor contains CSI facts")
    names = ("to_ds", "from_ds", "more_fragments", "retry", "power_management", "more_data", "protected", "order")
    pflags = {name: bool(packet_flags & (1 << (5 + i))) if available[0] else None for i, name in enumerate(names)}
    pflags.update(present=present, truncated=truncated, header_only=header_only, parse_valid=parsed, pointer_valid=pointer)
    packet = RxPacket(("unknown", "management", "control", "data", "misc")[typ], subtype if available[0] else None,
                      ("unknown", "absent", "present-valid", "present-invalid")[fcs], ("none", "header", "full")[mode],
                      header, *reports, captured, *(v if a else None for v, a in zip(words, available)), pflags)
    rx_sequence = _u32(record, 12)
    raw_scalars = [None if v == 255 else v for v in record[120:124]]
    return RxMetadata(sequence, struct.unpack_from("<Q", record, 4)[0],
                      ("normal", "power-save-dependent", "callback-time")[accuracy],
                      None if rx_sequence == UINT32_MAX else rx_sequence, _u32(record, 16), _u32(record, 20),
                      *addresses, struct.unpack_from("<b", record, 56)[0], noise, primary, SECONDARY[secondary], ant,
                      PHY[phy], bw, mc, stbc, smoothing, sounding, aggregation, ldpc, sgi, gi or None, ltf or None, dcm,
                      bool(_u32(record, 68) & 1), bool(_u32(record, 68) & 2), estimate, *raw_scalars, layout, packet)


def parse_rx_batch(data: bytes | bytearray | memoryview, *, kind: str) -> RxBatch:
    """Strict complete-batch parser; returned payload views borrow the input storage.

    Callers must keep mutable input unchanged for the lifetime of the result.
    Parsing does not infer UTC, driver epochs, FCS validity or CSI RF quality.
    """
    _require(kind in ("csi", "monitor"), "unknown RX format")
    try:
        view = memoryview(data).cast("B")
    except (TypeError, ValueError) as error:
        raise RxProtocolError("expected a contiguous byte buffer") from error
    _require(HEADER_BYTES <= len(view) <= UINT32_MAX, "batch is shorter than header or exceeds uint32")
    magic, version, header, count, directory_size, metadata_size, flags, total, reserved = struct.unpack_from("<8sHHIHHIII", view)
    _require(magic == (b"E32QCSI1" if kind == "csi" else b"E32QMON1"), "invalid batch magic")
    _require(version == 1 and header == 32 and metadata_size == 256, "unsupported RX v1 header/metadata size")
    _require(directory_size == (40 if kind == "csi" else 24), "invalid directory size")
    _require(flags == 1 and reserved == 0, "invalid batch flags/reserved")
    _require(1 <= count <= MAX_FRAMES, "invalid frame count")
    _require(total == len(view), "batch length differs from expected total")
    metadata_base = _add(32, count * directory_size)
    control_end = _add(metadata_base, count * 256)
    _require(control_end <= total, "control regions exceed input")
    cursor = control_end
    frames = []
    csi_total = packet_total = 0
    for index in range(count):
        d = view[32 + index * directory_size:32 + (index + 1) * directory_size]
        meta_offset = _u32(d, 0)
        _require(meta_offset == metadata_base + index * 256, "non-canonical metadata offset")
        record = view[meta_offset:meta_offset + 256]
        if kind == "csi":
            csi_offset, csi_length, packet_offset, captured = struct.unpack_from("<IIII", d, 4)
            prefix, rflags, payload_report, readable, sequence, d_reserved = struct.unpack_from("<HHIIII", d, 20)
            _require(d_reserved == 0, "nonzero directory reserved")
        else:
            csi_offset = csi_length = 0
            packet_offset, captured, payload_report, readable, prefix, rflags = struct.unpack_from("<IIIIHH", d, 4)
            sequence = _u32(record, 0)
        metadata = _metadata(record, kind, (csi_length, captured, readable, prefix, rflags, payload_report, sequence))
        spans = []
        for offset, length in ((csi_offset, csi_length), (packet_offset, captured)):
            _require(offset == (cursor if length else 0), "non-canonical data offset")
            end = _add(cursor, length)
            aligned = _align(end)
            _require(aligned <= total, "data span out of bounds")
            spans.append(view[cursor:end])
            _zero(view[end:aligned], "nonzero alignment padding")
            cursor = aligned
        frames.append(RxFrame(metadata, spans[0], spans[1], readable, prefix))
        csi_total = _add(csi_total, csi_length); packet_total = _add(packet_total, captured)
    _require(cursor == total, "unexpected trailing bytes")
    return RxBatch(kind, tuple(frames), count * 256, csi_total, packet_total, total)
