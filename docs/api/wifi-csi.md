# Wi-Fi CSI

`wifi.csi` provides bounded raw Channel State Information capture, retained
native sample and corresponding-packet views, structured frame metadata, batch export, filtering, and
radio-ownership reporting. `wifi.csi.capabilities()` is the authoritative
source for target support, the exact configuration schema, required capture
object, PHY/sample encoding, and channel set. The sole API version is
`wifi-csi/1`.

### Actual capture configuration

`session.getCaptureConfig()` takes no arguments and returns
`{ radioGeneration, capture }` from the actual SDK configuration while this
Session still owns its exact CSI Radio lease. It requires a stable started
driver and rejects close/restart/fault transitions. It does not start capture.
`status().requested` continues to describe the original request independently.

Check `wifi.csi.capabilities().supports.captureConfigReadback` first. The fixed
SDK implements its getter on HE targets (including C5); its C3/S3 legacy libraries
have a declaration but no implementation. Those builds report false and reject
the call with native `ESP_ERR_NOT_SUPPORTED`, without an SDK getter call or a
fabricated capture result.

`capture` uses the existing `wifi-csi-he/1` schema and includes the SDK `enable`
configuration bit, which is not a Session liveness indicator. C5 exposes actual
VHT, forced LLTF and 8/12-bit mode; other HE targets report their fixed
unsupported/8-bit values. Invalid native STBC selection rejects with
`csi-config-decode` rather than silently choosing a value.

The native snapshot is copied under Radio serialization and converted after
unlocking. The detached result retains no native storage or credentials. SDK
read failures use `WIFI_CSI_DRIVER_ERROR` / `csi-config-read`; allocation failure
does not mutate the Session or its configuration. Runtime/GC/RF qualification
remains pending the Wi-Fi phase tests.

### Capabilities

`wifi.csi.capabilities()` returns this detached object:

```text
{
  apiVersion: "wifi-csi/1",
  target: string,
  idfVersion: string,
  configSchema: "wifi-csi-legacy/1" | "wifi-csi-he/1",
  sources: ("associated" | "promiscuous")[],
  phyFormats: ("legacy" | "ht" | "vht" | "he-su" | "he-mu" |
               "he-er-su" | "he-tb" | "unknown")[],
  sampleEncodings: ("signed-int8" | "signed-int12-le" |
                    "signed-int12-packed")[],
  packetCapture: {
    header: true, full: true, required: true, requireComplete: true,
    maxHeaderBytes: 36, maxPacketBytes: 16384,
    fcs: "unknown", payloadRepresentation: "unknown", qualification: "candidate"
  },
  radio: {
    country: string | null,
    policy: "auto" | "manual" | null,
    bands: ({
      band: "2.4GHz" | "5GHz",
      allowedChannels: number[] | null
    })[]
  },
  limits: {
    maxCsiBytes: number,
    maxPoolCapacity: number,
    maxQueueCapacity: number,
    maxBatchFrames: number,
    scaleMinimum: number | null,
    scaleMaximum: number | null,
    shiftMinimum: number | null,
    shiftMaximum: number | null
  },
  supports: {
    fixedChannel: boolean,
    promiscuous: boolean,
    sourceMacFilter: boolean,
    destinationMacFilter: boolean,
    bssidFilter: boolean,
    frameTypeFilter: boolean,
    frameFilter: boolean,
    frameSubtypeFilter: boolean,
    rssiFilter: boolean,
    nativeDecimation: boolean,
    nativeRateLimit: boolean,
    vht: boolean,
    he: boolean,
    heStbcSelection: boolean,
    captureConfigReadback: boolean,
    manualScaling: boolean,
    lltfBitMode: boolean,
    layout: true
  }
}
```

`radio.allowedChannels: null` represents an authoritative channel set that is
unavailable from ESP-IDF and must be treated as unknown. Check `sources`,
`supports`, and the appropriate limit before requesting an optional control.

### Opening a session

`wifi.csi.open(options)` requires exactly one object with this shape and starts
the only session immediately:

