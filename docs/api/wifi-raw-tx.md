# Wi-Fi Raw TX API

`wifi.rawTx` is available with the Wi-Fi feature. Its sole v1 contract currently
provides one-shot transmission, a bounded Session queue and native periodic transmission. Stability is **candidate**: target
compilation is separate from the deferred fault, concurrency and RF qualification.

## `wifi.rawTx.capabilities()`

Returns `WiFiRawTxCapabilities` without starting Radio. Includes
`apiVersion: "wifi-raw-tx/1"`, target, IDF version, interfaces, supported frame
types and limits. AP is listed only when the SDK build enables SoftAP. The AP
interface requires an already running AP; sending does not create an AP network.

`frameTypes` is a duplicate-free array of `WiFiFrameType` descriptors:
`{ type: 0 | 1 | 2 | 3, subtype: number, name: string | null }`. Numeric fields
are the PV0 Frame Control identity; `name` uses the common RX/TX catalogue. The
reviewed local C3/S3/C5 builds report `supports.extendedManagement: true` and
include all 14 named Protocol Version 0 management subtypes plus plain Data:

| Management family | Identifiers |
| --- | --- |
| Association | `association-request`, `association-response` |
| Reassociation | `reassociation-request`, `reassociation-response` |
| Discovery | `probe-request`, `probe-response`, `beacon`, `timing-advertisement`, `atim` |
| Authentication / departure | `authentication`, `deauthentication`, `disassociation` |
| Action | `action`, `action-no-ack` |

`data` is the additional Data subtype 0 identifier. This management
extension is experimental: the build creates a separate, hash-checked SDK
archive and changes only its management-subtype admission branch. The shared
SDK remains untouched, prior SDK fixes are retained, and unreviewed objects
fail the build. The feature macro is enabled only after that gate succeeds.
This is a framework extension beyond Espressif's documented Raw TX allowlist;
it is not a claim that Espressif supports these extra types or that each type
has completed physical/RF qualification.

See the [implementation and verification record](../investigations/2026-09-13-raw-tx-management.md)
for the SDK patch boundary and remaining hardware acceptance.
The [shared frame identity record](../investigations/2026-09-13-wifi-frame-identity.md)
describes the current RX/TX descriptor contract and exact Monitor filtering.

Without the extension, the native implementation advertises only `beacon`,
`probe-request`, `probe-response`, `action` and `data`, with
`supports.extendedManagement: false`.
Only supported types are listed; array order has no meaning. One-shot and Session
results use the same descriptor as `frameType`. For example, Beacon is
`{ type: 0, subtype: 8, name: "beacon" }`; ordinary Data is
`{ type: 2, subtype: 0, name: "data" }`. Names are descriptive; numeric pairs
identify the frame. All currently advertised TX descriptors have a known name.
The array and its descriptors are fresh snapshots; changing them does not configure transmission.

```js
var caps = wifi.rawTx.capabilities();
var supportsProbeRequest = caps.frameTypes.some(function (frame) {
  return frame.type === 0 && frame.subtype === 4;
});
```

This is a frame-type capability check. It does not validate a particular packet
or guarantee admission under the current Radio state. `send` infers the type
from the MAC header; callers do not supply a separate `frameType` option.

The management extension covers frame subtypes, not every frame variant or
security exchange. Reserved management subtypes 7/15, nonzero protocol versions,
QoS, encrypted/Protected frames, Null/CF data, and control frames remain
unsupported. PMF configuration is unchanged; the framework does not encrypt or
authenticate management frames or bypass a receiver's PMF checks. Minimum length is 24 and maximum is
1500 bytes, including the complete variable MAC header. `callerFcs` is false.
There is no PHY preamble or RF IQ input.

`nativeQueue` and `batchAdmission` are true, with queue/batch limits of 128 packets.
There are at most 8 native Sessions, 8 registered send results per Session and
8 registered flushes per Session. `periodicTx` is true, with at most 8 native
periodic jobs across all Sessions and a minimum interval of 1000 microseconds.
`rateLease` is true for Session opening. `rateLeaseInterfaces` includes `"station"`
and, in SoftAP builds, `"access-point"`. A temporary rate requires stopped,
exclusive Radio ownership and a known previous framework write. AP rate opening
requires saved AP or APSTA mode and valid AP configuration.
Application sequence
is supported only without an established connection; driver sequence is required
when either a Station connection or an AP client is present.

