# Wi-Fi WPS

`wifi.wps` and `WiFiWpsSession` are present with Wi-Fi, BSD TCP/IP and IPv4.
This Candidate API implements Station enrollee PBC/PIN. It has not completed
concentrated runtime or RF qualification. The fixed SDK also supports AP
registrar when `CONFIG_ESP_WIFI_WPS_SOFTAP_REGISTRAR` is enabled (it depends on
SoftAP). SoftAP alone does not enable registrar. That build also exposes
`wifi.wps.startAP`, `wifi.wps.apStatus` and `WiFiWpsAPSession`, described below.

## `wifi.wps.start(options)`

Returns a Session after validating options, constructing its JS handle and
reserving the managed Station helper. SDK admission/start run on the background
worker. A returned handle does not prove native acceptance: use status/receive.
`start` is synchronous and is not a Future operation.

Station must already be started, idle and disconnected on a home channel allowed
by the target and country policy. The enrollee advertises the configured band
mode, including 5 GHz on supported targets. An unreadable or invalid band mode
fails M1 construction before message allocation or nonce generation; it does not
silently advertise 2.4 GHz. This follows the existing native protocol failure
path, without a separate public SDK band-read error.
Other Radio operations, fixed-channel leases, promiscuous capture, wake locks,
Enterprise borrows and another pending WPS/SmartConfig operation block admission.
APSTA additionally requires `allowApChannelChange: true`. WPS may interrupt its
channel while negotiating. Application/Station/AP identities remain pinned until
native cleanup, original Station configuration/channel/storage restoration and
Station DHCP/IP/event retirement finish. It does not stop another owner to gain
admission. WPS temporarily uses RAM storage; received credentials are not saved
to FLASH or automatically transferred to an application Station connection.

| Option | Meaning |
| --- | --- |
| `method` | `pbc` (default) or `pin`. |
| `pin` | PIN method only: exactly eight ASCII digits with valid WPS checksum. Absent or `00000000` requests SDK generation, not the literal all-zero PIN. |
| `device` | Plain options with `manufacturer` (0–64 UTF-8 bytes), `modelNumber`, `modelName`, `deviceName` (each 0–32 bytes). Empty/absent fields use SDK defaults. NUL is rejected. |
| `timeoutMs` | Session acquisition and stable delivery budget, integer 1–3600000 ms, default 120000. |
| `allowApChannelChange` | Boolean, default false. Explicit APSTA channel interruption consent. |

Unknown keys, malformed PINs, coercible/fractional numbers and invalid device
strings reject before Session allocation or driver changes. Device strings are
copied literally, including percent characters. PIN/config native copies are
scrubbed on failure/close; caller-owned JS strings cannot be erased by cleanup.

One active Session is allowed, including pending cleanup; up to four retained
Session handles may exist. Closed objects count until all public/Future/watch
references are released. No force-open overrides pending cleanup.

## `session.receive(options?)`

Supports direct waiting or `Future.call(session.receive, session, options)`.
Options accept only `timeoutMs`, integer 1–3600000, default 1000. Three budgets
have distinct effects:

- Receive wait expiry returns `null`; cancelling its Future cancels only that
  wait. Acquisition continues under the Session deadline.
- Session timeout revokes PIN/credential delivery and requests native cleanup;
  receive throws `WIFI_WPS_TIMEOUT`. It covers scheduling, negotiation, native
  capture retirement, restoration and stable credential handoff.
- The reviewed SDK has its own fixed 120-second negotiation timeout and a
  20-second per-connection timer. A larger Session budget cannot extend those
  SDK limits. None of these budgets preempts a native call already running.

Receive transfers one of these objects:

- `{type: "pin", pin: "12345670"}`: an eight-character secret PIN. A pending PIN
  is delivered before credentials. PBC does not produce this result.