```text
{
  source?: { mode: "associated" } |
           { mode: "promiscuous", channel?: "current" | number },
  capture: LegacyCapture | HeCapture,            // required
  packet?: {
    content?: "none" | "header" | "full",
    snapLength?: number,
    required?: boolean,
    requireComplete?: boolean
  },
  filter?: {
    sourceMac?: string | string[],
    destinationMac?: string | string[],
    bssid?: string | string[],
    types?: ("management" | "control" | "data" | "misc")[],
    subtypes?: number[],
    frames?: { type: 0 | 1 | 2 | 3, subtype: number, name?: WiFiFrameName | null }[],
    minimumRssi?: number,
    sampleEvery?: number,
    maximumRateHz?: number,
    validOnly?: boolean
  },
  buffering?: {
    poolCapacity?: number,
    queueCapacity?: number,
    overflow?: "drop-newest"
  }
}
```

Omitted `source` means `{mode:"associated"}`. Associated capture does not accept
a `channel` key, even with `undefined`; channel selection belongs to promiscuous
capture. Radio conflicts always fail.
`supports.fixedChannel` is false when promiscuous capture is disabled, since an
associated source cannot request a channel.

`1 <= queueCapacity <= poolCapacity <= capabilities().limits.maxPoolCapacity`.
The pool defaults to the build maximum, which is also the total slot budget across generations. The queue defaults to the smaller of the
build queue default and the selected pool capacity. An explicit oversized queue
is rejected. Pool slots and sample storage use the selected capacity and are
allocated before Radio acquisition. `limits.maxCsiBytes` is the build sample
limit per slot; packet storage is separately reserved in the same pool allocation.

Packet capture defaults to `content:"none"` and allocates no packet records or
packet bytes. The same-callback header is still inspected within its proven span
for metadata and filtering. Header/full capture requires `snapLength` in 36..16384 (default
1600). Header mode reserves 36 bytes per slot and copies only a complete parsed
MAC header. Full mode reserves `snapLength` per slot and copies the same native
observation's packet prefix. CSI and packet share one slot, generation and lease.
All records and bytes are reserved before Radio mutation; closing the Session
keeps them while any Frame, Batch, View or Source still owns that slot.

`required:true` drops the observation when no proven, parsed packet is available.
`requireComplete:true` implies required and, if content is omitted, full; an
explicit conflicting content is rejected. It drops the observation when the
proven span or snap limit is shorter than the driver's packet report. Without
this option, full capture may truncate the packet; CSI samples are never truncated.
Header-only capture is intentional and does not set the truncated flag.

The pinned SDK's native RX copy sites issue a receipt for completed contiguous
copies, carried through the same synchronous CSI constructor. The readable prefix
is the smaller of that span and the reported packet length. The SDK's fixed
`payload=hdr+24` and raw `payload_len` are not used as body/read bounds; variable
MAC headers are parsed within the proven prefix. Unreceipted origins, metadata-only
observations and unparseable headers produce no packet. No Monitor timestamp
matching, FCS reconstruction or decryption is performed. FCS and protected-payload
representation remain unknown. Capability reports implementation availability;
this Candidate path still requires concentrated runtime and RF qualification.

MAC predicates accept one address or 1..8 addresses per role, OR within a role
and AND across roles. Addresses must have exactly 17 characters; embedded NUL
suffixes are rejected. Source/destination are logical 802.11 address roles after
ToDS/FromDS decoding, distinct from transmitter/receiver. A requested role must
be known to match: four-address frames have no BSSID, and ACK has only receiver.
No SDK `mac`/`dmac` substitution is made for an unknown role.

`types`, `subtypes` and `frames` share the `WiFiRxFilter` contract with Monitor.
Each list rejects duplicates; an empty list matches nothing and omission adds no
predicate. `types` selects management/control/data/misc categories; `subtypes`
contains integers 0..15. `frames` accepts at most 64 exact PV0 `{type, subtype}`
pairs, OR within the list and AND with all other predicates. An optional `name`
must agree with the common catalogue (or be null for an unnamed pair).
For example, `frames:[{type:0,subtype:8},{type:2,subtype:0}]` selects Beacon or
ordinary Data without also selecting association requests or QoS Data.

