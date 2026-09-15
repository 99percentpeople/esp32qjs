"""Monitor wire to PCAPNG with explicit boot identity and clock policy.

PCAPNG 1.0 SHB/IDB/EPB; LINKTYPE_IEEE802_11_RADIOTAP (127).
See docs/api/wifi-monitor.md for relative-time display and field limitations.
"""
from __future__ import annotations

import json
import struct
from dataclasses import asdict

from capture.rx import RxFrame, RxProtocolError, parse_rx_batch

UINT32_MAX = (1 << 32) - 1
UINT64_MAX = (1 << 64) - 1


def _uint(value: int, maximum: int, name: str) -> int:
    if type(value) is not int or not 0 <= value <= maximum:
        raise RxProtocolError(f"{name} must be an integer in 0..{maximum}")
    return value


def _pad(data: bytes) -> bytes:
    return data + bytes((-len(data)) & 3)


def _option(code: int, data: bytes) -> bytes:
    if len(data) > 65535:
        raise RxProtocolError("PCAPNG option exceeds uint16 length")
    return struct.pack("<HH", code, len(data)) + _pad(data)


def _comment(value: dict[str, object]) -> bytes:
    return _option(1, json.dumps(value, ensure_ascii=True, sort_keys=True,
                                 separators=(",", ":"), allow_nan=False).encode("utf-8"))


def _block(kind: int, body: bytes) -> bytes:
    body = _pad(body)
    length = _uint(len(body) + 12, UINT32_MAX, "PCAPNG block length")
    return struct.pack("<II", kind, length) + body + struct.pack("<I", length)


def _radiotap(frame: RxFrame) -> bytes:
    """All emitted fields have byte alignment; fields remain in bit-number order."""
    m = frame.metadata
    present = 0
    fields = bytearray()
    # No inference from Protected, rx_state or driver lengths to FCS/decryption.
    if m.packet.fcs != "unknown":
        flags = 0
        if m.packet.fcs.startswith("present-") and not m.packet.flags["truncated"] and m.packet.capture_mode == "full":
            if len(frame.packet) < 4:
                raise RxProtocolError("complete packet cannot contain the reported FCS")
            flags = 0x10 | (0x40 if m.packet.fcs == "present-invalid" else 0)
        present |= 1 << 1
        fields.append(flags)
    present |= 1 << 5
    fields.extend(struct.pack("<b", m.rssi))
    if m.noise_floor is not None:
        present |= 1 << 6
        fields.extend(struct.pack("<b", m.noise_floor))
    if m.antenna is not None:
        present |= 1 << 11
        fields.append(m.antenna)
    if m.phy_format == "ht":
        known = flags = index = 0
        if m.bandwidth_mhz in (20, 40):
            known |= 1
            flags |= int(m.bandwidth_mhz == 40)
        if m.mcs is not None and 0 <= m.mcs <= 76:
            known |= 2
            index = m.mcs
        if m.short_gi is not None:
            known |= 4
            flags |= 4 if m.short_gi else 0
        if m.ldpc is not None:
            known |= 16
            flags |= 16 if m.ldpc else 0
        # A boolean true does not prove how many STBC streams were present.
        if m.stbc is False:
            known |= 32
        if known:
            present |= 1 << 19
            fields.extend((known, flags, index))
    # Channel number alone does not prove a frequency/band. Raw legacy rate codes,
    # HE SIG fields and callback timestamps are not Radiotap Rate/HE/TSFT facts.
    return struct.pack("<BBHI", 0, 0, 8 + len(fields), present) + bytes(fields)


