# Wi-Fi Monitor

`wifi.monitor` captures bounded copies of the ESP-IDF promiscuous callback's
MAC packets. It requires the `wifi` feature. The implementation is Candidate;
RF, coexistence, runtime GC and target-matrix qualification remain pending.

- `wifi.monitor.capabilities()` reports `apiVersion: "wifi-monitor/1"`, target,
  SDK version, `packetTypes`, `frameTypes`, callable/filter `supports`, and resource `limits`.
  It is read-only and does not initialize Radio. Capability support does not
  guarantee admission with the current owners or regulatory configuration.
  `packetTypes` lists SDK callback categories (`management`, `control`, `data`, `misc`).
  `frameTypes` lists the common parser's 39 named PV0 layouts as
  `{ type, subtype, name }`: 14 management, 10 control and 15 data. It describes
  parser coverage, not guaranteed RF capture or Raw TX support. `misc` is not MAC type 3.
- `wifi.monitor.open(options?)` constructs and starts a `WiFiMonitorSession`.
- `session.receive(timeoutMs?)` returns a `WiFiMonitorFrame` or `null`, with the
  same deadline/cancellation rules as [EventQueue](event-queues.md). Omit the
  argument to wait without a deadline, use `0` to poll, or an integer
  1–2147483647 for milliseconds. At most one receiver can wait per Session.
  `Future.call(session.receive, session, [timeoutMs])` uses the native queue driver.
- `session.stop()` returns status after stopping admission, drains the entered Radio subscriber and
  discards queued Frames. It preserves the Session's Radio lease and pool.
  Existing waits remain pending until restart produces a packet, their deadline
  expires, they are cancelled, or the Session closes.
- `session.start()` resumes the same stopped Session and returns status. A closed Session cannot
  restart. Create a new Session with `open()` after confirmed cleanup.
- `session.close()` requests irreversible shutdown, releases the subscriber,
  channel and Radio lease, and closes/discards its queue. Pending waits resolve
  `null`. Cleanup errors throw `WIFI_MONITOR_FAILED`; inspect `status()` and retry
  the same handle. The runtime reaper also retries only the unfinished cleanup.
- `session.status()` and `session.stats()` remain available after close.

Direct calls and `Future.call` share receive behavior, including waiting on a
stopped Session and returning `null` from a closed, drained Session. Timeout or
Future cancellation ends the wait; it does not stop capture. A packet already
claimed by a receive operation may still finish conversion after Session close.

## Options

All objects reject unknown fields and type coercion. `undefined` selects
defaults; `null` does not. Inputs are captured before Radio mutation.

| Option | Default and accepted values |
| --- | --- |
| `channel` | `"current"`; or a numeric channel supported by the target and current regulatory settings |
| `filter.types` | management/control/data/misc; array of 0–4 distinct values; empty matches none |
| `filter.subtypes` | unrestricted; array of 0–16 distinct integers 0–15; empty matches none |
| `filter.frames` | unrestricted; up to 64 distinct `{ type: 0–3, subtype: 0–15 }` pairs; empty matches none |
| `filter.sourceMac`, `destinationMac`, `bssid` | unrestricted; one colon-separated MAC string or 1–8 distinct addresses |
| `filter.minimumRssi` | unrestricted; integer −128–127 dBm |
| `filter.validOnly` | true; reject RX error, metadata-only, malformed or type-mismatched headers |
| `filter.sampleEvery` | 1; integer 1–4294967295; first qualified packet and then every Nth |
| `filter.maximumRateHz` | 0 means unlimited; integer 0–1000000 |
| `capture.snapLength` | 2048; integer 1–16384 bytes |
| `capture.requireComplete` | false; true discards incomplete native spans, captures that exceed snapLength and metadata-only callbacks |
| `buffering.poolCapacity` | 16; integer 1–128 |
| `buffering.queueCapacity` | 16; integer 1–128 |
| `buffering.overflow` | `"drop-newest"` only |

MACs in one role are ORed; distinct roles are ANDed. Addresses are parsed from
validated MAC headers, without decoding payloads. Sampling and rate admission
happen before pool allocation; a later pool/queue drop still consumes that
sample/rate opportunity. Filter phase resets on start; lifetime stats do not.

Numeric channel requests use shared Radio fixed-channel ownership. Existing
incompatible owners reject the request. Current AP-owner admission is still
restricted; full AP/APSTA coexistence remains under implementation. A detected
fixed-channel conflict stops capture and appears in status. `startChannel` is a
start-time snapshot, while each Frame reports its callback channel.