CSI matches header predicates only against this callback's proven, parsed MAC
header. Unparseable/unreceipted observations have category `unknown` and cannot
match an explicit type, subtype or pair filter. Numeric type 3 is representable
but has no supported CSI header layout; it is not the SDK misc category.
These filters also work with `packet.content:"none"` without packet storage
allocation. They do not make the SDK deliver additional frames. CSI `validOnly`
continues to check CSI sample/channel-estimate validity, whereas Monitor checks
MAC parsing and RX status. `requested.filter.frames` is a detached snapshot in
numeric type/subtype order; pair misses increment `filteredFrameSubtype`.

Static predicates precede decimation/rate limiting. `sampleEvery` is 1..UINT32_MAX,
with a cyclic phase that cannot wrap a lifetime counter into a different pattern.
The first qualifying sample is eligible. `maximumRateHz` is 0..1000000 when supplied;
the minimum interval is rounded up in microseconds, and regressing timestamps do
not bypass it. Omission or zero removes the limit. Configuration resets phase and timing.

The capture schema must equal `capabilities().configSchema`. A legacy capture
object is:

```text
{
  schema: "wifi-csi-legacy/1",
  lltf?: boolean,
  htLtf?: boolean,
  stbcHtLtf2?: boolean,
  ltfMerge?: boolean,
  adjacentSubcarrierFilter?: boolean,
  scale?: "auto" | { shiftBits: number },
  dumpAck?: boolean
}
```

An HE capture object is:

```text
{
  schema: "wifi-csi-he/1",
  enableLegacy?: boolean,
  forceLegacyLtf?: boolean,
  ht20?: boolean,
  ht40?: boolean,
  vht?: boolean,
  heSu?: boolean,
  heMu?: boolean,
  heDcm?: boolean,
  heBeamformed?: boolean,
  heStbcLtf?: "first" | "second" | "alternate",
  valueScale?: number,
  dumpAck?: boolean,
  lltfBits?: 8 | 12
}
```

Unknown fields and target-incompatible values are rejected. `validOnly` drops
every sample for which the target adapter exposes an invalid first word,
invalid channel estimate, or invalid callback data; its counters remain
separate in `stats()`.

Associated capture preserves the current Station radio channel. Promiscuous
capture requires `supports.promiscuous`. A numeric channel additionally
requires `supports.fixedChannel` and takes a fixed-channel shared-radio lease.
Matching fixed-channel constraints can share; the native promiscuous owner
remains exclusive.
Regulatory and coexistence conflicts fail without disconnecting Station,
moving SoftAP, or changing ESP-NOW. Capture preserves the existing modem power-save
mode. JavaScript owns power policy through `wifi.setPowerSave()` and explicit
`wifi.acquireWakeLock()` lifetimes. Timestamps use callback entry time.

A Radio home-channel conflict, or RX metadata reporting a different primary
channel for a fixed session, stops new CSI callback publication and latches
`state: "faulted"` when next observed by the callback or status path.
`lastError.code` reports `WIFI_CSI_CHANNEL_CONFLICT` with stage
`radio-channel-change`; an existing cleanup failure retains its own diagnostic.
Returning to the original channel does not resume capture automatically. Use
`stop()`, optionally `configure()`, then `start()` to explicitly revalidate the
requested channel. `channel: "current"` follows the actual Radio; `status()`
refreshes `effective.channel`, `secondaryChannel` and `radioGeneration`.
An unavailable Radio observation drops new callbacks until a successful refresh.
Retained Frame/Batch/View/Source data remains valid. Previously queued records
and pending receive Futures keep their existing queue/cancel semantics; call
`stop()`/`close()` explicitly for lifecycle cleanup.

Use an explicit promiscuous request when capture must not depend on association:

```js
var caps = wifi.csi.capabilities();
if (!caps.supports.promiscuous || !caps.supports.fixedChannel) {
  throw new Error("This Artifact does not support fixed-channel promiscuous CSI");
}
var capture = caps.configSchema === "wifi-csi-he/1"
  ? { schema: "wifi-csi-he/1", enableLegacy: true, ht20: true, heSu: true }
  : { schema: "wifi-csi-legacy/1", lltf: true, htLtf: true, scale: "auto" };
var session = wifi.csi.open({
  source: { mode: "promiscuous", channel: 6 },
  capture: capture,
  filter: { minimumRssi: -85, validOnly: true },
  buffering: { poolCapacity: 16, queueCapacity: 16, overflow: "drop-newest" }
});
```

