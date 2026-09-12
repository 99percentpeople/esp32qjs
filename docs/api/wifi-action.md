# Wi-Fi Action TX

`wifi.action` is available with `wifi`. This sole v1 API is **Candidate**: compilation
and contract checks do not establish RF delivery, cancellation, or runtime safety
qualification. ROC Session and explicit readable-source physical recovery are implemented;
unreadable/faulted-source recovery and complete runtime/RF qualification remain pending.

## `wifi.action.send(options)`

Returns `WiFiActionResult`. Use `Future.call(wifi.action.send, wifi.action, [options])` for an
asynchronous Future, or call directly to cooperatively wait. All option getters
and payload copies complete before Radio admission.

| Option | Contract |
| --- | --- |
| `interface` | `station` (default) or build-enabled `access-point` |
| `channel` | Required target-supported integer channel, also checked against current regulatory policy |
| `secondaryChannel` | `none` (default), `above`, or `below`; native channel-pair validation still applies |
| `destination` | Required nonzero MAC string, exactly `xx:xx:xx:xx:xx:xx`, case-insensitive |
| `bssid` | Optional nonzero MAC string; omission requests the SDK's broadcast BSSID |
| `payload` | ByteSource containing **only the Action body**, including its category; 1–1476 bytes |
| `waitMs` | Native target-channel residency, integer 1–60000 ms, default 100 |
| `timeoutMs` | Public waiting deadline, integer 1–60000 ms, default 1000 |
| `noAck` | Boolean SDK no-ACK request, default false |

The SDK generates the MAC header, interface source address and sequence control.
No caller MAC header or FCS is accepted here. `wifi.rawTx.send` accepts full frames
under its separate contract. The Action body is opaque; this API supplies neither
an Action receive stream nor a claim that arbitrary categories/security exchanges
are supported. Normal SDK security and frame handling still apply.

The Radio must already be healthy, configured and started in the selected mode.
There is no implicit initialization, start, mode change, association, or disconnect.
One native Action/ROC operation can own the lane. Incompatible lifecycle, scan,
TX and fixed-channel owners reject admission. Off-channel operation rejects an
associated Station, any AP mode, and incompatible capture/ESP-NOW owners. Normal
current-channel operation does not grant permission to change other owners' RF state.

The result contains `sequence` (boot non-reused framework identity),
`radioGeneration`, `operationId` (native 8-bit diagnostic, **not** a reusable token),
`interface`, `channel`, `payloadBytes`, `driverStatus` (`success`, `failed`, `unknown`)
and `terminalStatus` (`duration-completed`, `cancelled`). Sending success is only an
SDK observation, never peer receipt or protocol success. A native cancellation
can be caused by higher-priority work. The Future waits for residency termination,
not only TX_DONE; missing TX status is reported as `unknown`.

SDK admission/submission failures throw `WIFI_ACTION_SEND_FAILED`; deadline expiry
throws `WIFI_ACTION_TIMEOUT`. Details include this call's `stage`, raw `espCode` and
`espName`. Timeout/cancel stops waiting and requests native cancellation after the
submission worker returns. It cannot interrupt a blocked SDK call or undo an RF
transmission. A successful cancel request is not itself proof of termination.

After public completion, the exact native token is retained in a fixed cleanup
slot. Radio release requires native terminal evidence plus a synchronous SDK queue
fence, or a reviewed SDK task probe proving complete native retirement; both paths
require a matching default-event-loop marker. Queue saturation delays cleanup
without dropping terminal state or reissuing accepted cancellation. A subsequent
send waits for this slot to drain, within its own deadline. Runtime destruction
also waits for this cleanup; if neither terminal nor SDK retirement can be established it may require
explicit `wifi.action.recover` for an admissible readable source, or a **device reboot**
when the driver cannot be safely stopped/reconstructed.
`sys.restartRuntime` is not a recovery guarantee for that state.

## `wifi.action.status()`

Read-only native observations, also available at `wifi.status().radio.action`:

- `operationActive`, `sequence`, `radioGeneration`, `operationId` (identities null when absent).
- `terminal`, `ambiguous`, `nativeQuiescent`, `nativeTerminated`, `identityExhausted`, `cancelWritten`, `sdkFenced`, `eventFenced`.
- `cleanupPending`, `cleanupStage`, `cleanupError`, `submitError`, `cancelError` (absent errors null).

Native operation and cleanup snapshots are individually locked observations; they
are not an atomic transaction across both records. `cleanupPending: false` while
a Future is still active does not mean Radio release. Payloads/MACs are not exposed
by these diagnostics. An exhausted boot identity never wraps or reopens the lane.

## `wifi.action.capabilities()`

Returns `apiVersion: "wifi-action/1"`, `stability: "candidate"`, `target`,
`accessPoint`, `maximumPayloadBytes: 1476`, and `maximumOperations: 1`.
`wifi.capabilities().features.actionTx` describes registered send support;
`remainOnChannel` describes the registered ROC Session support described below.