def encode_monitor_pcapng(data: bytes | bytearray | memoryview, *, boot_id: str,
                           relative: bool = False, utc_anchor_us: int | None = None,
                           monotonic_anchor_us: int | None = None) -> bytes:
    """Encode one complete batch, with no file effects or partial returned output.

    boot_id identifies the physical device boot, supplied by the caller/transport;
    wire session/radio generations cannot prove it. Anchors must belong to that
    same boot. Relative mode is explicit opt-in: generic viewers can render its
    synthetic zero-based timeline as 1970; comments identify it as NOT UTC.
    """
    if not isinstance(boot_id, str):
        raise RxProtocolError("boot_id must be a string")
    try:
        identity_bytes = boot_id.encode("utf-8")
    except UnicodeEncodeError as error:
        raise RxProtocolError("boot_id must be valid UTF-8") from error
    if not 1 <= len(identity_bytes) <= 256 or any(ord(c) < 32 or ord(c) == 127 for c in boot_id):
        raise RxProtocolError("boot_id must contain 1..256 UTF-8 bytes without control characters")
    if type(relative) is not bool:
        raise RxProtocolError("relative must be a boolean")
    if relative:
        if utc_anchor_us is not None or monotonic_anchor_us is not None:
            raise RxProtocolError("relative time and UTC anchors are mutually exclusive")
    elif utc_anchor_us is None or monotonic_anchor_us is None:
        raise RxProtocolError("PCAPNG requires --relative or both UTC and monotonic anchors")
    else:
        _uint(utc_anchor_us, UINT64_MAX, "UTC anchor")
        _uint(monotonic_anchor_us, UINT64_MAX, "monotonic anchor")
    # Own an immutable snapshot; returned views cannot change during encoding.
    batch = parse_rx_batch(bytes(data), kind="monitor")
    origin = min(frame.metadata.timestamp_us for frame in batch.frames) if relative else monotonic_anchor_us
    base = 0 if relative else utc_anchor_us
    assert origin is not None and base is not None
    clock = {"mode": "relative" if relative else "utc-anchor", "bootId": boot_id,
             "monotonicAnchorUs": origin, "utcAnchorUs": None if relative else base,
             "timestampResolutionUs": 1,
             "meaning": "relative timeline; NOT UTC" if relative else "caller-supplied UTC anchor; no drift correction"}
    timestamps = [_uint(base + frame.metadata.timestamp_us - origin, UINT64_MAX, "mapped timestamp")
                  for frame in batch.frames]
    observations = [{"protocol": "esp32qjs-monitor/1", "frameIndex": i,
                     "bootId": boot_id, "clockMode": clock["mode"], "timestampUs": timestamps[i],
                     "metadata": asdict(frame.metadata), "packetBytes": len(frame.packet),
                     "packetReadableLength": frame.packet_readable_length,
                     "capturedHeaderLength": frame.captured_header_length}
                    for i, frame in enumerate(batch.frames)]
    # Metadata-only callbacks are observations, never fake zero-byte packets.
    options = _comment({"protocol": "esp32qjs-monitor/1", "clock": clock,
                        "observations": len(batch.frames),
                        "packets": sum(bool(frame.packet) for frame in batch.frames)})
    for frame, observation in zip(batch.frames, observations):
        if not frame.packet:
            options += _comment({"metadataOnly": observation})
    result = bytearray(_block(0x0A0D0D0A, struct.pack("<IHHq", 0x1A2B3C4D, 1, 0, -1) + options + bytes(4)))
    interfaces: dict[tuple[int, int], int] = {}
    for frame, observation, timestamp in zip(batch.frames, observations, timestamps):
        if not frame.packet:
            continue
        m = frame.metadata
        identity = (m.generation, m.radio_generation)
        if identity not in interfaces:
            interfaces[identity] = len(interfaces)
            name = f"esp32qjs-monitor/session-{identity[0]}/radio-{identity[1]}".encode("utf-8")
            description = _comment({"bootId": boot_id, "sessionGeneration": identity[0],
                                    "radioGeneration": identity[1], "clock": clock})
            # SnapLen=0 means no additional truncation by this exporter.
            body = struct.pack("<HHI", 127, 0, 0) + _option(2, name) + _option(9, b"\x06") + description + bytes(4)
            result.extend(_block(1, body))
        radiotap = _radiotap(frame)
        captured = _uint(len(radiotap) + len(frame.packet), UINT32_MAX, "captured packet length")
        original = _uint(len(radiotap) + frame.packet_readable_length, UINT32_MAX, "original packet length")
        observation["originalPacketLengthSource"] = "adapter-readable-span plus Radiotap; not raw driver report"
        payload = radiotap + frame.packet.tobytes()
        body = struct.pack("<IIIII", interfaces[identity], timestamp >> 32, timestamp & UINT32_MAX, captured, original)
        result.extend(_block(6, body + _pad(payload) + _comment(observation) + bytes(4)))
    return bytes(result)
