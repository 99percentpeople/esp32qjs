# `esp32qjs-csi/1` Wire Format

Sole development v1. The device Frame/Batch wire Sources and Host decoder use the layout
below; previous development layouts are rejected. All integers are little-endian.
The CSI adapter emits CSI-only or same-observation packet records according to
Session packet capture options. Frame Source uses this same envelope with frame count one. Raw samples
are exposed separately by `frame.sampleSource()`. Monitor Batch Source uses the
shared RX metadata with its own magic and 24-byte directory; see wifi-monitor.md.

## Batch and directory

| Header offset | Type | Meaning |
| ---: | --- | --- |
| 0 | u8[8] | ASCII E32QCSI1 |
| 8 | u16 | Version 1 |
| 10 | u16 | Header bytes 32 |
| 12 | u32 | Frame count 1–128 |
| 16 | u16 | Directory record bytes 40 |
| 18 | u16 | Metadata record bytes 256 |
| 20 | u32 | Flags exactly 1 (canonical layout) |
| 24 | u32 | Total bytes, including final alignment padding |
| 28 | u32 | Reserved zero |

| Directory offset | Type | Meaning |
| ---: | --- | --- |
| 0 | u32 | Absolute metadata offset |
| 4 | u32 | Absolute CSI offset, zero when absent |
| 8 | u32 | CSI byte length |
| 12 | u32 | Absolute captured packet offset, zero when absent |
| 16 | u32 | Captured packet length |
| 20 | u16 | Captured MAC header prefix length |
| 22 | u16 | Record flags |
| 24 | u32 | Driver payload report, available via metadata packet flag 17 |
| 28 | u32 | Adapter-proven original readable packet span |
| 32 | u32 | Same sequence as metadata offset 0 |
| 36 | u32 | Reserved zero |

Record flag bits: 0 packet present, 1 packet truncated, 2 intentionally header-only,
3 original header parsed, 4 packet pointer layout proven. Bits 5–15 are zero.

Ordering is header, all directories, all metadata, then each frame's CSI bytes
and packet bytes in frame order. Each data section ends with 0–3 zero bytes to
align the next section to four bytes; the final section is also padded. Absent
sections have both offset and length zero. Nonempty offsets must equal the
canonical cursor, with no holes, overlap or trailing data.

## RX metadata (256 bytes)

| Offset | Type | Meaning |
| ---: | --- | --- |
| 0 | u32 | Frame sequence |
| 4 | u64 | Same-boot monotonic timestamp, microseconds; accuracy at 87 |
| 12 | u32 | Driver RX sequence; 0xffffffff means unavailable |
| 16 | u32 | Capture Session generation |
| 20 | u32 | Physical Radio generation, not channel revision |
| 24,30,36,42,48 | u8[6] each | Source, destination, transmitter, receiver, BSSID |
| 54 | u16 | Address availability bits 0–4 in that order; others zero |
| 56 | i8 | RSSI |
| 57 | i8 | Noise floor, zero when unavailable |
| 58 | u8 | Primary channel |
| 59 | u8 | Secondary: 0 none, 1 above, 2 below, 255 unknown |
| 60 | u8 | Antenna, 255 when unavailable |
| 61 | u8 | PHY: 0 legacy, 1 HT, 2 VHT, 3 HE-SU, 4 HE-MU, 5 HE-ER-SU, 6 HE-TB, 255 unknown |
| 62 | u8 | Bandwidth MHz: 20/40/80/160 when available, otherwise zero |
| 63 | u8 | MCS, 255 when unavailable |
| 64 | u32 | RX flags |
| 68 | u32 | CSI flags |
| 72 | u8 | Sample encoding: 0 unknown, 1 signed-int8, 2 signed-int12-le, 3 signed-int12-packed |
| 73 | u8 | Sample bits: 0/8/12/12 matching encoding |
| 74 | u8 | Layout schema: 0 unknown, 1 legacy, 2 HE |
| 75 | u8 | Component order 0 imaginary-real |
| 76 | u32 | CSI byte length, same as directory |
| 80 | u32 | Total IQ pair count |
| 84 | u16 | CSI trailing padding bytes, 0–3 (inside CSI length) |
| 86 | u8 | Segment count, at most 3 |
| 87 | u8 | Time accuracy: 0 normal, 1 power-save-dependent, 2 callback-time |
| 88,90,92,94 | u16 each | Frame control, duration/ID, sequence control, QoS control; zero if unavailable |
| 96 | u8 | Packet type: 0 unknown, 1 management, 2 control, 3 data, 4 misc |
| 97 | u8 | Subtype 0–15; 255 if frame control unavailable |
| 98 | u8 | FCS: 0 unknown, 1 absent, 2 present-valid, 3 present-invalid |
| 99 | u8 | Capture mode: 0 none, 1 header, 2 full |
| 100 | u16 | Known full MAC header required length; zero when unknown |
| 102 | u16 | Reserved zero |
| 104 | u32 | Driver payload report; zero if unavailable |
| 108 | u32 | Driver original packet report; zero if unavailable |
| 112 | u32 | Captured packet length, same as directory |
| 116 | u32 | Packet flags |
| 120,121,122,123 | u8 each | Normalized legacy rate, signal mode, AMPDU count, RX state; 255 unavailable |
| 124 | u16 | Guard interval ns: 0 unavailable; 400/800/1600/3200 |
| 126 | u8 | HE-LTF size multiplier: 0 unavailable; 1/2/4 |
| 127 | u8 | Reserved zero |
| 128 | segment[3] | Three fixed 40-byte slots |
| 248 | u8[8] | Reserved zero |