- `{type: "credentials", credentials: [...]}`: one to three entries, each with
  `ssidBytes`, `passwordBytes`, `authType`, `encryptionType`, `keyIndex`, `mac`.
  Byte arrays preserve explicit native lengths, embedded NUL and full-capacity
  32-byte SSIDs/64-byte keys. Authentication/encryption values are raw WPS bit
  masks, not SDK `WIFI_AUTH_*` enums. `mac` is the WPS credential attribute, not
  an inferred AP BSSID.

PIN and credential transfers are each one-time. Native storage is consumed and
cleared only after the entire JS result converts successfully. GC/allocation
failure preserves it for retry. Concurrent receives compete for the same result;
receive after credentials have been consumed throws ReferenceError. PIN/keys
never appear in status, errors or observations. Returned JS objects are secrets
owned by the caller; closing Session does not erase those copies.

Credentials are delivered only after capture has stopped, original configuration,
channel and storage have been restored, and Station events/IP have drained.
Session still owns Radio until close succeeds. Close before manually connecting;
choose credentials and the connection's security policy explicitly. WPS capture
success is not proof of a usable connection or Internet access.

## `session.cancel()` and `session.close(options?)`

`cancel()` synchronously revokes result delivery and requests cleanup; it does
not wait for native retirement. It is idempotent and not Future-capable.

`close()` requests the same cleanup and waits until Radio and Station ownership
have retired. It supports Future and the same wait options as receive. A close
wait timeout throws `WIFI_WPS_TIMEOUT` with `details.waitTimedOut: true`; cleanup
continues. Cancelling an already-started close Future also leaves cleanup active.
Cancelling a close Future before it starts does not request close. Retrying close
waits on the retained cleanup, retrying only failed native cleanup suffixes.

GC of the last Session/Future/watch reference requests close. The registry and
worker retain native storage until retirement is proven. Unknown native handoff
stays diagnosable and blocks reuse; runtime restart is not guaranteed recovery.
Runtime teardown closes observations before waiting for native cleanup and only
then retires the Wi-Fi helper.

## Status and observation

`session.status()` reports metadata: state, exact operation/radio generation,
64-bit `nativeIdentity` as `{low, high}`, result readiness/consumption, worker
activity, capture/restoration/helper progress, timeout, handoff/tracking faults,
and separate operation/cleanup errors and stages. `reservedBytes` counts only
Session storage, excluding Radio/worker/SDK/queue allocations. SDK start/event
fields are historical snapshots, not proof that the SDK is currently active.

`wifi.wps.status()` reports active Session metadata, handles, workers and runtime
closing. `wifi.wps.capabilities()` reports `wifi-wps/1`, Candidate stability,
implemented enrollee/PBC/PIN, budgets and limits. `apRegistrar` reports the implemented AP role in registrar-enabled builds;
`sdkApRegistrar` describes the corresponding fixed SDK flag. AP has separate
`maxAPSessions` and `maxAPWatchQueues` budgets (four each, zero when disabled). `autoConnect: false`, `sdkTimeoutMs: 120000` and
`sdkTimeoutConfigurable: false` state the current behavior explicitly.

`session.watch({capacity?})` returns `EventQueue<WiFiWpsObservation>` with initial
and changed `{sequence, status}` snapshots. Capacity is 1–16, default 8. At most
one observer per Session and four retained queues globally are allowed. Closed
queues keep their budget until queue storage is freed. A watch retains Session
until queue close/GC. Worker activity alone does not generate observations.

Observation is sampled and drop-newest. Intermediate transitions may coalesce;
sequence gaps identify dropped snapshots. Sequence exhaustion closes the queue
without wrapping. Future terminal processing takes precedence over observations;
queue saturation cannot prevent PIN/credentials, disconnect or close completion.
Observation is not a reliable control-completion stream.

Operational errors use `WIFI_WPS_FAILED`, `WIFI_WPS_TIMEOUT` or `WIFI_WPS_CLOSED`
with `operation` and metadata-only `details`, including raw `espCode`, `espName`
and `waitTimedOut`. Invalid JS input uses ordinary TypeError/ReferenceError.

```js
if (wifi.wps) {
    var caps = wifi.wps.capabilities();
    print(caps.apiVersion, caps.stability, caps.enrollee);
}
```