### `WiFiCsiSession`, status, and statistics

- `status()` returns `{ generation, state, requested, effective, lastError }`.
  `state` is `"running"`, `"stopped"`, `"stopping"`, `"faulted"`, or
  `"closed"`. `requested` is a detached, normalized open object: it can be reused by
  `configure(requested)` on a stopped session or `open(requested)` after resource
  release, subject to the usual target and Radio admission rules. Unset MAC/RSSI
  predicates are omitted; `maximumRateHz:0` means unlimited. Explicit empty
  `types`, `subtypes` or `frames` arrays remain empty and continue to match none.
  `effective` contains
  `{ source, channel, secondaryChannel, radioGeneration, configSchema,
  maxCsiBytes, queueCapacity, poolCapacity, powerSave, timestampAccuracy }`.
  `secondaryChannel` is `"none"`, `"above"`, or `"below"`, and
  `timestampAccuracy` is `"callback-time"`. `radioGeneration` identifies the
  physical Radio lease generation, not the channel change counter.
- `stats()` returns `{ callbacks, accepted, deliveredFrames, deliveredBatches,
  filteredMac, filteredBssid, filteredFrameType, filteredFrameSubtype,
  filteredRssi, filteredDecimation, filteredRateLimit,
  filteredFirstWordInvalid, filteredChannelEstimateInvalid,
  invalidCallbackData, droppedPoolFull, droppedQueueFull,
  droppedFrameTooLarge, droppedClosing, receivedBytes, leasedFrames,
  packetUnavailable, packetMalformed, packetTruncated, droppedPacketRequired,
  droppedPacketIncomplete, receivedPacketBytes, droppedIdentityExhausted,
  freePoolSlots, queue }`. `queue` is the normal EventQueue statistics object,
  including `highWater`. `wifi.diagnostics.resetFrameworkCounters()` clears
  readable generation observation histories while preserving live leases,
  callback activity, sequence/generation identities and filter scheduling.
  Concurrent callbacks may publish new counts during reset; allocating/retiring
  generations are skipped and reported in diagnostics reset metadata.
- `receive(timeoutMs?)` returns one `WiFiCsiFrame` or `null`.
- `receiveBatch(options?)` accepts an object with optional
  `{ maximumFrames, minimumFrames, timeoutMs, maximumLatencyMs }`.
  `maximumFrames` defaults to the smaller of the Build Context batch limit and
  this Session's queue capacity, `minimumFrames` to 1,
  omitted `timeoutMs` waits indefinitely for the first frame, and
  `maximumLatencyMs` defaults to 0. Values must satisfy
  `1 <= minimumFrames <= maximumFrames <= min(queueCapacity, caps.limits.maxBatchFrames)`; time
  values are non-negative integers no greater than `INT32_MAX`.
  After the first frame the method drains the queue and returns at the minimum
  or maximum. When below the minimum it waits at most `maximumLatencyMs`, then
  returns a partial batch. First-frame timeout/stop/close returns `null`; a
  stop/close after at least one frame returns the partial batch.
- `stop()` returns status after disabling capture. While stopped,
  `configure(options)` replaces the complete open object and `start()` resumes.
  Packet mode/snap length may change only within the packet storage reserved at
  open; enabling packet capture after a CSI-only open requires a new Session.
  Pool and queue capacities remain fixed during a Session; changing either
  requires closing and reopening. Once native cleanup completes, reopen uses
  the remaining CSI pool budget even while old views retain their data.
  `close()` is idempotent. Failed promiscuous/Radio cleanup keeps the native
  lease, channel and retained control/queue resources; the session remains
  faulted until retry/reaping completes. `close()` returns false while cleanup
  remains unfinished. An initial start failure may also retain ownership when
  native rollback fails. Inspect `wifi.status().radio` for the original fault
  and cleanup suffix; runtime restart does not bypass retained ownership.