## `wifi.rawTx.send(frame, options?)`

`frame` is a `ByteSource`: an array-like object of integer bytes or a live ByteView.
The call uses the native Future driver and cooperatively waits for its result.
It is also callable through `Future.call(wifi.rawTx.send, wifi.rawTx, [frame, options])`; see
the [Future calling convention](futures.md). Arguments are copied during capture,
before I/O. Array-like length is read once and bounded before payload allocation;
each byte must be an integer in 0–255. Caller mutations after capture do not change
the captured packet. Options getters run before ByteView acquisition, and a view
closed by a getter is rejected rather than accessed through an old pointer.

| Option | Contract |
| --- | --- |
| `interface` | `"station"` by default, or `"access-point"` when enabled/running |
| `channel` | `"current"` by default, or a target-supported numeric channel admitted by Radio |
| `sequenceControl` | `"driver"` by default, or `"application"` when disconnected |
| `timeoutMs` | Integer 1–2147483647, default 1000; covers the entire Future including waiting and initialization |

Unknown keys, null/array options, invalid enums (including NUL suffixes), fractions
and out-of-range values are rejected. C retains mandatory MAC/SDK validation, including the build-specific management
subtype boundary. It does not parse arbitrary management bodies or detect FCS/containers;
application content validation belongs in JavaScript. Input must be a pure 802.11 MAC frame without Radiotap,
PCAP record headers or caller-added FCS. A valid-looking header cannot establish
that arbitrary trailing bytes are not a caller-added FCS.

Radio can initialize/start a Station interface as part of one-shot send. A numeric
channel is checked against live regulatory state; when the driver is initially
uninitialized, that readback occurs after initialization/start. Failure can leave
native cleanup pending. AP startup remains explicit through Wi-Fi configuration.

Radio reads actual association/MAC state to validate connected-path DS bits and
Power Management/More Data/Retry restrictions. SDK association changes are not
serialized by the framework mutex, so final SDK rejection remains possible.
Existing incompatible fixed-channel owners or scan/connect reservations reject
submission. The chosen channel stays pinned through native retirement, including
after a public timeout. Sending does not change interface-global PHY rate settings.

The result contains sequence, radioGeneration, interface, observed channel,
frameType, byteLength, submittedAtUs, completedAtUs, driverAccepted,
driverCompleted, driverStatus, rate, rawRate and rawStatus. Sequence is a boot-lived
framework operation identity, **not** the packet's 802.11 sequence-control field.
Timestamps use the local monotonic clock, not UTC; completedAtUs is callback time.
`driverStatus` is `"success"`, `"failed"` or `"unknown"`. A failed MAC completion
is a completed result; SDK submission errors throw. Completion is not a peer
application ACK. After native completion, `rate` maps the target SDK enum name;
unknown numeric codes remain null. `rawRate` preserves the original code. Rate
names do not infer PHY mode, GI duration, throughput or acknowledgment.

```js
// Supply a valid MAC frame built for your network and target.
function sendPacket(frameBytes) {
  return wifi.rawTx.send(frameBytes, {
    interface: "station",
    channel: "current",
    sequenceControl: "driver",
    timeoutMs: 1000
  });
}
```

## Timeout, cancellation and cleanup

Only one native raw send can be in flight. One-shot Future calls use one bounded
runtime lane; a later call also waits for the preceding native cleanup. Session
producers share the native arbiter described below. Worker queue
saturation retries admission without transmitting a second copy. Completion
polling reads native state and does not depend on an observer EventQueue.

Timeout throws `WIFI_RAW_TX_TIMEOUT`. Cancellation stops waiting and prevents
submission if observed before the worker submits; neither action undoes already
submitted RF, nor guarantees that an in-progress SDK call will not transmit.
After the worker returns, Future-owned captured bytes can be freed. The separate
driver buffer and exact Radio lease remain in a fixed native cleanup slot until
the original completion is safe to retire. Future result-conversion OOM uses the
same ownership transfer; do not blindly resend after an ambiguous error.

