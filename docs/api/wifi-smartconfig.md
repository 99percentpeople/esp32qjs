# Wi-Fi SmartConfig

`wifi.smartConfig` and `WiFiSmartConfigSession` are available with Wi-Fi,
BSD TCP/IP and IPv4. This is a Candidate credential acquisition API. SDK protocol
support and successful compilation do not establish RF or lifecycle qualification.
Explicit automatic Station connection and local phone ACK submission are also
implemented, together with ESPTouch v2 custom data and dedicated observation
queues. Capabilities describe implemented operations, not RF qualification.

## `wifi.smartConfig.start(options)`

Returns a Session synchronously after reserving native storage and constructing
the JS handle. Actual Radio admission and SDK start run in a background worker.
A returned handle therefore does not mean that the driver accepted the operation;
inspect `session.status()` or call `session.receive()` for failure/results.
This scheduling method has no native Future driver. Receive and close do.

Requires the managed Station helper to be started, idle and disconnected, on a
2.4 GHz home channel. It does not initialize or start Radio, disconnect Station,
change band policy, or stop another owner. APSTA requires explicit
`allowApChannelChange: true`, authorizing channel interruption during decoding.
Radio rejects other operations, fixed-channel owners, wake locks, promiscuous
capture and borrowed Enterprise configuration. Exact Application/Station/AP lease
identities remain pinned until decoder cleanup and home-channel restoration finish.

| Option | Meaning |
| --- | --- |
| `protocol` | `esptouch` (default), `airkiss`, `esptouch-airkiss`, or `esptouch-v2`. |
| `fastMode` | Boolean, default false. |
| `channelTimeoutSeconds` | SDK find-channel timeout, integer 15–255; default 15. |
| `aesKey` | Optional ESPTouch v2 key: exactly 16 non-NUL UTF-8 bytes. Rejected for other protocols. |
| `timeoutMs` | Acquisition and stable credential handoff deadline, integer 1–3600000; default 120000. |
| `allowApChannelChange` | Boolean, default false. Required to admit APSTA. |
| `autoConnect` | Boolean, default false. Connect using the decoded credentials, submit the phone ACK, and retire the Session's native owners. |
| `connectionTimeoutMs` | autoConnect only: integer 1–3600000, default 30000. Station connection/IP deadline. |
| `minimumAuthMode` | autoConnect only: `wpa2-psk` (default) or `wpa3-psk` (requires SDK SAE). |
| `pmf` | autoConnect only: `optional` (default), or `required`. WPA3 defaults to and requires `required`. |
| `allowOpenNetwork` | autoConnect only: boolean, default false. Explicitly allow a decoded empty password. Incompatible with WPA3 or required PMF. |

Unknown keys, coercible strings, fractional values, invalid key length/NUL and
invalid protocol/key combinations fail before allocating the native Session or
changing the driver. Connection-only options without `autoConnect: true` reject.
WPA3 never falls back to WPA2; PMF is never disabled. A 64-byte WPA2 password must
be hexadecimal PSK; shorter WPA2 passwords require 8–63 bytes. WPA3 requires 1–63
bytes. Empty passwords require `allowOpenNetwork`; nonempty passwords still use
the requested security threshold. Decoded invalid SSID/BSSID/password rejects
before Station configuration. Accepted configuration follows Radio's existing
RAM/FLASH storage policy, including credential persistence when FLASH is selected. Keys are copied before activation. Native key copies are
scrubbed on failure/close; the original JS string remains application-owned.

There is one active Session, including pending cleanup, and at most four retained
native handles. Closed handles count until their public/Future references are
released. There is no force-open or overwrite of a previous pending Session.

## `session.receive(options?)`

Supports direct waiting and `Future.call(session.receive, session, options)`.
Accepts only `timeoutMs`, integer 1–3600000, default 1000. This wait budget begins
when the Future is scheduled; expiry returns `null` and cancellation only cancels
that wait. Neither action stops acquisition or extends the Session deadline.

The Session deadline is different: it covers background submission, RF capture,
capture termination, home-channel restoration and stable native credential
transfer. With autoConnect it also covers Station connection/IP and local ACK
submission until successful connection handoff. Final native cleanup remains
owned and must finish before successful receive; cleanup failures remain visible. If it expires first, the Session requests close and receive throws
`WIFI_SMARTCONFIG_TIMEOUT`. It is not a hard limit on an uninterruptible SDK call.