A slot identity or per-session delivery sequence that reaches UINT32_MAX is never
reused. The next eligible publication stops capture and reports
`WIFI_CSI_IDENTITY_EXHAUSTED` at stage `capture-identity`; stats include
`droppedIdentityExhausted`. Existing queued records, Frames, Views and Sources
remain valid. `stop()` can retire the driver, but `start()`/`configure()` cannot
revive that generation. Close and reopen a new Session within the pool budget.
This differs from exhaustion of the boot-wide pool generation, which requires a
reboot. Diagnostics expose `identityExhausted` per readable generation as well as
the separate boot-wide generation exhaustion flag.

### `WiFiCsiFrame` and `WiFiCsiBatch` data

`frame.info` and `batch.info(index)` return:

```text
{
  sequence: number,
  timestampUs: number,
  timestampAccuracy: "callback-time",
  radioGeneration: number,
  rxSequence: number | null,
  generation: number,
  signal: { rssi: number, noiseFloor: number | null, antenna: number | null },
  channel: {
    band: "2.4GHz" | "5GHz" | null,
    primary: number, secondary: "none" | "above" | "below" | null
  },
  addresses: {
    source: string | null, destination: string | null,
    transmitter: string | null, receiver: string | null, bssid: string | null
  },
  packet: WiFiCsiPacketInfo | null,
  phy: {
    format: "legacy" | "ht" | "vht" | "he-su" | "he-mu" |
            "he-er-su" | "he-tb" | "unknown",
    bandwidthMHz: 20 | 40 | 80 | 160 | null,
    mcs: number | null,
    legacyRate: null,
    stbc: boolean | null,
    guardIntervalNs: 400 | 800 | 1600 | 3200 | null,
    heLtfSize: 1 | 2 | 4 | null, dcm: boolean | null,
    shortGuardInterval: boolean | null, fecCoding: "bcc" | "ldpc" | null,
    aggregation: boolean | null, ampduCount: number | null,
    smoothingRecommended: boolean | null, sounding: boolean | null
  },
  validity: {
    firstWordInvalid: boolean,
    channelEstimateValid: boolean | null,
    callbackDataValid: true, layoutKnown: boolean
  },
  layout: {
    schema: string,
    componentOrder: "imaginary-real",
    sampleEncoding: "signed-int8" | "signed-int12-le" |
                    "signed-int12-packed" | "unknown",
    sampleBits: 8 | 12 | null,
    byteLength: number,
    iqPairCount: number,
    trailingPaddingBytes: number,
    segments: ({
      type: "lltf" | "ht-ltf" | "stbc-ht-ltf2" | "vht-ltf" |
            "he-ltf1" | "he-ltf2" | "mixed" | "unknown",
      offsetBytes: number,
      lengthBytes: number,
      iqPairCount: number,
      subcarrierRanges: ({ start: number, end: number })[],
      nullSubcarriers: number[]
    })[]
  }
}
```

This is the common `WiFiRxInfo` structure extended with generation, CSI validity
and layout. `rxSequence` is the parsed sequence-control number (upper 12 bits),
or null when unavailable, including control ACK; it does not read the SDK's
conditionally initialized `rx_seq`. Unknown address roles are null, also when no
proven header is available. A metadata-only CSI observation keeps `packet:null`.

HT metadata supplies short GI, FEC, aggregation, smoothing and sounding from the
reviewed target fields. Legacy-layout HT additionally provides AMPDU count unless
it is the wire sentinel 255; HE-layout HT has no corresponding count. VHT FEC is
reported only when the shared SIG decoder proves it, and HE FEC keeps that decoder's
per-format availability. HE guard duration does not imply HT short GI. Legacy rate
remains null because raw target rate codes are not verified bitrates. Reserved PHY
and secondary-channel values stay unknown; they do not create a known CSI layout.

`timestampUs` is monotonic time sampled at native callback entry and represented
as a JavaScript number. It is an arrival approximation, not a verified RF timestamp.
Long gaps and driver timestamp wraps do not require guessing an epoch; no UTC
anchor is inferred. Each frame retains its capture Radio generation.
Firmware preserves raw imaginary-real IQ
order. Ambiguous PHY/configuration/length combinations report
`validity.layoutKnown: false` with an explicit unknown layout.