Full security/failure injection, native scheduling/GC/queue tests,
RF interoperability, restart/coexistence and warmed heap comparisons remain
pending. Successful compilation does not promote this feature's stability.

## AP registrar: `wifi.wps.startAP(options)`

Available only when `capabilities().apRegistrar` is true. This synchronous method
validates/copies options, constructs a `WiFiWpsAPSession`, reserves existing
helpers and schedules native admission. Check status/receive for asynchronous
SDK failure. It does not implicitly start AP. No AP password is accepted here:
the registrar supplies the already configured AP credentials to the enrolling
peer. Current SDK validation requires a supported PSK/encryption configuration.

Options are `method`, `pin`, `device` and `timeoutMs`, with the same strict types,
lengths, PIN checksum/generation and Session budget as Station WPS. The
Station-only `allowApChannelChange` field is rejected even when false. The AP
must already be managed and running on a target-supported, regulatory-valid
channel. 5 GHz is supported when `capabilities().apRegistrar5GHz` is true.
The build-local registrar reads the current RF band at initialization and for
each M2/M2D message; an APSTA band change is reflected in subsequent messages.
Unknown or failed band reads never fall back to 2.4 GHz. Initialization preserves
the native read error; a later unavailable band fails message construction before
allocation/key derivation and enters the existing protocol failure/cleanup path.
This capability describes implementation, not peer interoperability or RF qualification.

APSTA may keep its existing Station connection. AP WPS does not initiate a scan,
change channel/storage/configuration or disconnect Station. It pins the current
application/AP/STA helpers and blocks competing connect, scan, explicit
disconnect, stop and other Radio mutations until cleanup releases ownership.
Normal Station events still update its connection state. AP and Station WPS,
SmartConfig and incompatible Radio owners cannot run concurrently.

AP Session exposes `status`, `watch`, `receive`, `cancel` and `close`. Its wait,
Future cancellation, copy/commit, GC and queue semantics match the Station
Session, with these role-specific results and effects:

- PIN mode may first deliver `{type: "pin", pin: "12345670"}`. This PIN is secret;
  it is never copied into status/errors/watch.
- The first successful native registration delivers `{type: "registered", mac}`
  after native capture cleanup and the event fence. It is one-time and is not
  proof of a current client association, DHCP address or Internet access.
- Result-conversion allocation failure preserves native storage for another
  receive. Receive after consuming the registration throws ReferenceError.
- Session timeout, cancel and close stop this registrar and revoke undelivered
  results. They preserve the AP/STA helpers and ordinary clients; an unfinished
  WPS exchange can fail when its registrar closes. Close does not stop AP.
- Success ends this capture; it is not a continuous multi-client provisioning
  service. Close before opening another WPS Session.

`wifi.wps.apStatus()` reports AP registry handles/workers and active Session
metadata. `WiFiWpsAPStatus` uses `resultReady`/`resultConsumed`, a `registered`
state, and no Station configuration-restoration fields. Its native identity is
32-bit, represented as `{low, high: 0}`. `helperDrained` means this Session's
reservation has been released, not that AP/STA netifs have been destroyed.
Native cleanup-stage values belong to the AP implementation. AP observations
have a separate four-queue budget, one per Session, capacity 1–16 (default 8).
Both roles share the same `WIFI_WPS_*` error codes; `operation` identifies the
role and method. Unknown native handoff retains resources and blocks reuse.

```js
if (wifi.wps && wifi.wps.startAP) {
    // A compatible managed AP must already be running.
    var registrar = wifi.wps.startAP({method: "pbc", timeoutMs: 120000});
    try {
        var registration = registrar.receive({timeoutMs: 120000});
        if (registration && registration.type === "registered") {
            print("WPS registered", registration.mac);
        }
    } finally {
        registrar.close({timeoutMs: 5000});
    }
}
```

AP public bindings are Candidate. SDK scheduling, APSTA continuity, failed
cleanup retries, VM/GC/OOM/queue execution and RF interoperability await the
concentrated Wi-Fi validation phase.