Capture/frame rejection uses `WIFI_RAW_TX_INVALID_FRAME`; native failures use
`WIFI_RAW_TX_SEND_FAILED`, with `espCode`, `espName`, `stage` and `validationCode`.
Read `wifi.status().radio.rawTx` for operationActive/operationIdentity, quarantined,
correlationFault, cleanupPending/cleanupStage/cleanupError, submitError,
registrationError and identityExhausted. `nativeTerminated` indicates retained
physical-deinit proof waiting for exact owner retirement; it is not TX completion.
In that state `operationActive` is false but `operationIdentity` remains available,
and `terminatedRadioGeneration` identifies the terminated physical generation.
Registration cannot be reused until its owner consumes that evidence. Otherwise
terminatedRadioGeneration is null. `laneIdentity` is the active native
scheduler grant (or null); it is distinct from the SDK operationIdentity and can
remain present while Radio acquisition or cleanup is in progress. `laneWaiters`
counts admitted scheduler requests waiting for that grant. Future calls still
waiting in the Future resource queue are not included. `laneIdentityExhausted`
reports boot-scoped scheduler identity exhaustion; identities are never reused
by runtime or Radio restart. The fields are diagnostic snapshots, not a combined
atomic transaction across scheduler, broker and cleanup. No packet bytes or credentials are
included. `clients.wifiRawTx` and `activeOperations` retain unfinished native work.

The native scheduler admits at most nine requests, grants them in request order,
and keeps the current grant through original native completion and required
Radio cleanup. Cancelling a waiting request removes only that request. Worker
queue saturation does not release an already granted operation into a second
send. The arbiter is shared by one-shot and Session producers. A Session retains
its grant while its native window is occupied, then rejoins the request order once
that window drains. A continuously replenished window may delay other producers;
there is no per-producer fairness guarantee. A temporary-rate Session retains its
exclusive grant until rate restoration.

The public SDK callback has no request cookie. For the pinned C3/S3/C5 SDK, a
hash-checked build-local adapter binds each native descriptor before submission
and consumes that exact descriptor before recycling. It changes two call-site
relocations, leaving RF bytes unchanged. Identical frames and out-of-order callbacks
therefore retain independent identities; callback order and MAC addresses are not
identity sources. Mismatching metadata or an unknown descriptor quarantines work. A late completion can retire its quarantined
operation; a missing completion, submission error with uncertain native ownership,
or correlation fault can keep the lane unavailable. Runtime teardown waits for
this native cleanup and can remain blocked. Runtime restart is not a recovery
guarantee. Explicit physical recovery is available through `wifi.rawTx.recover`.
The internal termination handoff now drains exact tokens and periodic tickets
once physical deinit is proven. The public recovery coordinator restores trustworthy saved configuration;
close/GC do not initiate physical deinit to bypass other owners. A device reboot resets the boot-owned state. Do not interpret the availability of this API as a successful RF/soak test.

## `wifi.rawTx.recover(options)`

Explicit physical recovery for an outstanding submitted Raw TX operation. Also
supports `Future.call`. Read `wifi.status().rawTx` once: pass its `operationIdentity`
as `sequence` and its `radioGeneration` as `radioGeneration`. These two fields come
from the same broker snapshot. A Session queue sequence, periodic ticket, arbiter
lane identity, or an independently read Radio generation is not this identity.

Only `sequence`, `radioGeneration` (integers 1–4294967295), `allowDisconnect`
(boolean, default false), and `timeoutMs` (integer 1–60000, default 10000) are
accepted. Capture completes before admission. The native begin revalidates the
complete broker token and original Radio lease under the mutation mutex; stale
inputs cannot select another operation.

The source must remain a healthy, started STA/AP/APSTA driver with trustworthy
saved configuration. Active native submission, another lifecycle/Radio operation,
foreign owners, scan/connect helpers or incompatible borrowed policies prevent
admission. A known temporary rate belonging to this original Raw TX owner is
allowed; its permanent predecessor is frozen for restoration. An uncertain rate,
pending failed rate restoration, already-faulted driver or unreadable saved
configuration cannot yet be recovered by this implementation.
Support for these sources remains planned work.

The call authorizes restarting the managed AP and disconnecting its clients.
An established Station additionally requires `allowDisconnect: true`. It does not
authorize stopping other Raw TX Sessions, ESP-NOW, CSI or Monitor owners.

Recovery shares the existing Action/ROC/FTM Future lane, configuration checkpoint,
AP/Station helper retirement and reconstruction. It stops Wi-Fi, seals the Raw TX
callback, drains SDK task work that might already hold that callback, then performs
physical deinit. Unregister or STOP alone is not termination proof. The original
Future/Session worker consumes `nativeTerminated`, releases its broker token and
Radio lease, and closes affected queued/periodic work. Recovery yields until that
owner retires; only then does it rebuild and replay the frozen configuration,
including the temporary rate's predecessor. It does not retransmit, restart
periodic jobs, reconnect Station or restore application sessions.