`frame.samples()` returns a retained pool-backed `ByteView`,
`frame.copySamples()` returns an independent owned `ByteView`, and
`frame.sampleSource()` returns a one-shot Source of raw imaginary-real CSI bytes.
`frame.packetBytes()` returns a retained packet ByteView, `copyPacketBytes()` an
independent owned copy, and `packetSource()` a one-shot Source of packet bytes.
All three return null when the observation has no captured packet. A batch also
provides `packetBytes(index)`. Packet views/sources retain the same slot as CSI;
Frame/Batch/Session close does not invalidate an independently retained owner.

`info.packet` is null or a parsed packet record with category, frameType (`{ type, subtype, name }`),
frameControl, durationId, optional sequenceControl/qosControl, decoded flags,
and `capture`. Capture includes mode, headerLength, payloadLength, driverLength,
driverPayloadLength, readableLength, capturedLength, payloadCapturedLength,
truncated, fcs, payloadRepresentation, pointerLayoutValid and parseValid.
`driverPayloadLength` is the raw SDK report, whose meaning is not inferred to be
body length. `payloadLength` counts readable bytes after the parsed header;
`payloadCapturedLength` counts copied bytes after it. These may include FCS because
its presence is unknown. `driverLength` is a report, never a read permission.
`readableLength` is the bounded receipt span, and capturedLength is the bytes held.
A non-null packet has a complete parsed header, pointerLayoutValid/parseValid true.
`frame.source({ format: "esp32qjs-csi/1" })` returns a one-frame wire envelope.
A batch exposes `frameCount`, `info(index)`, `samples(index)`, and
`source({ format: "esp32qjs-csi/1" })` using the same wire encoder and ownership.
Both wire methods require the options object and exact format; omitted format,
unknown keys, coercible objects and other strings are rejected. Format is read
once before resolving the native Frame/Batch. A getter can close the owner, in
which case Source creation rejects the stale owner; a getter exception propagates.

Close every frame, batch, view and Source deterministically. Sources hold their
own native payload leases and do not keep the original JS Frame/Batch and copied
info alive. Outstanding leases safely outlive Session close and a subsequent open.
Each pool is registered by a boot-monotonic generation; old Frame/Batch/View/Source
operations continue to resolve that exact pool. Only one driver capture Session
runs at a time. Closed data generations do not retain Radio ownership.

`limits.maxTotalPoolCapacity` is the shared CSI slot budget, equal to the build's
existing pool-capacity limit. Up to `limits.maxPoolGenerations` (8) pools may be
registered, including allocating and retiring pools. An open reserves its entire
requested capacity and a generation entry before allocation or Radio mutation.
A retained full-capacity pool therefore exhausts the budget; choose smaller
`buffering.poolCapacity` values to allow overlap. Capacity is returned only after
all allocation/free calls finish. Budget exhaustion rejects open with
`WIFI_CSI_RESOURCE_EXHAUSTED`; it never evicts old data. Generation exhaustion
rejects with `WIFI_CSI_IDENTITY_EXHAUSTED` and requires a device reboot; runtime
restart does not reset the identity or budget. Final data release returns the
retired pool independently of subsequent JS or driver operations.

`wifi.diagnostics.snapshot().csi.generations` lists the charged pools, including
closed retained data. `storageBytes` includes resource control, slot metadata and
sample and optional packet allocations; `pendingStorageBytes` remains reserved while allocation or
free is running. Counters and free slots are null during those transitions.
This CSI sub-budget excludes queue/Frame/View/Source/JS/driver allocations and
allocator overhead; the shared Wi-Fi/BLE control and total memory budget remains
separate work. A stale closed Session handle does not control a newer Session.
Source creation failure releases only its new leases. Reading completion or
cancellation releases wire control storage and payloads. A Source wrapper closed
while a native read is active keeps them until the native iterator closes. Sources
are one-shot; reopening a consumed Source fails.

Frame and Batch wire Sources reject inconsistent retained snapshots with
`WIFI_CSI_INVALID_DATA`; allocation failure remains an out-of-memory exception.
The sole little-endian `esp32qjs-csi/1` format carries a 32-byte header, 40-byte
directory entries and 256-byte metadata records. Each frame emits CSI bytes and
zero padding, then optional packet bytes and zero padding, all aligned to four bytes.
The decoder rejects previous development layouts; recreate local capture fixtures. Read
`doc://framework/wifi-csi-protocol` for exact offsets and enum values.