Successful receive returns one plain object:

- `ssidBytes`: SDK bytes up to the first NUL, or all 32 bytes when no NUL exists.
- `passwordBytes`: secret SDK bytes up to the first NUL, or all 64 bytes.
- `customDataBytes`: exact ESPTouch v2 binary bytes, length 0–64, including embedded
  NUL and trailing zero bytes; an empty array means zero bytes. Other protocols return null.
- `protocol`: decoded protocol name.
- `bssid`: selected MAC string, or null when not specified.

The SDK credential event has fixed C-string fields and no explicit lengths.
The API does not invent embedded-NUL SSID support, decode arbitrary bytes as UTF-8,
include struct padding, or publish phone/token metadata. Treat the result as a
secret; do not log or attach it to diagnostics.

Receive is a one-time transfer: native credentials are committed and wiped only
after the complete JS object is constructed. Allocation/conversion failure keeps
the original native credentials for another receive. A subsequent successful
receive attempt after consumption throws ReferenceError. Concurrent receive
Futures compete for this same one-time result; there are no duplicate copies by
policy. Returned JS arrays belong to the caller and survive Session close; native
cleanup cannot erase application copies or guarantee VM heap scrubbing.

With `autoConnect: false`, capture has stopped, the home channel is restored and
the default event-loop fence has passed before delivery, but the Session still
owns Radio. Close it successfully before manually calling `wifi.connect()`; this
manual path does not acknowledge the phone.

With `autoConnect: true`, receive waits for the complete automatic flow. After
capture/native/event retirement, one exact connection borrow reuses the Station
helper's generation, IP/disconnect handling and timeout timer. No fake Future
token is registered. Ordinary connect/disconnect/scan and other Radio mutations
cannot replace this owner while it connects or acknowledges. Terminal metadata
is committed directly; observation queue saturation cannot lose the result.

The ACK starts only after this connection gets IPv4. Its successful completion
means local UDP submission, **not proof that the phone received it**. Once the
ACK task/socket has retired, the connection is transferred to the application,
the Session releases its decoder/Radio resources, and the credentials become
readable. `connectionTransferred` records the irreversible handoff;
`completed` additionally requires native Session cleanup. The application can
keep using the established connection after receive/close. A naturally lost
connection is not automatically reconnected or a reason to resend the ACK.

## `session.close(options?)`

Supports direct waiting and `Future.call`; accepts the same timeout option as
receive, with default 1000 ms. Once scheduled, close immediately revokes delivery
and requests native cleanup. It returns `undefined` only after decoder, timers,
ACK reservation and Radio ownership are retired and the channel is restored.
It is idempotent; an acquisition error remains visible in status after close.

Close timeout throws `WIFI_SMARTCONFIG_TIMEOUT` but does not abandon cleanup.
Cancelling an already-started close cancels its wait; cleanup continues. Cancelling
before execution does not request close. A later close can wait for that cleanup.
The registry and worker own independent references after any public wait ends.

During autoConnect, close/GC/acquisition timeout before connection handoff cancels
only the exact connection generation started by this Session and waits for its
IP/timer fences. After `connectionTransferred`, close preserves the application's
connection even if later native cleanup fails. Closing cannot undo configuration
already accepted/persisted by the SDK. A receive wait timeout does not cancel the
Session or disconnect it; call close explicitly when abandoning the whole flow.

Dropping the last public handle, waiting Futures and open watchers requests close. A pending
Future retains the native Session through GC; it does not keep secret JS objects
rooted. Runtime destruction requests close and retains native storage until workers
and SDK references drain. Unknown native handoff or repeated cleanup failure can
block reopening/runtime destruction and may require device reboot; runtime restart
is not promised recovery.

## `session.watch(options?)`

Returns `EventQueue<WiFiSmartConfigObservation>` with `{sequence, status}` items.
`capacity` is an integer 1–16, default 8; unknown options reject before queue
allocation. One observer can be open per Session. At most four queue contexts
may be retained globally, including closed queues whose buffers await GC.