The result is `{sequence, previousRadioGeneration, radioGeneration}`. It confirms
framework reconstruction, not TX success, peer delivery or uninterrupted network
service. A result already delivered remains unchanged; physical termination can
produce a terminated Session outcome rather than a completion result.

Errors `WIFI_RAW_TX_RECOVERY_FAILED` / `WIFI_RAW_TX_RECOVERY_TIMEOUT` identify
`wifi.rawTx.recover`. Details contain `espCode`, `espName`, `stage`, selected
sequence/generation, `lifecycleAdmitted`, `checkpointAttempted`, `replayAttempted`,
`resumeAttempted`, `cleanupPending`, `restartRequired`, `radioFaultStage` and
`radioFaultError`. They never contain packet bytes or credentials. Cleanup/fault
fields are current shared observations; lifecycleAdmitted belongs to this attempt.

Before admission, cancellation has no driver effect. After admission, timeout or
cancellation ends restoration and leaves the exact lifecycle with central cleanup.
It cannot undo STOP/deinit or release native ownership early. Inspect status and
complete `wifi.stop()` cleanup before further work; successful suffixes are not
repeated. Runtime destruction also advances an already-admitted physical cleanup
while waiting for native owners/Futures, retaining runtime until all drain gates
pass. It does not initiate a new recovery. SDK calls are not preemptible, and failed
helper teardown can still require a device reboot. Result conversion may fail
after successful reconstruction; observe status before another mutation.

`wifi.rawTx.capabilities().supports.recovery` reports the reviewed C3/S3/C5 target
binding, not current eligibility or hardware qualification. This remains Candidate;
full competition, matrix and RF validation is pending.

## `wifi.rawTx.open(options?)`

Opens a `WiFiRawTxSession`, cooperatively waiting for native Radio acquisition.
It can also be used with `Future.call(wifi.rawTx.open, wifi.rawTx, [options])`.
Options are captured before Radio work; an opening timeout requests cleanup and
does not publish a partially opened JS object.

| Option | Values / default |
| --- | --- |
| interface | `"station"` / `"access-point"`; default `"station"` |
| channel | `"current"` or a supported numeric channel; default `"current"` |
| sequenceControl | `"driver"` / `"application"`; default `"driver"` |
| timeoutMs | Opening deadline, integer 1–2147483647; default 1000 |
| rate | Optional `WiFiTxRateConfig`; exclusive pre-start interface lease, described below |
| maxInFlight | Integer 1–8, at most `capacityPackets`; default 1 |
| queue.capacityPackets | Integer 1–128, including all in-flight packets; default 32 |
| queue.capacityBytes | Integer 24–`capacityPackets * 1500`; default `capacityPackets * 1500` |
| queue.overflow | `"reject-newest"` / `"drop-oldest-batch"`; default `"reject-newest"` |

The Session retains its Radio lease while idle. A numeric channel stays pinned
until close; `"current"` follows the actual channel observed at each submission.
Without `rate`, opening the AP interface requires an already running AP. Configuration, sequence
and association constraints are checked again at actual driver admission.

Admission and TX completion wake a coalesced native worker independently of JS
polling. A single boot-owned timer retries saturated worker admission and pending
cleanup; it disarms when Sessions have no work. No dedicated task stack is added.
AP temporary-rate helper transitions still run on the runtime task. Unknown options
are rejected.

`maxInFlight` controls outstanding driver submissions within a Session. Driver calls
remain serialized; Wi-Fi shares one physical transmitter/channel. Completion can be
out of order. This is a submission pipeline, not simultaneous RF transmission or a
throughput guarantee. `limits.maximumInFlight` reports the compiled maximum.

The queue byte budget counts retained frame payloads, including in-flight slots.
Transient JS capture, broker copies, SDK buffers and control storage are additional
wireless memory allocations. `capacityBytes` is not a whole-driver heap limit.

### Temporary Session rate

`rate` uses the same strict PHY/rate/ERSU/DCM capture as
[`wifi.driver.configureTxRate`](wifi-driver.md). It is an interface-global setting,
not a per-packet option. All getters are captured before Radio admission. It is
available for an initialized, fully stopped driver free of all owners/wake locks/
operations. Its previous rate must be known from a successful framework write in
the same Radio generation. Unknown defaults, running/shared Radio and insufficient
write-identity space fail without changing the rate or starting Wi-Fi.

