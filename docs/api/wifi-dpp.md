# Wi-Fi DPP

`wifi.dpp` is a Candidate QR-code enrollee API. It is present when Wi-Fi, BSD
TCP/IP, IPv4 and `CONFIG_ESP_WIFI_DPP_SUPPORT` are enabled. Check for the module
before calling it. The sole contract is `wifi-dpp/1`.

```ts
wifi.dpp.capabilities(): WiFiDppCapabilities
wifi.dpp.status(): WiFiDppGlobalStatus
wifi.dpp.startEnrollee(options: WiFiDppOptions): WiFiDppSession
session.status(): WiFiDppStatus
session.watch(options?: { capacity?: number }): EventQueue<WiFiDppObservation>
session.receive(options?: { timeoutMs?: number }): WiFiDppEvent | null
session.connect(index: number, options?: WiFiDppConnectOptions): WiFiDppConnectResult
session.recover(): void
session.cancel(): void
session.close(options?: { timeoutMs?: number }): void
```

`receive`, `connect` and `close` have native Future drivers. Call them directly to wait
cooperatively, or use `Future.call(session.receive, session, options)` and
`Future.call(session.close, session, options)`.

## Starting an enrollee

Start a managed Station with `wifi.start({mode: "station"})` and leave it
disconnected before provisioning. Start captures and validates all options,
creates the Session, then schedules background bootstrap generation and listening.
It does not mean authentication or configuration has succeeded.

| Option | Meaning |
| --- | --- |
| `channels` | One to five unique integer channels, default `[6]`. Every channel must be supported by the fixed SDK and current Radio country policy. |
| `privateKeyHexDer` | Optional private bootstrap key as hexadecimal DER, at most 256 decoded bytes. Omit to generate a key. The reviewed SDK consumes DER, despite its raw-key header wording. |
| `info` | Optional printable ASCII, at most 128 bytes, excluding `;`. |
| `timeoutMs` | Overall provisioning deadline from activation, 1..3600000 ms, default 120000. Includes bootstrap and result capture. |
| `allowApChannelChange` | Default false. Explicit consent for channel interruption when an AP shares the Station's Radio. |

Scan/connect, fixed-channel owners and incompatible shared Radio operations
block admission. DPP retains the exact application/Station/AP leases through
native cleanup, channel restoration and Station helper drain. It does not
initialize another Radio or reset an unrelated owner.

## Receiving results

`receive()` returns one URI result, then one configuration-list result:

- `{type: "uri", uri: string}`: public bootstrap information to display to the
  configurator. The URI is not emitted by the global Wi-Fi watch.
- `{type: "configurations", configurations: WiFiDppConfiguration[]}`: received
  configuration rows. All rows are converted successfully before receipt is
  committed. Allocation/conversion failure leaves the result available to retry.

Each row has `index`, exact `ssidBytes` and `passwordBytes`, `akm`, `connector`,
`netAccessKeyBytes`, `cSignKeyBytes`, `netAccessKeyExpiry`, and `channel`. AKM is
`unknown`, `dpp`, `psk`, `sae`, `psk-sae`, `sae-dpp`, or `psk-sae-dpp`; hybrid rows
preserve both password and Connector material. Empty byte arrays and a null
Connector represent absent fields. The expiry hint is `{low, high}` to preserve
all 64 bits, or null for zero; delivery does not validate expiry. `channel` is
the provisioning exchange channel, not evidence of connection.

Configuration results contain credentials and private key material. They are
available only through the dedicated receive call. Status, watch and error
details contain metadata. Native copies are scrubbed on close; JavaScript
copies belong to the caller. Do not place configuration results in logs.

## Selecting and connecting

After receiving the configuration list, call `session.connect(index, options)`
with an explicit received index. Provisioning never selects or connects a
configuration automatically. `configurationSelection` is true and
`autoConnect` remains false.

| Option | Meaning |
| --- | --- |
| `authentication` | Optional `"dpp"`, `"wpa3-sae"` or `"wpa2-psk"`. Omitted selects the strongest authentication in the received AKM: Connector, then SAE, then PSK. An explicit choice must be present in that row. Missing build support fails without downgrading. |
| `allowApRestart` | Boolean, default false. In APSTA, explicitly permits AP interruption during the STOP/START needed to restore an eligible saved PMF-disabled Station configuration when the Session closes. Checked before credential mutation. Independent of `allowApChannelChange` during provisioning. |
| `timeoutMs` | Default 30000; integer 1..3600000. Covers configuration installation, Station connection, IP readiness and verification of negotiated authentication. |

```js
var connection = session.connect(chosenIndex, {timeoutMs: 30000});
// Use the connection while this Session remains open.
console.log(connection.connected, connection.authentication);
```

The result extends `WiFiConnectResult` with `configurationIndex`,
`connectionGeneration` and `authentication`. Success requires an IP-ready
Station and verification of the actual BSSID and negotiated AKM. A mismatch
fails and disconnects this Session's connection. Repeating the same index and
authentication after success returns the existing result without another
connection attempt; this also permits retry after result-conversion failure.
A pending or different selection cannot replace the current attempt.

The Session owns this connection and, for DPP authentication, its Connector.
Closing/canceling the Session or canceling a started connection Future requests
disconnection, then retires native protocol work, restores the previous Station
configuration, home channel and storage policy, and releases its leases.
Connection credentials are applied in RAM even when the previous policy was
FLASH. Native rows/temporary copies are scrubbed during cleanup. There is no
connection transfer to another owner or automatic retry after disconnection.