## `wifi.action.remainOnChannel(options)`

Returns a `WiFiRocSession` after the SDK has accepted the residency request.
Supports `Future.call(wifi.action.remainOnChannel, wifi.action, [options])`.
It does not wait for the residency to end; it may return an already closed Session
if the native operation finished before the Future was polled. Acceptance does
not prove that the requested channel was held for the entire duration.

Options are `interface` (station default or build-enabled access-point), required
`channel`, optional `secondaryChannel` (`none`/`above`/`below`), required
`durationMs` (integer 1–60000), optional `allowBroadcast` (boolean, false default),
and `timeoutMs` (opening deadline 1–60000, default 1000). The native request keeps
`done_cb` unset so that completion reaches the boot-owned default-loop handler.
`allowBroadcast` controls the SDK's native Action receive filter; this API still
does not expose an Action RX stream. APSTA plus allowBroadcast is rejected by
the current Radio policy.

Action send and ROC share the same exclusive native lane and Radio/regulatory
admission rules. A conflicting active operation rejects admission; ROC is not a
queue of future channel reservations. It does not grant a separate Action sender
permission to borrow its lane. No implicit start, mode change, or disconnect.

## `WiFiRocSession.status()`

Returns a read-only `WiFiRocStatus` with:

- `state`: `opening`, `active`, `closing`, `faulted`, or `closed`.
- Requested `interface`, `channel`, `secondaryChannel`, `durationMs`, `allowBroadcast`.
- `driverAccepted`, `closeRequested`, `cleanupPending` (native responsibility is still held).
- Cached `sequence`, `radioGeneration`, `operationId`; absent values are null.
- `terminalStatus`: `completed`, `cancelled`, or null; SDK ROC_FAIL maps to cancelled.
- `ambiguous`, `nativeQuiescent`, `nativeTerminated`, `error`, `stage`, `cleanupError`, `cleanupStage` (absent errors null).

Snapshots are published by the native worker at cooperative service points;
`active` is an accepted operation awaiting retirement, not instantaneous RF proof.
Natural completion automatically proceeds through the two native fences and
releases Radio. The closed JS handle retains its final snapshot. No native timer,
SDK callback, cleanup worker or retained Session stores a JS/runtime/task pointer.

## `WiFiRocSession.wait(options?)`

Waits for native termination **and Radio release**, returning the final status.
`options.timeoutMs` is 1–60000, default 1000. Supports
`Future.call(session.wait, session, [{timeoutMs: 5000}])`.
Timeout or cancellation of this wait stops only that waiter; the ROC residency
continues. A pending wait does not occupy the close Future's resource lane.

## `WiFiRocSession.close(options?)`

Requests cancellation and waits for native termination/fences/Radio release;
returns undefined, including repeat close on a fully retired Session.
`options.timeoutMs` is 1–60000, default 1000; close is also a native Future method.
Timeout/cancellation after close has started keeps the native close request active.
Cancelling a queued close before it starts performs no native close operation.

An opening timeout or abandoned opening result requests close. A JS allocation
failure after SDK acceptance also closes the native Session. GC of a public
Session requests close and releases its JS-owned reference; the native registry
and worker references keep storage alive until cleanup actually completes.
Runtime destruction requests close and waits for all active native ROC ownership.
If terminal delivery is missing or ambiguous, the SDK task may independently prove
native retirement, followed by the same event-loop fence. If neither proof is
available, a device reboot can still be required pending the physical recovery
coordinator; runtime restart is not that recovery path.

All captured, live and retained closed ROC handles share a limit of **8**.
Explicit close does not free a retained JS handle's budget; dropping the handle
and eventual GC releases it. One native residency can be active at a time.
`wifi.action.status()` includes `kind` (`send`/`roc`/null), `rocHandles`, `rocActive`,
`rocError`/`rocStage` and `rocCleanupError`/`rocCleanupStage`; diagnostics remain
available after a public handle was GC'd. `cleanupPending` includes closing ROC
ownership; all individual snapshots remain observations, not a combined transaction.

Opening/operation errors use `WIFI_ROC_FAILED`; operation deadlines use
`WIFI_ROC_TIMEOUT`. `espCode`, `espName`, `stage` and cleanup fields describe the
current native obligation. Current capabilities include `remainOnChannel: true`
and `maximumRocHandles: 8`; the implementation remains Candidate pending phase tests.


The `nativeQuiescent` flag means the reviewed SDK task observed its entire native
Action/ROC record cleared. It does **not** fabricate a terminal event or success
result: `terminalStatus` can remain null, and an already rejected/timed-out Future
stays rejected. A matching late event invalidates this proof and requires another
SDK/event-loop drain. A nonzero native record (including another native owner),
an SDK query failure, or exhausted event revision keeps ownership retained.
The quiescence query does not stop/restart Wi-Fi, disconnect peers, or alter user
configuration; like the existing SDK ioctl fence it uses temporary SDK messaging
and balanced native power-management wake references. C3/S3/C5 support is gated
by exact reviewed archive hashes. Runtime/RF qualification remains pending.