For `interface: "access-point"`, first configure the desired AP with `wifi.configure`,
stop Wi-Fi to retire its owners, and set a known permanent AP rate using
`wifi.driver.configureTxRate("access-point", config)`. Opening borrows the rate and
starts that saved mode with the AP netif/DHCP helper and, for APSTA, a Station
helper. It does not connect Station or change its saved configuration/rate. The AP
may advertise and accept clients;
closing stops that AP and disconnects its clients. A numeric `channel` must match
the saved AP channel; use `"current"` for SDK automatic channel selection. Opening
does not rewrite AP configuration, change mode or restart a running AP. Helper
operations run on the runtime task;
SDK calls are not preemptible by a Future timeout.

During the Session, no new Radio owner or wake lock can join. The Session keeps
its shared Raw TX arbiter grant even when idle, so other sends wait under their
own deadlines. Idle grant retention does not repeatedly schedule a worker. A
rate Session therefore cannot share Radio with an existing Station/AP owner, ESP-NOW,
Monitor or CSI. Ordinary Sessions without `rate` retain existing sharing behavior.

Close/GC/open failure/runtime teardown use the same native cleanup path: retire
any SDK packet, stop the driver while retaining the exact Radio owner, then
restore the captured previous rate. An accepted stop whose event fence is still
pending is not repeated. A failed restore retains the Session, Radio lease,
previous configuration and arbiter grant; background service retries only the
unfinished cleanup. This remains true after the public Future times out or its
JS object is collected. Generic Radio release cannot bypass restoration. When
restoration completes, the driver remains stopped and the original rate is known;
this does not restart a prior connection or clear unrelated lifecycle faults.
For AP, runtime cleanup atomically exchanges the rate owner for an exclusive
lifecycle token, retires the AP helper and any Station helper it prepared or
whose earlier retirement failed, then releases that token. The Session and
arbiter grant remain retained through helper failure/retry. After an admitted
physical recovery, the original Session consumes termination and leaves helper
retirement/reconstruction to central recovery; it cannot close a rebuilt helper.

Inspect `wifi.driver.txRateStatus(iface).temporaryLease` for the exact owner,
write identity, previous config, restorePending and restoreError. It is null when
restoration is complete. `wifi.status().radio` records native stop/restore faults;
Session/global Raw TX cleanup status continues to cover unpublished failed opens.
Repeated restore failures can exhaust boot-scoped write identities; the remaining
owner stays diagnostic and no identity is recycled. A successful initial write
that cannot be returned as a JS object still follows this cleanup path.

```javascript
// Prerequisite: close all Wi-Fi/ESP-NOW/capture child resources first.
wifi.start();
wifi.stop();
wifi.driver.configureTxRate("station", {phy: "11g", rate: "6m"});
var tx = wifi.rawTx.open({rate: {phy: "11g", rate: "9m"}});
try {
  print(wifi.driver.txRateStatus("station").temporaryLease.radioLeaseIdentity);
  // tx.send(validFrame) or tx.startPeriodic(...) uses this interface setting.
} finally {
  tx.close();
}
print(wifi.driver.txRateStatus("station").config.rate); // 6m after successful close
```

A Session without a rate lease requires AP to be running before AP sends.
After a rate Session closes and leaves the driver stopped, explicitly start AP
before sending through such a Session. The stopped AP rate-opening path above
owns its startup and restoration. One-shot `wifi.rawTx.send()` and per-Session
`send()` have no rate option.

## `session.enqueue(frame)` and `session.enqueueBatch(frames)`

Copy and validate one ByteSource or an ArrayLike of ByteSources, then admit the
whole batch or reject it. Collection length is read once and bounded by the
Session capacity before descriptor allocation. All frames are captured before
queue mutation. Getter errors, invalid bytes and capture allocation failures
leave the queue unchanged. A getter that closes the Session cannot release the
native context still used by capture; final admission rejects the closed Session.

Admission returns `WiFiRawTxAdmission`:

- `sessionGeneration`, `firstSequence`, `lastSequence`, `batchSequence`.
- `admittedPackets`, `evictedPackets`, `evictedBatches`.