## Reconfiguration

`session.configure(options)` replaces all open options on a **stopped** Session
and returns its new status. Call `stop()` and wait for cleanup to complete first;
running, closing, closed or cleanup-pending Sessions reject configuration.
Omitted option fields select the same defaults as `open`, rather than retaining
old values. Pool capacity, queue capacity and snapLength can all change.

The new pool and queue are allocated before committing. Input, allocation or
admission failure preserves the previous stopped Session. Replacement temporarily
needs both pools plus one additional Session control; resource exhaustion rejects
it without closing the previous Session. The exact Radio lease is transferred,
so configuration does not restart Wi-Fi or introduce an owner-free interval.
Numeric channel and power-save policy are checked against live Radio when the
new configuration is explicitly `start()`ed, not during configuration.

Successful configuration gets a new `generation`, resets the current pool's stats
and remains stopped. The old queue closes: an old receive returns `null` unless
it already claimed a packet. Existing Frames/Views/Sources retain their original
pool, metadata and generation until released. The new status accounts for the
new pool; an aggregate retired-byte ledger remains W-09 work. A Source can remain
readable across repeated configuration and subsequent Session close.

The JS queue is staged in an inactive internal property before native handoff;
commit changes a C selector without allocating. Old closed queue objects are
disposed after commit so repeated configuration does not accumulate idle controls.
Do not modify internal properties beginning with `_`.

`configure`, `start` and `stop` return a status snapshot after native changes.
If allocating that result fails, the native change can already be committed;
query `status()` before retrying. All native cleanup/admission diagnostics remain
available through the same Session and `wifi.status().radio`.

## Batch reception

`session.receiveBatch(options?)` returns a `WiFiMonitorBatch`, or null when no
first Frame arrives before timeout or the original queue closes. Call this
cooperative aggregation helper directly; it is not itself registered with
`Future.call`. Its individual waits use the production EventQueue Future driver.

| Option | Default and bounds |
| --- | --- |
| `maximumFrames` | min(32, poolCapacity); integer 1–poolCapacity, at most 128 |
| `minimumFrames` | 1; integer 1–maximumFrames |
| `timeoutMs` | No deadline for the first Frame; integer 0–2147483647, with 0 polling |
| `maximumLatencyMs` | 0; integer 0–2147483647, aggregation window after obtaining the first Frame |

All input validation occurs before queue consumption. After the first Frame,
currently queued packets are drained up to maximumFrames. When minimumFrames
has been reached the Batch returns. Otherwise, a positive maximumLatencyMs
allows further waits within that window. A zero window returns available Frames
immediately even below minimumFrames. Expiry or queue close returns the partial
Batch; minimumFrames is a target, not a guarantee. Timeout before the first Frame
returns null. Stopped Sessions may be waited on, following single receive rules.

A Batch pins the original native Session and queue before waiting. A concurrent
configure closes the old queue and completes this Batch from its original pool;
it never switches to the replacement queue. A queued operation that already
claimed its first packet can finish after close. One receiver may wait on a queue
at a time; Batch checks receiver ownership before native draining and fails if a
competing receiver owns it. Failure releases every Frame already collected.

- `batch.frameCount` remains readable after close.
- `batch.info(index)` returns a fresh copied WiFiRxInfo for that Frame.
- `batch.bytes(index)` returns an independently retained ByteView.
- `batch.close()` releases all Batch Frame roots and is idempotent. The Batch is
  also released by GC. New indexed access after close throws
  `WIFI_MONITOR_STALE_BATCH`.

Indices are integers 0–frameCount−1 without coercion. A ByteView obtained before
Batch close remains readable across Batch close, Session configure and Session
close. Batch metadata/payload conversion errors preserve existing owners until
explicit close, while failure to construct the Batch itself releases its partial
collection. The Batch owns bounded native event tokens, not a JavaScript array
of Frame objects; queued packets after the first wait can transfer directly into
its native collection without per-packet JS conversion.

`batch.source({ format: "esp32qjs-monitor/1" })` returns a one-shot wire
`ByteSpanSource`. The options object and exact format are required; unknown keys
and other formats are rejected before retaining data. Creation after Batch close
throws `WIFI_MONITOR_STALE_BATCH`. A format getter that closes the Batch cannot
leave a stale native pointer in the constructor.