`nativeTerminated` is separate from `nativeQuiescent`: it records successful
physical deinit after native callback unregistration/drain. It never means TX
completion or a completed residency. Pending Action/ROC consumers report native
termination as an error and retire their exact native token without another SDK
query. Results already delivered to callers are unchanged.

Ordinary close does not implicitly stop other Radio owners or invoke physical teardown.

## `wifi.action.recover(options)`

Returns `WiFiActionRecoveryResult`; also supports
`Future.call(wifi.action.recover, wifi.action, [options])`. This explicitly restarts
managed Wi-Fi/AP to recover one exact Action/ROC operation whose native owner is
still retained. It is a **Candidate readable-source recovery**, not an unconditional
reset for arbitrary driver faults.

| Option | Contract |
| --- | --- |
| `sequence` | Required integer 1–4294967295, framework identity from `wifi.action.status()` |
| `radioGeneration` | Required integer 1–4294967295 from the same observed operation |
| `allowDisconnect` | Boolean, default false; authorizes disconnecting the established Station |
| `timeoutMs` | Integer 1–60000, default 10000; applies to this attempt's waiting |

Both identities are captured before admission and checked together against the
current operation under the Radio mutex. A stale pair, already retired operation,
busy native dispatch/cancel, competing lifecycle, pending scan/connect Future,
foreign Radio owner, or unsafe helper state fails without taking another operation's
ownership. The native 8-bit `operationId` is not accepted as a recovery identity.
The explicit recovery call authorizes restarting the managed AP, disconnecting its
clients; it does not authorize shutting down ESP-NOW, Monitor/CSI or Raw TX owners.
An established Station requires `allowDisconnect: true`; success restores its saved
configuration but **does not reconnect Station or restore application sessions**.

Recovery reserves the lifecycle, drains any authorized Station disconnect, freezes
known configuration/credentials and the SDK home channel, stops Wi-Fi, retires old
AP/Station network interfaces, and deinitializes the driver. It retains the original
Action/ROC owner throughout. The original Future/worker consumes physical termination
and retires its token while recovery yields to the scheduler. Only then does recovery
rebuild helpers and replay the same frozen configuration/policies used by driver
restart. Final readback precedes publication of new managed Wi-Fi leases. On C5,
full saved PHY policy is read through the reviewed SDK task boundary without a
capture-time START/band switch.

The result contains `sequence`, `previousRadioGeneration`, and the rebuilt
`radioGeneration`. It does not assert peer delivery, successful residency, IP
readiness or continuity. The original send/ROC may report native termination;
a result/Future already delivered is never rewritten.

`WIFI_ACTION_RECOVERY_FAILED` and `WIFI_ACTION_RECOVERY_TIMEOUT` include `espCode`,
`espName`, `stage`, the selected identity, `lifecycleAdmitted`, `checkpointAttempted`,
`replayAttempted`, `resumeAttempted`, `cleanupPending`, `restartRequired`, and Radio
fault details. No credentials enter these results/errors. `cleanupPending` is a
current coordinator observation; use `lifecycleAdmitted` to determine whether this
attempt acquired it.

Action/ROC, Raw TX and FTM recovery share one recovery Future lane and the same runtime
coordinator; their native drain/retirement guards remain operation-specific.
Queued cancellation has no driver effects. After admission, timeout/cancel ends
this restoration attempt and leaves the exact reservation with central cleanup;
it does not undo a disconnect/STOP/deinit, resume replay, or free the original native
owner. Inspect `wifi.status()` and complete `wifi.stop()` cleanup before starting
again. Cleanup failures preserve the unfinished suffix; `sys.restartRuntime` cannot
repair an unresponsive SDK task or a netif detach requiring device reboot. Native SDK
calls/mutex waits are not preemptible, so the deadline is not a hard execution limit.
A result-conversion allocation failure can occur after successful reconstruction;
read status before retrying any mutation.

The source must still support trustworthy reads of all required saved configuration.
Unknown non-readable policy, invalid hidden PHY state, unrecoverable helper teardown,
or an already faulted/unreadable driver is not covered by this entry. Those broader
recovery sources and native competition/failure/RF tests remain pending.
`wifi.action.capabilities().recover` reports that this binding is registered, not
that the current operation is eligible or that recovery is hardware-qualified.

During runtime destruction, cancellation is requested first; an already admitted
physical recovery cleanup and original-owner retirement continue even while
another Future is pending. Runtime storage remains retained until all drain
conditions are met. This does not initiate a new recovery or resume restoration;
unresponsive native calls and failed helper teardown can still prevent destruction.