Sequences are monotonic Session admission identities, not broker callback IDs.
The batch identity is its first sequence. `enqueue` is a one-frame batch and
returns the same receipt shape. Admission does not prove driver acceptance or RF
delivery. Result-object allocation can fail **after** admission; inspect Session
stats/status before retrying an OOM instead of blindly duplicating the batch.

`reject-newest` rejects insufficient space without modifying the queue.
`drop-oldest-batch` evicts only complete batches that have never started; an
in-flight frame and the unsent remainder of its batch remain protected, including
between packets. If eligible batches cannot make enough room, admission fails
without eviction. Close can discard all unsent frames, including that remainder.

## `session.waitWritable(options?)`

Waits until the open Session has at least `minimumPackets` free slots (default 1)
and `minimumBytes` free payload bytes (default `minimumPackets * 24`, the minimum
frame size). Explicit 0 requests a packet-slot-only capacity observation. `timeoutMs` defaults to 1000;
all three options are integers, packet/byte thresholds must fit the configured
capacity, and the timeout range is 1–2147483647 ms. Returns `undefined`; also
supports `Future.call(session.waitWritable, session, [options])`.

It observes capacity without reserving it or evicting packets. A competing producer
may consume that space before enqueue. Close, fault or admission-identity exhaustion
fails the wait. Timeout/cancellation only stop waiting and never cancel admitted
frames. Use `minimumBytes` when the next batch size is known.

```js
var tx = wifi.rawTx.open({
  queue: { capacityPackets: 64, capacityBytes: 65536 },
  maxInFlight: 4
});
// frames is an application-provided collection of valid Uint8Array frames.
var bytes = 0;
for (var i = 0; i < frames.length; i++) bytes += frames[i].byteLength;
tx.waitWritable({ minimumPackets: frames.length, minimumBytes: bytes, timeoutMs: 5000 });
tx.enqueueBatch(frames);
var completion = tx.flush(5000);
tx.close();
```

## `session.send(frame, options?)`

Admits one packet and cooperatively waits for its exact native result. The only
per-call option is `timeoutMs` (integer 1–2147483647, default 1000). Use
`Future.call(session.send, session, [frame, options])` for asynchronous waiting.

Returns the one-shot `WiFiRawTxResult` fields plus `sessionGeneration` and
`admissionSequence`. The existing `sequence` remains the native broker identity.
Later packets cannot overwrite an earlier pending result. MAC failure is a
completed result with `driverStatus: "failed"`; an evicted/closed queued packet
produces an error. A submission with uncertain native ownership produces
`WIFI_RAW_TX_UNCERTAIN` and leaves native cleanup pending.

Timeout or cancellation stops observing this send. **After admission the packet
can still transmit, including when it was queued at the deadline.** Its result
record can be released immediately, while payload/driver/lease ownership stays
with the native Session. This differs from cancelling a one-shot before its worker
enters Radio. A timeout never cancels unrelated Session packets or producers.

## `session.flush(timeoutMs?)`

Waits for all admissions through the fence captured when the Future starts.
Default timeout is 1000 ms; allowed range is 1–2147483647. It also supports
`Future.call(session.flush, session, [timeoutMs])`. Enqueues after that fence cannot
extend the wait or alter its result.

Returns Session-cumulative `admitted`, `submitted`, `settled`, `succeeded`, `failed`,
`unknown`, `rejected`, `aborted`, and `dropped`, restricted to that fence, plus
`sessionGeneration`, `throughSequence` and `pending` (zero on successful return).
It includes drops and failures; completion does not mean every packet succeeded,
that a peer received anything, or that Radio close cleanup has finished.

Timeout/cancellation releases only this flush watcher and does not cancel queued
or in-flight packets. A missing native completion may leave the fence pending.
Outstanding flushes retain their own native Session reference through close.

## `session.status()`, `session.stats()` and `session.close()`

`status()` reports `state` (`open`, `closing`, `closed`, `faulted`), Session/Radio
generation, interface, observed channel, sequence policy, queue capacity, queued
and pending packet counts, active sequence, request identity and worker activity.
`queuedPackets` excludes in-flight packets; `pendingPackets` includes them.
`inFlight` and `maxInFlight` describe the native window. `capacityBytes`, `usedBytes`,
`availableBytes`, `highWaterBytes` and `availablePackets` describe queue ownership.
A representative `activeSequence` does not imply FIFO completion.
`requestIdentity` can represent a waiter that has not yet acquired the shared
grant. `periodicJobs` counts native periodic children still retaining timer/worker/
template ownership, including jobs still cleaning up after stop or close.
`faulted`, `error`/`stage`, and `cleanupPending`/`cleanupError`/`cleanupStage`
preserve operational failure and unfinished cleanup separately.