The Source independently retains all frame payloads and their original Session.
It remains readable after Batch close, Session configure/close and GC. Construction
failure releases only the new references. Native read completion/cancellation
releases payloads and control storage; closing the Source wrapper during an active
read defers release until that iterator closes. Reopening a consumed Source fails.
The published byteLength is the full canonical length, including final padding.

Wire consists of a 32-byte header (`E32QMON1`), 24-byte directories and the shared
256-byte [RX metadata](wifi-csi-protocol.md), followed by each retained packet and
0–3 zero padding bytes. Metadata-only frames contribute no packet span. Encoding
copies the bounded control area; packet payloads stream directly from their pool.
Invalid retained metadata fails with `WIFI_MONITOR_INVALID_DATA`; allocation
failure remains OOM. Reported driver lengths are facts, never read bounds.

`scripts/esp32qjs_monitor.py capture.bin` strictly validates the whole batch and
writes one JSONL summary per frame. Both `supports.wireSource` and `supports.hostPcapngConverter`
are true; the latter describes the repository's Host exporter, not on-device
PCAPNG encoding or discovery of an installed Host tool. Runtime/conformance
tests remain deferred to the Wi-Fi phase test run; stability remains `candidate`.

## Host PCAPNG export

The Host CLI exports one complete Monitor batch per invocation. It does not merge
captures from different boards or boots. Obtain a device boot identifier from the
transport/session metadata and pass it explicitly. The wire's session and Radio
generations do not establish a boot identity; the caller must associate the file
and any clock anchor with the same physical device boot.

```sh
python scripts/esp32qjs_monitor.py capture.bin --format pcapng --output capture.pcapng --boot-id device-a/boot-42 --relative
```

`--relative` is explicit acceptance of a synthetic timeline. The earliest native
callback timestamp in the batch, including metadata-only callbacks, becomes zero.
Native timestamps and order are preserved in per-observation comments; input
order is not sorted and large gaps are not interpreted as 32-bit wraps. Section
and interface comments mark this mode as NOT UTC. Generic packet viewers may
render this synthetic timeline as dates near 1970; use their relative-time display.
It must not be used for absolute-time correlation with other captures.

To use a measured UTC anchor, replace `--relative` with both
`--utc-anchor-us UNIX_MICROSECONDS` and `--monotonic-anchor-us DEVICE_MICROSECONDS`.
An anchor at device time zero represents a UTC boot-time anchor. Arithmetic uses
integers: `UTC = utcAnchorUs + callbackTimeUs - monotonicAnchorUs`. Negative or
uint64-overflow results, partial anchors and mixed relative/UTC options fail.
The exporter does not infer UTC from the Host clock or correct device drift;
callback-time remains an approximation regardless of the anchor.

PCAPNG uses Section Header, Interface Description and Enhanced Packet blocks,
microsecond timestamp units and Radiotap link type 127. Distinct session/Radio
generations receive distinct interfaces under the supplied boot identity.
Metadata-only callbacks are retained as section comments and produce no packet
blocks. Every packet comment preserves normalized RX metadata, original callback
time, identity, proven readable span, captured/header lengths and driver reports.

Radiotap emits RSSI and available noise/antenna, plus known HT bandwidth, MCS
index, guard interval and FEC. A false STBC boolean proves zero streams; true does
not prove the stream count. FCS flags are omitted when unknown and never inferred
from Protected, RX state or driver lengths. Known FCS inclusion requires a complete
full capture; no CRC bytes are synthesized. Legacy driver rate codes, HE/VHT SIG
fields, channel numbers without a proven frequency/band, and callback time are
kept in comments rather than guessed as Rate, HE/VHT, Channel or TSFT fields.

Packet bytes are copied exactly after Radiotap. Captured length includes Radiotap;
original length is Radiotap plus the adapter-proven span before snap truncation,
not the raw driver length. That length describes data supplied by the capture
mechanism, not a claim about extra RF/FCS bytes. External padding is zero.

`--output` is required for PCAPNG, optional for JSONL. Input and output paths must
differ. The tool validates and encodes the entire batch before creating an output
replacement, then writes a temporary file in the output directory and atomically
replaces the destination. Validation/encoding/write failures preserve an existing
output. Without a clock policy PCAPNG fails; default JSONL keeps native timestamps.