Unavailable MACs are all zero; availability distinguishes a known zero address.
The current CSI adapter does not provide legacy rate/signal mode/AMPDU/RX-state
and uses their unavailable representation. Captured packets provide parsed header
fields, the raw SDK payload_len report and the RX packet-length report. The latter
is bounded by a native completed-copy receipt before reading. payload_len is not
interpreted as a body length; the SDK fixed hdr+24 pointer is not concatenated.

RX flag bits: 0 noise available, 1 antenna available, 2 MCS available, 3 bandwidth
available; 4 STBC available, 5 STBC value; 6 smoothing value; 7 sounding available,
8 sounding value; 9 aggregation available, 10 aggregation value; 11 FEC available,
12 LDPC; 13 short GI available, 14 short GI; 15 channel-estimate validity available,
16 channel-estimate valid; 17 smoothing available; 18 DCM available, 19 DCM value.
Bits 20–31 are zero. Each value
bit requires its corresponding availability bit; unavailable does not mean false.

Guard interval describes the PHY symbol, independently of timestamp accuracy.
HT/VHT allow 400/800 ns; HE allows 800/1600/3200 ns. If short GI and the duration
are both known, they must agree (true=400, false=800). Nonzero HE-LTF size and
DCM availability require HE PHY; size is the 1x/2x/4x multiplier, not the number
of LTF symbols. Zero GI/LTF means unavailable and is delivered as null. Invalid
enums, non-HE HE fields, contradictory SGI/duration, and reserved bits are rejected
by both the native writer and Host parser. Current SU/ER-SU supply GI/LTF/DCM,
MU supplies common GI/LTF, and TB requires trigger information and leaves these
unknown. Both SU/ER-SU raw DCM/STBC bits set with GI code 3 report 4x/800 ns
and false for both data encodings. Other SIG validity qualification remains pending.

CSI flags: bit 0 first word invalid, bit 1 callback CSI data valid, bit 2 layout
known. All other bits are zero: CSI is never truncated. Callback data valid means
published CSI bytes passed structural admission, not that RF quality is good.
Without CSI bytes, all CSI/layout fields and segment slots are zero.

Packet flag bits: 0 present, 1 header-only, 2 truncated, 3 pointer-valid, 4 parsed;
5–12 ToDS, FromDS, more-fragments, retry, power-management, more-data, protected,
order; 13 sequence-control available, 14 QoS available, 15 frame-control available,
16 duration available, 17 driver-payload-report available, 18 driver-packet-report
available. Bits 19–31 are zero. Bits 5–12 and subtype are derived from available
frame control. Other available header words require frame control and a proven
span of at least 4/24/26 bytes for duration/sequence/QoS respectively.

## Length and capture relationships

Driver reports, proven readable spans and actual captured lengths are different
facts. Only actual section lengths control copying and offsets. Captured bytes
cannot exceed the proven span. Unavailable reports are zero with availability
clear; known zero has availability set. The directory payload report repeats
metadata offset 104 and shares packet bit 17. A driver packet report may differ
from the directory's proven span.

The directory header prefix is min(metadata header length, captured length).
It may be incomplete under snapLength; metadata can still describe a completely
parsed original header. Parsed requires a proven pointer, supported frame-control
version/type and a complete original header within the proven span. Header-only
capture must contain the full header and is not truncation. Full capture is
truncated exactly when captured length is less than the available driver packet
report, falling back to the proven span only when that report is unavailable.
Thus native-copy truncation remains visible even when all proven bytes were
captured. The proven span must not exceed an available driver packet report.
The directory alone can enforce truncation below the proven span; the metadata
validator and Host parser enforce the complete driver-report relationship.

No captured packet means zero offset/length/prefix and clear present/truncated/
header-only flags, but known original header facts and driver reports may remain.
Directory and metadata packet flags must agree semantically (bit positions differ).

## Segment record (40 bytes)

Offsets 0/1/2 are u8 type/range-count/null-count, offset 3 is reserved zero.
Offsets 4/8/12 are u32 CSI byte offset/length/IQ count. Offset 16 has two i16
start/end range pairs; offset 24 has three i16 null indices; offset 30 has ten
reserved zero bytes. Unused range/null/segment slots are zero.

Types: 0 unknown, 1 LLTF, 2 HT-LTF, 3 STBC-HT-LTF2, 4 VHT-LTF, 5 HE-LTF1,
6 HE-LTF2, 7 mixed. Used segments are nonempty and contiguous. Length sum plus
trailing padding equals CSI length; IQ sum equals the metadata total. Encodings
1/2/3 have 2/4/3 bytes per IQ pair. Ranges have ordered endpoints and do not
overlap each other, but retain their original order (such as positive then negative).
Known layouts require known schema/encoding/segment types and range sizes equal
to IQ counts. Null indices are unique and do not remove bytes from the payload.

## Time, parser and ownership

Current captures use native callback entry time and accuracy 2. No driver epoch
is guessed from adjacent 32-bit timestamps. Power-save settings do not promote
this to RF arrival time. Host tools do not invent UTC or combine different boot
clocks; external boot identity/UTC anchoring and exports remain separate work.

`scripts/esp32qjs_csi.py` validates and summarizes CSI captures using the shared
strict decoder `scripts/esp32qjs_rx.py`. The result has `frames[].csi` and
`frames[].packet` memoryviews and explicit metadata availability. Views borrow
input storage: callers must keep mutable input stable. The parser rejects
previous 24/16/192 layouts, unknown mandatory values, nonzero reserved/padding,
noncanonical or overflowing offsets, truncated input and inconsistent fields.

Device Source retains every original CSI slot while streaming. Its control
bytes are copied separately; sample spans borrow retained pool storage, followed
by explicit zero padding. Iterator close/cancel releases those retained owners.