`stats()` returns the cumulative counters listed under flush for the whole
Session. Neither method starts Radio. Snapshots may describe a worker currently
inside a driver call; driver-owned fields publish when that step returns.

`close()` rejects new admission, drops queued frames and cooperatively waits up
to 1000 ms for native retirement and Radio cleanup. It supports
`Future.call(session.close, session, [])`. A close timeout leaves the native close
requested; it does not free an in-flight packet, forcibly reset shared Radio or
cancel an unrelated owner. Repeating close retries/observes the remaining cleanup.

After successful close, `status()` and `stats()` return a cached native snapshot
without retaining the old native queue owner. Close is then idempotent; send,
enqueue and new flush calls reject. This allows normal close/reopen without
waiting for JS GC. Outstanding result/flush owners still retain their own bounded
native records until released. GC requests close but never calls the SDK from a
finalizer.

Operational errors use `WIFI_RAW_TX_SESSION_FAILED`, `WIFI_RAW_TX_ADMISSION_FAILED`,
`WIFI_RAW_TX_INVALID_FRAME`, `WIFI_RAW_TX_UNCERTAIN` or `WIFI_RAW_TX_TIMEOUT`, with
`espCode`, `espName`, `stage`, `admitted`, `validationCode` and `queueCode` details.
`admitted` describes this send's queue admission, not proof of RF transmission.

Global `wifi.status().radio.rawTx` also reports `liveSessions`,
`retainedClosedSessions`, `closingSessions`, `faultedSessions`,
`registeredSessionResults` and `registeredSessionFlushes`. Registered counts
include completed records not yet released. `sessionErrorGeneration`,
`sessionError`/`sessionStage`, and `sessionCleanupError`/`sessionCleanupStage` show
the first observed Session diagnostic, including failed opens with no JS handle.
Global `cleanupPending` includes closing Sessions; individual Session status gives
its own diagnostics. This is a sequence of native snapshots, not a cross-Session
atomic transaction.

```js
function sendBatch(frames) {
  var tx = wifi.rawTx.open({ queue: { capacityPackets: 32 } });
  try {
    var receipt = tx.enqueueBatch(frames);
    var completed = tx.flush(5000);
    return { receipt: receipt, completed: completed };
  } finally {
    tx.close();
  }
}
```

## `session.startPeriodic(options)`

Captures a frame template and cooperatively waits for native timer startup.
It also supports `Future.call(session.startPeriodic, session, [options])`.
All policy getters run before the frame getter and ByteSource copy; the method
retains the native Session through capture, even if a getter closes its JS owner.
Final admission rejects a closing Session. No JS pointer or ByteView read lease
is retained by the timer or native worker.

| Option | Contract |
| --- | --- |
| frame | Required ByteSource; same MAC/SDK validation as Session.send |
| intervalUs | Required integer 1000–4294967295 microseconds |
| count | Integer 0–4294967295; default 0 continues until stopped or identity exhaustion |
| startDelayUs | Integer 0–4294967295; default 0, measured from native job creation |
| busyPolicy | `"skip"` (default) or `"stop"` |
| stopOnError | Boolean, default true |
| timeoutMs | Startup Future deadline, integer 1–2147483647; default 1000 |

Returns a `WiFiRawPeriodicTx`. There are at most 8 native jobs across all Sessions,
including retired jobs whose native caller reference has not yet been released.
Keep the Session and periodic JS objects reachable while sending: GC requests
close, and GC of the parent Session stops its periodic children.

A native timer submits bounded background work. No JS `setInterval` or JS polling
is needed after timer startup. The requested interval is a schedule, **not a
promise of timer precision or RF throughput**. A delayed worker skips older due
opportunities and attempts at most the most recent one, with no catch-up burst.
`count` limits schedule opportunities, including skipped ones; it does not count
successful transmissions. A finite job may already be stopped by the time startup
returns. Continuous jobs stop with exhaustion before their 32-bit counters wrap.