Format references: [PCAPNG draft, March 2026](https://www.ietf.org/archive/id/draft-ietf-opsawg-pcapng-05.html),
[Radiotap defined fields](https://www.radiotap.org/fields/defined),
[Radiotap MCS](https://raw.githubusercontent.com/radiotap/radiotap.github.io/master/fields/MCS.md).
External reader and target/RF qualification remain pending; source registration
and a firmware build do not prove PCAPNG interoperability.

## Frame data and ownership

`frame.info` is a copied `WiFiRxInfo` object; it remains readable after close.
`frame.bytes()` returns a retained `ByteView`. `frame.copyBytes()` returns an
independent heap copy. `frame.source()` returns a one-shot `ByteSpanSource` of
raw captured bytes, without a Monitor/CSI wire envelope. Close every Frame,
View and Source when finished. Repeated Frame close is harmless; obtaining new
bytes or a Source from a closed Frame throws `WIFI_MONITOR_STALE_FRAME`.

Views and Sources remain valid after their Frame and Session close. The pool is
returned after its last native payload owner releases, even if the closed
Session remains reachable for status queries. A Source whose JS wrapper closes
while a native iterator is active retains its payload until that iterator closes.
Idle closed Sessions retain bounded control records, not payload storage. The
current limit of eight Session controls includes closed reachable controls and
retired payload owners; GC releases unused controls. It is not the future W-09
aggregate byte budget.

Metadata facts have explicit limits:

- `timestampUs` is monotonic callback time, and `timestampAccuracy` is
  `"callback-time"`. `rxSequence` is null. `sequence` is a per-pool identity and
  may have gaps after drops; `radioGeneration` identifies the capture's lease.
- The target supplies RSSI/noise floor. Unknown antenna, secondary channel and
  PHY fields are null. HT fields are decoded from legacy callbacks and the fixed SDK's HT-SIG
  word on HE-layout targets. Reserved HT MCS values above 76 on that path leave
  HT fields unavailable. The HE-layout word has no AMPDU count or antenna value;
  those remain null. VHT SIG-A on HE-layout targets additionally supplies nominal bandwidth,
  STBC and short GI. SU MCS (0–9) and coding require both a non-MU SDK format
  and Group ID 0 or 63; MU per-user fields and reserved MCS remain null.
  A nominal 160 MHz value does not distinguish contiguous 160 from 80+80 or
  describe this receiver's captured bandwidth.
- HE SIG-A supplies nominal SU/MU/TB bandwidth and SU/ER-SU MCS/FEC; ER-SU
  is 20 MHz with a 242-tone or upper 106-tone RU. Reserved ER bandwidth or MCS
  combinations remain unknown. MU preamble-puncturing width codes retain the
  nominal 80/160 MHz width without claiming an occupied-subchannel map.
  MU STBC comes from its common field; TB STBC and MU/TB per-user MCS/FEC
  remain unknown. HE guard interval is not mapped to HT/VHT short GI.
  `guardIntervalNs` supplies HT/VHT 400/800 ns and HE SU/ER-SU/MU
  800/1600/3200 ns; `heLtfSize` is the 1/2/4 multiplier, not a symbol count.
  `dcm` is the SU/ER-SU data encoding flag; MU per-user DCM and TB GI/LTF/DCM
  remain null. The SU/ER-SU raw DCM/STBC special combination with GI code 3
  reports 800 ns, 4x LTF, and false for both data encodings. These fields share
  the CSI/wire decoder and are independent of timestamp accuracy. Spatial
  streams, RU and puncturing detail, and normalized legacy bitrates remain pending.
- `packet` is null for metadata-only callbacks; their byte length is zero.
  Unknown short-header subtype/flags are null. Addresses require a completely
  parsed header whose type agrees with the driver.
- `capture.mode` is `"full"`, bounded by snapLength. `pointerLayoutValid` means
  the target adapter proved a readable SDK span. It does not prove valid RF or
  FCS. `fcs` remains `"unknown"`; `requireComplete` requires the proven span and
  captured bytes to cover the driver packet-length report. This is not RF/FCS proof.
  A partial native copy sets `truncated` even when all readable bytes were captured.
- `headerLength` can be the required length of a short header; inspect
  `parseValid`. Payload lengths count the proven remainder after a valid header,
  potentially including unknown FCS bytes; they do not describe decrypted data.

`stats()` distinguishes queue/pool/closing/invalid/complete-required/identity
losses and reports live leases. `accepted` counts successful queue publication,
not JS delivery. `filtered` is an object with named counters: `invalidConfig`,
`invalidCallback`, `invalidHeader`, `rxError`, `type`, `subtype`, `mac`, `rssi`,
`decimation`, and `rate`. Each callback contributes at most one filter rejection,
following the native filter's first-match order. Pool/queue/closing losses are
separate from filtering. Native counters saturate at uint64 maximum; JS numbers lose
integer precision above 2^53−1. Stats survive pool retirement until explicit
`wifi.diagnostics.resetFrameworkCounters()` clears observation histories.
That reset preserves live publishers/leases, identity and filter scheduling.
The queue summary includes `highWater`, starting at current depth on reset.
The shared JS/wire PHY snapshot also treats
AMPDU count 255 as unavailable, matching the sole-v1 wire sentinel.

Monitor errors include the original ESP code/name, last stage, cleanup stage/code,
cleanupPending and Session generation. Shared driver faults remain visible in
`wifi.status().radio`. An open failure can leave native cleanup pending without a
JS Session; the bounded native registry retains it for the runtime reaper.

`status().queue` reports the native queue's `open`, `queued`, `capacity` and
`receiverPending`, or null after detach or when the queue cannot be sampled.
`accepting` reports pool admission. `radioLeaseHeld`, `subscriberHeld` and
`fixedChannelHeld` identify cleanup obligations, including after a failed stop.
`startChannelGeneration` accompanies the start-channel observation; neither is a
live channel query. A stopped Session normally retains its Radio lease, while its
subscriber and fixed-channel claim have been released.

Pool and queue snapshots use separate locks and can observe different callback
instants. Do not subtract queued from leasedFrames to infer delivered/retained
Frames, or use these observations to authorize mutations. Closing may retain
payload owners even after queue detach. Current-pool stats reset on configure;
retired-generation totals and aggregate byte budgets remain W-09 work.

Capabilities group callable flags and native filtering under `supports`, and
`maxSessions`, `maxPoolCapacity`, `maxQueueCapacity`, `maxSnapLength`,
`maxBatchFrames`, `maxMacsPerRole` under `limits`. Frame types are management,
control, data and misc; misc callbacks carry metadata without packet bytes.
These bounds are independent, not a promise that every maximum can be allocated
simultaneously. The sole v1 replaces the former flat capability fields and indexed
`filtered` array directly; no aliases are provided.

## Frame identity and exact filtering

`info.packet.category` preserves the SDK callback category. `info.packet.frameType`
is the common `{ type, subtype, name }` descriptor, or null when no readable PV0
Frame Control exists. Unknown/reserved identities retain their numbers with
`name: null`. A descriptor does not imply `capture.parseValid`; short MAC headers
and driver/header mismatches can have an identity while parsing remains invalid.
The parser does not support MAC type 3 layouts; current `misc` callbacks provide
metadata only and cannot satisfy an exact frame filter.

```js
var capture = wifi.monitor.open({
  filter: {
    frames: [{ type: 0, subtype: 8 }, { type: 2, subtype: 0 }]
  }
});
```

This admits Beacon or ordinary Data, without matching Management subtype 0 or
Data subtype 8. Pairs are ORed together; `frames`, `types`, `subtypes`, MAC and
other predicates are ANDed. Duplicate pairs, missing fields, names instead of
numbers, fractions and unknown item keys are rejected. Full capability descriptors
can be passed directly: optional `name` must agree with the catalogue for the
numeric pair (null only for unnamed layouts). Filtering requires a
readable PV0 Frame Control and a matching driver category. `validOnly` additionally
requires successful structural parsing and an error-free SDK receive status.
`supports.frameFilter` reports this exact-pair facility. It applies to `open()`
and `configure()` and uses fixed native masks without callback allocations.

Monitor and CSI accept the same `WiFiRxFilter` field names: `types`, `subtypes`,
`frames`, address predicates, RSSI, decimation and rate limit. `frames` uses the
shared descriptor parser. `validOnly` remains source-specific: Monitor checks
MAC parsing/RX status; CSI checks sample/channel-estimate validity. A shared
filter does not imply identical native observations or supported layouts.

Capture preserves the existing power-save mode. Use `wifi.setPowerSave()` or an
explicit `wifi.acquireWakeLock()` in JavaScript when the experiment requires it.
