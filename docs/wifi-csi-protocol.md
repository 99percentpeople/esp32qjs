# `esp32qjs-csi/1` Wire Format

Status: sole development v1 contract. All integers are little-endian. There is
no legacy metadata reader or alternate protocol version.

The batch starts with a 24-byte header:

| Offset | Type | Meaning |
| ---: | --- | --- |
| 0 | `u8[8]` | ASCII `E32QCSI1` |
| 8 | `u16` | Version, always `1` |
| 10 | `u16` | Header bytes, always `24` |
| 12 | `u32` | Frame count |
| 16 | `u32` | Total metadata bytes (`frameCount * 192`) |
| 20 | `u32` | Total payload bytes |

Each frame has one 16-byte directory record containing `metadataOffset` (`u32`),
`payloadOffset` (`u32`), `payloadLength` (`u32`), `metadataRecordBytes` (`u16`,
always 192), and two reserved zero bytes. Directory, metadata, and payload
regions are canonical and contiguous in that order.

## Metadata record

| Offset | Type | Meaning |
| ---: | --- | --- |
| 0 | `u32` | Frame sequence |
| 4 | `u64` | Wrap-extended boot-relative driver timestamp in microseconds |
| 12 | `u32` | ESP-IDF receive sequence |
| 16 | `u32` | CSI session generation |
| 20 | `u8[6]` | Source MAC |
| 26 | `u8[6]` | Destination MAC |
| 32 | `i8` | RSSI |
| 33 | `i8` | Noise floor |
| 34 | `u8` | Primary channel |
| 35 | `u8` | Secondary channel: none, above, below |
| 36 | `u8` | Antenna, `255` when unavailable |
| 37 | `u8` | PHY enum: legacy, HT, VHT, HE-SU, HE-MU, HE-ER-SU, HE-TB, unknown |
| 38 | `u8` | Bandwidth MHz, zero when unavailable |
| 39 | `u8` | MCS, `255` when unavailable |
| 40 | `u16` | Availability, STBC, and validity flags |
| 42 | `u8` | Encoding: unknown, signed-int8, signed-int12-le, signed-int12-packed |
| 43 | `u8` | Sample bits, zero when unknown |
| 44 | `u32` | Payload byte length |
| 48 | `u32` | Total IQ-pair count |
| 52 | `u16` | Trailing padding bytes |
| 54 | `u8` | Segment count, at most 3 |
| 55 | `u8` | Layout schema: unknown, legacy, HE |
| 56 | `u8` | Component order, zero means imaginary-real |
| 57 | `u8` | Layout known flag, zero or one |
| 58 | `u8[2]` | Reserved zero bytes |
| 60 | `segment[3]` | Three fixed 40-byte segment slots |
| 180 | `u8[12]` | Reserved zero bytes |

Each used segment records its type (`u8`), range count (`u8`, at most 2), null
count (`u8`, at most 3), one reserved byte, byte offset/length and IQ-pair count
(`u32` each), two signed start/end subcarrier ranges (`i16` each), three signed
null-subcarrier indices (`i16` each), and ten reserved bytes. Used segment byte
ranges are contiguous. Their lengths plus trailing padding equal the payload
length, and their IQ-pair counts equal the layout total.

The authoritative parser and validator is
[`scripts/esp32qjs_csi.py`](../scripts/esp32qjs_csi.py). It rejects truncated,
non-canonical, unknown-enum, and internally inconsistent batches.