Each job has at most one uncompleted packet. Busy shared Radio, another Session
producer, or an outstanding periodic packet causes skip/stop according to
busyPolicy. Actual Session admission rechecks contention, never evicts another
producer, and reclassifies an admission race as the same skipped opportunity.
Other producers can still evict an unstarted periodic packet under the Session's
configured drop policy; that packet gets a dropped terminal result.

`stopOnError` stops after MAC failure, rejection or drop. Unknown MAC results are
counted separately. A Session/Radio operational fault or uncertain driver ownership
stops the affected job regardless of this setting. No rate setting is changed.

Startup timeout/cancellation requests native close. Already admitted packets may
still transmit; timer startup and a first packet may precede publication of the
JS object. Result-construction OOM cannot undo these effects. Inspect global
Raw TX/Session diagnostics before retrying an ambiguous startup error.

## `periodic.status()`, `periodic.stop()` and `periodic.close()`

`status()` returns:

- `state`: running, stopped, closing, closed or faulted; `periodicGeneration`,
  `intervalUs`, `count` and nullable `activeSequence`.
- `scheduled`, `issued`, `submitted`, `completed`, `failed`, `unknown`, `rejected`,
  `aborted`, `dropped`, `skippedBusy` and `skippedLate`.
- `retired`, `stopRequested`, `closeRequested`, `workerBusy`, `timerPresent`,
  `timerQuiesced`, `timerTransition`, `faulted`, `exhausted`, `uncertain`,
  `cleanupPending`, and nullable error/stage plus cleanupError/cleanupStage.

`scheduled = issued + skippedBusy + skippedLate`. Issued is a scheduling
reservation, not proof of queue/SDK acceptance. Submitted records SDK acceptance;
completed counts MAC terminal results, including failed/unknown. Failed also
includes rejected and dropped packets. These are worker-published observations;
a temporarily unchanged counter does not prove no RF has happened. Timer fields
reflect completed native steps; timerTransition indicates an SDK step in progress.

`stop()` synchronously prevents future periodic admissions. It does not retract
already admitted packets or wait for timer/native retirement. There is no resume;
start a new periodic job when needed. `close()` requests the same stop and waits
up to 1000 ms for the original packet, exact result record and timer cleanup. It
also supports `Future.call(periodic.close, periodic, [])`. A close deadline retains
close intent even when Future start was not dispatched. Explicit cancellation
before start can prevent that close operation; cancellation after start leaves
the native close requested.

Timer cleanup waits for callback quiescence before deletion. Failures retain
storage and retry only the unfinished suffix. A missing/uncertain native completion
can keep cleanup pending and block parent Session/runtime close. Close does not
force-reset shared Radio or stop unrelated producers. Internally, a proven
physical teardown can settle a retained uncertain ticket as `aborted`, including
an SDK-accepted packet; submitted remains counted and completed is not incremented.
This does not imply the packet was prevented from transmitting. Error history is
retained while native ownership uncertainty can be cleared after termination.

After native retirement, status/close caches the final snapshot and releases the
native caller reference; a retained JS object then occupies no native registry
slot or Session child. Closed status remains readable and repeated close is
idempotent. Finite completion or stop can retire native resources while the
object's state remains stopped until close is requested.

Errors use `WIFI_RAW_TX_PERIODIC_FAILED`, `WIFI_RAW_TX_INVALID_FRAME` or
`WIFI_RAW_TX_TIMEOUT`, with native error/stage, validationCode, periodicGeneration,
schedule/submission observations and cleanup details. Global
`wifi.status().radio.rawTx` adds `periodicJobs`, `retiredPeriodicJobs`,
`faultedPeriodicJobs`, `periodicCleanupPending` (a count), `periodicIdentityExhausted`,
`periodicErrorGeneration`, `periodicError`/`periodicStage` and
`periodicCleanupError`/`periodicCleanupStage`. It reports the first retained native
job diagnostic, including an unpublished failed startup; it is not a permanent
history or an atomic snapshot across all producers. Global cleanupPending also
includes unfinished periodic cleanup.

```js
function emitForAWhile(frameBytes) {
  var tx = wifi.rawTx.open({ channel: 6 });
  try {
    var periodic = tx.startPeriodic({
      frame: frameBytes, intervalUs: 10000, count: 100, busyPolicy: "skip"
    });
    try {
      sleep(1200);
      periodic.stop();
      return periodic.status();
    } finally {
      periodic.close();
    }
  } finally {
    tx.close();
  }
}
```