Operational failures use `error.operation === "wifi.csi"` and one of:
`WIFI_CSI_NOT_COMPILED`, `WIFI_CSI_NOT_SUPPORTED`, `WIFI_CSI_ALREADY_OPEN`,
`WIFI_CSI_NOT_OPEN`, `WIFI_CSI_NOT_RUNNING`, `WIFI_CSI_CLOSING`,
`WIFI_CSI_CONFIG_SCHEMA_MISMATCH`, `WIFI_CSI_CONFIG_UNSUPPORTED`,
`WIFI_CSI_CONFIG_INVALID`, `WIFI_CSI_RADIO_CONFLICT`, `WIFI_CSI_CHANNEL_CONFLICT`,
`WIFI_CSI_REGULATORY_CONFLICT`, `WIFI_CSI_PROMISCUOUS_CONFLICT`,
`WIFI_CSI_RESOURCE_EXHAUSTED`,
`WIFI_CSI_FRAME_TOO_LARGE`, `WIFI_CSI_STALE_FRAME`,
`WIFI_CSI_DRIVER_ERROR`, or `WIFI_CSI_CLEANUP_PENDING`. `error.details` may
contain `{ stage, espCode, espName, requestedChannel, effectiveChannel,
radioGeneration, configSchema }`; `status().lastError` retains the same
structured information.

```js
var caps = wifi.csi.capabilities();
var capture = caps.configSchema === "wifi-csi-he/1"
  ? { schema: "wifi-csi-he/1", enableLegacy: true, ht20: true, heSu: true }
  : { schema: "wifi-csi-legacy/1", lltf: true, htLtf: true, scale: "auto" };
var session = wifi.csi.open({ capture: capture });
try {
  var frame = session.receive(1000);
  if (frame) {
    try { print(frame.info.signal.rssi, frame.info.layout.byteLength); }
    finally { frame.close(); }
  }
} finally {
  session.close();
}
```

### VHT signal metadata

On HE-layout targets, VHT bandwidth now reports nominal PPDU width
20/40/80/160 MHz from the copied SDK signal. The 160 value does not distinguish
contiguous 160 from 80+80 or prove the receiver captured that bandwidth.
VHT MCS is available only for a non-MU SDK format, SU Group ID (0 or 63), and
MCS 0–9. MU/reserved MCS stays unavailable. The CSI layout adapter does not infer
SU LTF ordering from those unknown fields or reinterpret 80/160 MHz as HT40;
these samples retain their bytes with an unknown layout. Target/RF qualification
and full VHT MU layout support remain pending.

### HE signal metadata

SU, MU, ER-SU and TB now use the shared fixed-SDK SIG decoder for bandwidth,
MCS availability and STBC. SU supports MCS 0–11; ER-SU supports 0–2 with the
242-tone RU or MCS 0 with the upper 106-tone RU, both within nominal 20 MHz.
Reserved ER width and MCS combinations remain unavailable. MU/TB data MCS is
not derived from the SIG-B MCS or unrelated SIG-A bits. TB STBC remains unknown.

Nominal MU bandwidth includes punctured 80/160 MHz modes; this value does not
specify occupied subchannels or this receiver's captured width. Existing CSI
layout support remains separately constrained: unsupported widths/formats and
unavailable SU MCS preserve samples with unknown layout. Per-user allocation
and fuller subcarrier qualification remain pending.

`phy.guardIntervalNs` reports 400/800 ns for HT/VHT and 800/1600/3200 ns for
HE SU/ER-SU/MU. `phy.heLtfSize` is the HE-LTF multiplier (1/2/4), not the number
of symbols. `phy.dcm` is known for SU/ER-SU. With raw DCM/STBC both set and
GI code 3, the decoder reports 4x LTF / 800 ns and false for both data encodings.
TB GI/LTF/DCM and MU per-user DCM remain null. These values are also included
in Frame/Batch wire metadata and Host JSONL; they do not increase timestamp
accuracy or qualify full CSI subcarrier layout/RF behavior.