The runtime publishes an initial metadata snapshot and subsequent changes.
Intermediate native events may coalesce; this is not an ordered RF event log.
`workerBusy` alone does not emit. No SSID, password, AES key, custom bytes or
phone metadata enters this queue. Terminal snapshots wait for corresponding
captured receive/close Futures to finish. Observers never drive native completion.

Overflow drops the newest snapshot; `queue.stats().dropped` and sequence gaps
report loss. A dropped final snapshot is not replayed: use `session.status()`
for current authoritative state. Sequence exhaustion closes the observer rather
than wrapping. The queue stays open after Session retirement so a final queued
snapshot can be read; close it explicitly when observation is finished.

An open watcher retains the native Session through public-handle GC. Queue
close/GC releases that reference; if it was the last external owner, normal
Session cleanup starts. Closing a queue while the Session handle is still held
does not cancel provisioning. Runtime teardown detaches every watcher before
native Session teardown. JS conversion failure consumes that observation only;
it cannot consume credentials or affect the connection.

## Status, capabilities and errors

`session.status()` is a cached metadata-only observation: state, exact operation
and Radio generation, decoder identity as unsigned `{low, high}` words, worker
state, capture progress, credential availability/consumption, channel restoration,
close/timeout/cleanup state and raw error stages. `reservedBytes` covers the
Session allocation only; it excludes SDK allocations and separate native timer,
event and Radio records. No status/error includes SSID, password, key or custom data.

`wifi.smartConfig.status()` atomically observes active Session metadata, retained
handle count, worker count and runtime admission. It also diagnoses an abandoned
handle whose cleanup still owns Radio. No SDK query is performed by either status
method. A cached `driverStarted` describes accepted start, not current RF activity;
use `captureStopped` and lifecycle state as well.

`wifi.smartConfig.capabilities()` reports `apiVersion: "wifi-smartconfig/1"`,
`stability: "candidate"`, the SDK `version`, protocol names, ESPTouch v2/encryption
support, `autoConnect: true`, `acknowledgement: true` (the automatic flow only),
`customData: true`, `observations: true`, `maxWatchQueues: 4`,
`maxWatchCapacity: 16`, `maxSessions: 4`
and `maxActiveSessions: 1`. SDK version is third-party metadata, not an API format
version or a hardware test result.

Operational errors use `WIFI_SMARTCONFIG_FAILED`, `WIFI_SMARTCONFIG_CLOSED`, or
`WIFI_SMARTCONFIG_TIMEOUT`. Details contain the non-secret status snapshot plus
`espCode`, `espName` and `waitTimedOut`. Invalid inputs and reused consumed
credentials use normal JS TypeError/ReferenceError; JS allocation errors remain
VM errors. SDK decoder allocation failure is recorded as `ESP_ERR_NO_MEM` at
`smartconfig-allocation` and reaches `receive()` as `WIFI_SMARTCONFIG_FAILED`.
The worker requests cleanup without waiting for the acquisition deadline or an
observation event. Once that native failure is recorded, retained credentials
cannot be copied or committed as a successful result. A close timeout's
`waitTimedOut` is distinct from the acquisition deadline's
`timedOut`. Status after timeout remains the authority for pending cleanup.

```js
if (wifi.smartConfig) {
    wifi.start({mode: "station"});
    var session = wifi.smartConfig.start({protocol: "esptouch", timeoutMs: 120000});
    try {
        var credentials = session.receive({timeoutMs: 120000});
        // Pass credentials privately to application logic; never print them.
    } finally {
        session.close({timeoutMs: 10000});
    }
}
```

SDK decoder heap allocations are scrubbed before release. The pinned C3/S3/C5
build copies also clear the reviewed local frames of the ESPTouch, ESPTouch v2
and AirKiss credential decode functions before return or tail call, including
temporary password, event and v2 AES-key buffers. This adds no allocation or
retained owner. Saved registers and the S3 register-window spill area are preserved.
SDK drift is rejected during generation. This covers those decoder local buffers;
it is not a claim of erasing all task/register memory or durable configuration.
Complete decoder allocation-failure, interrupt/ABI and physical erasure
qualification remain pending. Concentrated
Host/VM/RTOS/GC/OOM and real-device tests follow the Wi-Fi implementation wave;
long soak stays deferred until BLE APIs are also complete.