An eligible saved PMF-disabled Station configuration is restored through the
shared Radio STOP executor, the dedicated PMF-disable API, complete configuration
readback, START and its event fence. The original leases and generation remain
owned throughout. The SDK's authentication and WPA3-compatible-mode requirements
must permit restoring disabled PMF; unsupported saved states fail before mutation.
In APSTA, this requires `allowApRestart: true` and can disconnect AP clients.
Station-only restoration needs no AP permission. Power save, maximum TX power
and enabled-interface inactivity thresholds are restored and checked in RAM
before restoring the original storage policy. Background STOP/START waits use
the native event fence without entering the JavaScript runtime's wait scope.
A saved PMF-capable configuration
uses the existing restoration path without a Radio restart.

Status exposes `restoreRequiresRestart`, `restoreStopped`, `restoreStarted`,
`restoreStartFailed`, `restoreStartError`, `recoveryPending` and `recoveryAttempts`.
An accepted STOP/START whose event fence is delayed is not resubmitted. A failed
SDK START remains a Radio fault with retained owners; ordinary close and runtime
restart do not retry that mutation or clear the fault.

`session.recover()` requests one retry specifically for this Session's known
restoration START failure. It takes no arguments and returns immediately; native
admission runs in the existing worker. It requires a closing Session without an
active connection and rejects requests during runtime teardown. It is idempotent
while recovery is pending. Call `session.close({timeoutMs: ...})` to wait for the
remaining cleanup and inspect status/error details if the wait expires.

The worker rechecks the exact operation, original helper owners, known RAM
storage, retired native protocol and fault source. It preserves the original
START error until STOP and its event fence complete, then checks/restores the
saved configuration and permits one new START. A second SDK START failure parks
cleanup again and requires another explicit request. `recoveryAttempts` counts
admitted requests and fails at its maximum instead of wrapping. This recovery
finishes closing; it does not reconnect or transfer the connection. The AP
interruption consent captured by `connect` still applies. Unknown handoffs,
unrelated faults and exhausted identities remain retained and may require
device reboot; this API does not claim recovery for those sources.

SSIDs containing
NUL are delivered exactly by `receive()`, but cannot be passed to the SDK Station
configuration without truncation and are rejected by `connect()`. PSK accepts
8..63 password bytes or exactly 64 hexadecimal bytes; SAE accepts 1..63 bytes.
Passwords containing NUL are rejected. Connector authentication supplies no
password fallback, and DPP/SAE require PMF.

## Timeouts and closing

| Operation | Timeout/cancellation behavior |
| --- | --- |
| `receive({timeoutMs})` | Default 1000 ms. Wait expiry returns null; canceling its Future ends only that wait. Provisioning continues under its overall deadline. |
| `connect(index, options)` | Timeout requests Session close and reports `WIFI_DPP_TIMEOUT`; canceling a started Future also requests close. Cancellation before start does not close the Session. Failed cleanup retains the connection generation and native resources. |
| Overall provisioning deadline | Requests close, revokes delivery and reports `WIFI_DPP_TIMEOUT`. Raw native references and leases remain until cleanup completes. |
| `cancel()` | Requests Session close immediately without waiting. Repeated requests are safe. |
| `recover()` | Queues one known-START-failure recovery request. No Future or wait deadline; repeated calls while pending do not authorize more attempts. Use `close` to wait. |
| `close({timeoutMs})` | Default 1000 ms. Wait expiry throws `WIFI_DPP_TIMEOUT`; cancellation or expiry leaves the already-started cleanup active. Retry close to wait for the remaining suffix. |

After successful receipt, another receive throws a consumed-result error.
Close is idempotent after retirement. GC and runtime teardown request the same
cleanup; they do not free an active worker or an unconfirmed IPC argument.
Unconfirmed native handoff remains a retained fault and can require device
restart. `sys.restartRuntime()` is not evidence that this fault can recover.

## Status and observation

Status reports state, exact operation/generation, result availability, copied
configuration count, cleanup progress and native errors. `reservedBytes` covers
the framework Session/Radio/worker/result and pending TX copies; SDK-internal
allocations and watch queues are separate. A native off-channel record ending
does not mean its transmitted buffer has been freed: `txBufferPresent`,
`txRecycling` and `txRecycled` distinguish that lifecycle. A failed HMAC wake is
retried without appending/transmitting the same frame again.

`watch({capacity})` supports capacity 1..16 (default 8), one observer per Session,
four retained queues globally, and drop-newest overflow. It emits
`{sequence, status}` snapshots. Intermediate transitions can coalesce. A full
queue cannot prevent a receive/connect Future, terminal result or close from completing.
Terminal observation waits for the corresponding public Future to finish.

There are at most four retained Session handles and one active Session, including a Session that owns a connection. A
closed handle/queue still counts toward its allocation budget until released.
Hardware/RF interoperability, coexistence, fault injection and runtime lifecycle
acceptance remain pending; the API remains Candidate.

```js
if (wifi.dpp) {
    wifi.start({mode: "station"});
    var session = wifi.dpp.startEnrollee({channels: [6], timeoutMs: 120000});
    try {
        var complete = false;
        while (!complete) {
            var event = session.receive({timeoutMs: 1000});
            if (event && event.type === "uri") {
                console.log(event.uri); // Public bootstrap information for the configurator.
            } else if (event && event.type === "configurations") {
                console.log("configuration count", event.configurations.length);
                complete = true;
            }
        }
    } finally {
        session.close({timeoutMs: 5000});
    }
}
```
