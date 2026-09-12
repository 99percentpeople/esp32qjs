# Wi-Fi TWT

`wifi.twt` is a Candidate module on the reviewed ESP32-C5 HE build. Check
`wifi.twt !== undefined` before use. C3, S3 and Wi-Fi-disabled builds omit it.
The sole contract is `wifi-twt/1`. `capabilities()`, `status()`, `probe(options?)`,
`getConfig()`, `configure(config)`, `getFlowStatus()`, `setTargetWakeTimeOffset(offsetUs)`,
`setupIndividual(options)`, `setupBroadcast(options)`, `agreements()`, `closeAll(options?)`, `recover(options)` and `broadcasts(options?)` are registered. Individual
agreements expose `status()`, `close(options?)`, `suspend(options)` and
`resume(options?)`. Broadcast agreements expose `status()` and `close(options?)`;
probe does not create an agreement.

## Policy, flow state and wake offset

`getConfig()` reads the actual shared native TWT policy as
`{postWakeupEvents:boolean,keepAlive:boolean}`. `configure(config)` replaces both
fields and returns actual readback. Both booleans are required; unknown keys,
missing fields and coercible values are rejected before native dispatch.
`postWakeupEvents` controls native TWT wakeup event posting to the existing
Wi-Fi event path; it does not create a subscription or guarantee queue delivery.
`keepAlive` controls native QoS-null keep-alive behavior, potentially changing
power/traffic behavior for current agreements. It does not establish an agreement.

Policy controls require an initialized stable STA/APSTA Radio. Writes require
idle framework helpers and exact helper owners; existing managed individual or
broadcast TWT owners are allowed. Other Radio owners, pending global operations,
faults and cleanup reject. Managed START re-applies explicitly configured intent;
explicit restart captures actual policy and restores it after START with final
readback. Shutdown and driver-default restore discard the saved intent. Ordinary
configuration reads never present saved intent as native state. These calls do
not initialize, connect, enable modem sleep or change the security configuration.

`getFlowStatus()` returns `{radioGeneration,bitmap}` for the current associated
Station. `bitmap` is an integer 0..255; bit n means native individual flow n is
established. Pending setup is not represented. This reads the actual SDK flow
state, including native flows without a managed handle; it does not confer
ownership, construct Agreements or permit closing an unmanaged flow.
The association and bitmap can change after the read. Generation identifies the
physical driver, not a particular association.

`setTargetWakeTimeOffset(offsetUs)` accepts an integer 0..102400 microseconds and
returns the accepted/read-back number. It controls the offset relative to TBTT
in generated iTWT setup request target-wake fields, including subsequent retry
frames. It requires a currently associated, started Station and the same write
ownership checks. Association validation, native write and readback execute in
one Wi-Fi-task callback, so no queued callback can swap the node between those
steps. It does not reschedule negotiated agreements or attest physical wake
timing. The offset belongs to that native AP node; it is not replayed onto a new
association or after driver restart. Configure it again for the new connection.

These four methods are synchronous controls. The reviewed native-handler
equivalents of the public SDK methods execute through the existing blocking
Wi-Fi task dispatcher, avoiding recursive ioctl entry. Scalar/policy input is
copied; the dispatcher holds its storage until completion or confirmed discard.
`WIFI_TWT_CONTROL_FAILED` details contain `{espCode,espName,stage,mutationAttempted}`.
Native errors are retained. A failed write whose result is uncertain leaves a
Radio fault for explicit cleanup. JS result allocation can fail after a completed
configuration write; inspect `getConfig()` before repeating it. No automatic
reconnection, rollback or retry is performed. C3/S3 continue to omit `wifi.twt`.

`capabilities().flowStatus`, `.targetWakeTimeOffset` and `.configure` describe
these registered controls. They do not raise stability above Candidate; runtime,
RF, concurrency and power-behavior validation are still deferred to the Wi-Fi
test phase.

## Explicit generation recovery

`wifi.twt.recover({ radioGeneration: wifi.status().radio.generation,
closeAll: true, allowDisconnect: true, timeoutMs: 10000 })` is a native-driver
Future method. Both boolean consents must explicitly be true. `radioGeneration`
is required, integer 1..4294967295; timeout is optional, integer 1..60000 ms.
No sequence is accepted: probe, individual and broadcast native sequence domains
are independent, and recovery deliberately selects all managed TWT owners in
one driver generation. At least one such owner must exist when admitted.

Admission requires a healthy initialized/started STA or APSTA Radio whose
configuration can be checkpointed. It validates exact managed TWT leases and
Station/AP helper leases under the mutation lock; unrelated Radio owners,
wake locks, borrowed interval/rate, parked Vendor IE, Enterprise ownership and
conflicting operations prevent admission. It then reserves the lifecycle,
blocks new owners and marks every current TWT owner for cleanup.

Before disconnect, the exact outstanding probe is cancelled while its native
association and PM reference can still be verified. Cancellation is distinct
from resource drain. Station disconnects, current configuration is checkpointed,
Radio stops, and helpers retire. Original TWT owners keep their leases through
TX/PM, timer/native/event fences. The initialized driver remains available for
these native queue calls: deinit is forbidden until all original TWT owners and
information-operation storage are retired. The existing recovery coordinator
then rebuilds the driver and restores its saved configuration and interfaces.
Success returns `{ previousRadioGeneration, radioGeneration }`. It does not
recreate old Agreement handles, renegotiate TWT or prove Station reconnection.
AP clients must reconnect after the radio stop.

Timeout, cancellation or a failed phase retains the central lifecycle cleanup;
new owners remain blocked until it finishes. Cleanup proceeds to shutdown, not
promised configuration replay after the Future has ended. The original probe
Future observes cancellation; late destruction cannot enqueue an already retired
probe over a successor. `WIFI_TWT_RECOVERY_FAILED` / `WIFI_TWT_RECOVERY_TIMEOUT`
include the common recovery stage, native error, generation, checkpoint/replay/
resume attempts, cleanup and restart-required observations; no sequence field.
Inspect `wifi.status().radio`, `wifi.twt.status()` and `wifi.twt.agreements()`.

This Candidate path does not erase sticky native tracking faults, repair an
unknown submit handoff or accept an unreadable/already-faulted driver as a
checkpoint source. Those conditions retain diagnostic ownership and may require
a physical device reboot. A stopped driver alone is not proof of safe TWT
retirement. Other fault sources are rejected by this recovery API; their
retirement and runtime/RF behavior are part of the concentrated Wi-Fi tests.

## Closing all managed agreements

`wifi.twt.closeAll({ timeoutMs: 6000 })` is a native-driver Future method returning
`undefined` after local retirement. `timeoutMs` is optional, integer 1..60000,
default 6000. At Future start, one Radio lock selects all current individual and
broadcast owners and marks them closing, including pending, failed and retained
owners. Later setup admissions are excluded; this is a snapshot, not a gate
against future setup. The probe operation is independent and is not selected.
An empty selection succeeds without initializing Wi-Fi.

Cleanup uses the existing per-owner cancellation/teardown and TX/PM, timer,
native queue and event fences. A shared all-flow information timer can be
cancelled once every included still-retained request has explicitly requested
close. Closing one agreement cannot authorize cancellation for another live
agreement. Unknown/unmanaged requests block cancellation. Close consent is
permanent for that request and does not assert native close or teardown success.

Timeout/cancel ends the wait but leaves every selected owner's cleanup active.
`WIFI_TWT_CLOSE_TIMEOUT` has operation `wifi.twt.closeAll` and details
`{ started, selected, pending }`; counts refer only to this selection. Inspect
`wifi.twt.agreements()` for retained owners' original errors and cleanup stages.
A new owner reusing a freed slot does not extend an old wait. Repeating closeAll
selects the then-current set and never retransmits an already attempted teardown.
This method does not disconnect Station, restart Radio, clear tracking faults or
promise physical fault recovery. Remote AP state is not attested by completion.
`capabilities().closeAll` reports registration; stability remains Candidate.

## Broadcast discovery

`wifi.twt.broadcasts({ timeoutMs: 6000 })` returns
`{ radioGeneration, schedules: WiFiBroadcastTwtSchedule[] }`. It is a native-driver
Future method; direct calls wait cooperatively and `Future.call` supports polling
and cancellation. Only `timeoutMs` is accepted (integer 1..60000, default 6000).

Discovery requires a healthy, already started and associated Station. It copies
the AP's cached broadcast announcements in one native Wi-Fi task call, using a
fixed capacity of 32 records. It sends no probe, establishes no Agreement and
changes no power-save policy. An empty array means no cached announcements at
capture time; it does not prove that the AP lacks broadcast TWT support.

Each entry contains `broadcastId` (0..31), `joined` (native membership at capture
time), `trigger`, `announced`, raw `recommendation` (0..7),
`minimumWakeDuration` (raw eight-bit value), `wakeDurationUnit: null`,
`wakeIntervalMantissa`, `wakeIntervalExponent`, `wakeIntervalUs` and
`persistence` (raw advertised TBTT count). The reviewed SDK getter copies the
duration value but does not return its unit bit, so no duration in microseconds
is invented. Interval is exactly `mantissa * 2^exponent` microseconds and remains
within the exact JavaScript integer range even at the SDK maximum.

Announcements are observations, not managed handles or reservations. `joined`
does not confer a framework owner. `radioGeneration` identifies the driver
generation, not a particular association; the AP can disconnect, replace its
advertisement or change membership after capture. No freshness or RF acceptance
guarantee is implied. Use `setupBroadcast` to request a managed agreement.

Errors are `WIFI_TWT_DISCOVERY_FAILED` and `WIFI_TWT_DISCOVERY_TIMEOUT`, with
`operation: "wifi.twt.broadcasts"` and details `{ espCode, espName, stage }`;
stage is `snapshot` or `deadline`. Timeout/cancel discards the query result without
disconnecting or changing any Agreement. If the worker already started, native
storage remains until it returns, including during runtime teardown. Conversion
failure creates no retained Radio owner or native resource.

## Broadcast agreement

`wifi.twt.setupBroadcast({ broadcastId: 1 })` is a native-driver Future method
returning a `WiFiTwtAgreement`. Direct calls wait cooperatively; use
`Future.call(wifi.twt.setupBroadcast, wifi.twt, [options])` for explicit polling.
Only `broadcastId` (required integer 1..31), `command` (`request`, `suggest` or
`demand`, default `request`), `responseTimeoutMs` (1..65535, default 5000) and
`timeoutMs` (1..60000, default 6000) are accepted. Response timing and the public
deadline are independent; scheduling/lane wait counts toward the latter.

Admission requires a healthy started, associated C5 HE Station with modem sleep
already enabled by the caller and no conflicting Radio owner/mutation. The API
does not enable power saving, associate, change mode or move channel. It holds a
separate lease on the current channel. Individual and broadcast setup share one
Future submission lane. Each broadcast ID admits one managed owner, including
pending, failed and closing owners; a successor is rejected until the old owner
has fully retired. AP rejection, malformed/ambiguous results and native errors
fail the Future rather than constructing an active handle.

The reviewed wire dialog is eight bits. Each broadcast ID has **255 attempts per
physical boot**, never wrapping or reusing a nonzero value. Failed output can
consume an attempt. Closing, reassociation, runtime restart and driver restart
do not replenish it. Exhaustion rejects setup with `ESP_ERR_NO_MEM`; it does not
consume another ID's budget. `capabilities().maximumBroadcastAttemptsPerId`
and `status().dialogAttemptsRemaining` on a broadcast Agreement expose this
limit (closed handles retain their last observed value). Maximum managed
broadcast owners is 31, additionally bounded by shared Radio/native resources.

`agreement.status()` and `wifi.twt.agreements()` discriminate snapshots with
`kind: "broadcast"`. Broadcast fields include `broadcastId`, native request
`sequence`, `radioGeneration`, native status/reason, `nativeClosed`, setup and
teardown errors, observation errors and cleanup diagnostics. Negotiated timing
and trigger fields are null until a valid accepted result; `targetWakeTimeUs`
is an exact decimal string. `wakeDurationUnit` is always null because the SDK
completion omits that bit. Neither discovery nor completion invents that unit.

`close({timeoutMs: 6000})` cancels pending setup or submits teardown once for the
exact established owner, then waits for TX/PM, timers, native queue and event
retirement. This is local native retirement, not proof of remote AP state.
GC and runtime teardown request the same background cleanup. Timeout/cancel of
setup also schedules cleanup after any running worker returns. A public Future
ending does not free native owner storage, release its lease or make its ID
reusable. Queue saturation cannot prevent native completion from being stored.

Submission/completion/cleanup failures preserve the owner and original stage.
Repeated cleanup retries only incomplete local work and does not automatically
retransmit an uncertain teardown or disconnect another owner. A real Station
connection close revokes the old native association; it still must pass local
retirement. Unknown driver handoff remains diagnostically retained and may need
a device reboot; `recover` only supports its documented admission and drain conditions.
Calling `close` again does not promise recovery from that condition.

The shared prototype's `suspend` and `resume` methods reject broadcast handles
before native work; those operations currently apply only to individual TWT.
Errors use `WIFI_TWT_AGREEMENT_FAILED` / `WIFI_TWT_AGREEMENT_TIMEOUT`, with setup
operation `wifi.twt.setupBroadcast` and the same details shape as individual
agreements. These APIs remain Candidate pending concentrated runtime/RF tests.

## Probe

```js
if (wifi.twt !== undefined) {
    var result = wifi.twt.probe({ responseTimeoutMs: 5000, timeoutMs: 6000 });
    print(JSON.stringify(result));
}
```

This example requires an already started and associated Station. The module
uses the current Station channel and a dedicated Radio lease. APSTA may share
that channel; a conflicting owner, active Radio operation/lifecycle or an
in-flight Raw TX prevents admission. It does not start Wi-Fi, associate, change
mode/channel or overwrite the configured power-save policy. The native SDK
temporarily holds a wake reference for its probe.

`probe` is a native-driver Future method. Direct calls wait cooperatively;
`Future.call(wifi.twt.probe, wifi.twt, [options])` exposes the Future for polling
and cancellation. At most one probe owns the native lane. Later calls wait for
both the preceding Future and retained native cleanup; their own deadline
includes that wait. A native submission is never automatically retried.

| Option | Contract |
| --- | --- |
| `responseTimeoutMs` | Integer 1..60000, default 5000. Passed as the SDK response-time estimate in milliseconds. |
| `timeoutMs` | Integer 1..60000, default 6000. Public Future deadline, including scheduling and lane wait. |

Only these option keys are accepted. Invalid input fails before allocation or
Radio mutation. SDK response scheduling uses internal timing conversions; this
API does not promise an exact RF wait interval. The two timeouts are independent.

The result has `sequence` (boot-scoped non-reused native identity),
`radioGeneration`, `status`, `reason` and
`correlation: "associated-ap-liveness"`. `status` is `success`, `failed`,
`timeout` or `disconnected`; the defensive `unknown` value is reserved for an
unrecognized native status. `reason` preserves the SDK byte, normalized to zero
for success. Native TX failure, timeout and disconnection can be returned as
terminal results when no SDK/control error occurred. Inspect `status`.

Success records a Beacon or Probe Response from the currently associated AP.
It does not prove a request-specific RF response, AP TWT acceptance, clock
synchronization, TSF readback or freshness of an individual RF exchange.

## Completion and cleanup

The native result is saved before publishing the optional Wi-Fi watch event.
A full observation queue is recorded as `observationError` and cannot suppress
the Future's terminal result. SDK submission/control failures throw
`WIFI_TWT_PROBE_FAILED`. Public deadline expiry throws `WIFI_TWT_TIMEOUT` with
stage `deadline`; it requests cancellation of this probe and does not initiate
a Station disconnect. A submit racing cancellation may already have reached
the driver. Submitted requests must retire even if the public result allocation
fails or the Future is cancelled.

Both errors have `operation: "wifi.twt.probe"` and details
`{ espCode, espName, stage, nativeStatus, nativeReason }`. Deadline errors do not
read the worker's concurrent result, so their native fields are null. Input
errors and allocation failures retain the normal TypeError/OOM behavior.

Public completion does not mean native resources have been released. Cleanup
retains the exact Radio lease and operation until cancellation, TX ownership,
timer callback exit, native queue ordering and copied-identity event ordering
are confirmed. A failed step retries its unfinished suffix. Cleanup never
reuses a live native identity or releases another client's lease. Runtime
teardown progresses this cleanup and waits while it remains pending. Ambiguous
native ownership stays visible and can require a device reboot; runtime restart
is not a physical recovery guarantee.

## Status and capabilities

`status()` reads native copies without calling the SDK or starting Radio.
Its top-level fields describe the current probe; after successful retirement they reset.
`information` is the current individual suspension operation, or null after its TX cleanup retires.
It is a diagnostic observation, not an atomic cross-module snapshot.

| Fields | Meaning |
| --- | --- |
| `operationActive`, `sequence`, `radioGeneration` | Radio reservation and native identity/generation; numbers are null when unavailable. |
| `dispatching`, `nativeOwned` | Submit in progress and pinned native ownership. |
| `nativeStatus`, `nativeReason` | Saved completion, or null before a terminal observation. |
| `nativeError`, `submitError`, `observationError` | Original numeric errors, or null for no error. Observation failure does not itself fail the probe. |
| `cancelRequested`, `cancelComplete` | Native cancellation request and acknowledgement; acknowledgement alone does not prove retirement. |
| `cleanupPending`, `cleanupError`, `cleanupStage` | Retained cleanup and its most recent unfinished/error stage. A retry may report the SDK's not-finished code. |

`wifi.status().radio.clients.wifiTwt` includes individual/broadcast agreement and retained probe leases and contributes
to `clients.total`; it is present only with this binding.

`capabilities()` returns `{ apiVersion: "wifi-twt/1", stability: "candidate",
target: "esp32c5", probe: true, maximumProbes: 1,
broadcastDiscovery: true, maximumBroadcastSchedules: 32,
setupIndividual: true, closeAll: true, recover: true, maximumIndividualAgreements: 8,
setupBroadcast: true, maximumBroadcastAgreements: 31, maximumBroadcastAttemptsPerId: 255,
suspendIndividual: true, resumeIndividual: true,
maximumInformationOperations: 1, maximumSuspendDurationMs: 4294967,
maximumResponseTimeoutMs: 60000, maximumTimeoutMs: 60000 }`.

Build and static contract checks do not qualify RF behavior. Controlled
competition, GC/OOM, runtime restart and real-device tests remain deferred to
the Wi-Fi stage. Long soak remains after the BLE APIs are complete.


## Individual agreements

```js
if (wifi.twt !== undefined) {
    var agreement = wifi.twt.setupIndividual({
        command: "request", flowId: 0, trigger: true, announced: true,
        wakeDurationUnit: "256us", minimumWakeDuration: 255,
        wakeIntervalMantissa: 512, wakeIntervalExponent: 12,
        responseTimeoutMs: 5000, timeoutMs: 6000
    });
    print(JSON.stringify(agreement.status()));
    agreement.close({ timeoutMs: 6000 });
}
```

`setupIndividual()` is a native-driver Future. Direct calls wait cooperatively;
`Future.call(wifi.twt.setupIndividual, wifi.twt, [options])` exposes cancellation.
It requires an already associated Station with modem sleep explicitly enabled.
A managed setup rejects power-save NONE before SDK mutation because the SDK
would otherwise silently enable it during negotiation. Each request/established agreement
retains its own fixed-channel Radio lease. Up to eight individual owners can
coexist, subject to native capacity, Radio's shared lease limit and admission.
Setup submissions share a Future scheduling lane; established agreements do
not reserve that lane. Shared power-save policy is never silently changed.

| Option | Default and validation |
| --- | --- |
| `command` | `request`; also `suggest`, `demand`. |
| `flowId` | 0; integer 0..7. SDK/AP flow selection may differ. |
| `connectionId` | Automatic; optional integer 0..32767, at least the next unused ID. IDs never repeat within a boot, including failures. Exhaustion fails explicitly. |
| `trigger`, `announced` | Both true; booleans only. |
| `wakeDurationUnit` | `256us`; also `1024us`. |
| `minimumWakeDuration` | 255; integer 1..255 in the selected unit. |
| `wakeIntervalMantissa` | 512; integer 1..65535. |
| `wakeIntervalExponent` | 12; integer 0..31. Interval is mantissa × 2^exponent microseconds. |
| `responseTimeoutMs` | 5000; integer 100..65535, passed to native negotiation. |
| `timeoutMs` | 6000; integer 1..60000, including scheduling and submission. |

Only these keys are accepted. Interval minus wake duration must be at least
10,000 microseconds and at most 2^35 microseconds, matching the reviewed SDK.
Requested configuration, SDK writeback and AP response are kept separately.
The Future returns a `WiFiTwtAgreement` only for an unambiguous accepted setup;
rejects, invalid responses and native submission failures throw
`WIFI_TWT_AGREEMENT_FAILED`. Public deadline throws
`WIFI_TWT_AGREEMENT_TIMEOUT`. Both contain
`{ espCode, espName, stage, nativeStatusId, reason, sdkError, driverError, handoffError, informationSequence, informationError }`; deadline native fields
are null because the worker may still be writing them. `operation` is
`wifi.twt.setupIndividual`, `WiFiTwtAgreement.close`, `WiFiTwtAgreement.suspend` or `WiFiTwtAgreement.resume`.

A timeout/cancel/allocation failure may race successful native establishment.
Cleanup cancels a pending setup or tears down the exact established request;
it never disconnects Station or releases another agreement. One accepted
teardown submission is not blindly repeated after an error. A failed or
ambiguous native retirement retains the owner and can require physical
recovery; runtime restart is not a recovery guarantee.

A native whole-connection close permanently revokes existing individual request
identities, including a reserved setup that has not entered the SDK queue yet.
`nativeClosed: true` records this revoked association authority, not completed
resource retirement. Such a handle reports failed (or closing/closed) rather
than active; a late successful setup event cannot restore it. Setup/TX and
information timer callbacks from that connection are revoked before SDK flow
IDs are cleared, even if a later association reuses the native node address.

After this real connection boundary, local cleanup no longer waits for a lost
or failed teardown success event. It still requires exact native table checks,
TX/recycler/PM quiescence, timer cleanup and native/event fences. The original
`teardownError`, `teardownStatusId` and `teardownObservationError` remain intact;
no successful RF result is invented. Timer stop/delete failures retain the
handle and cleanup suffix. This does not add an implicit disconnect, clear a
tracking fault, reset request IDs or provide complete physical fault recovery.

`WiFiTwtAgreement.close({ timeoutMs? })` is a native-driver Future, with the
same 1..60000 ms range and 6000 ms default. It requests close and waits for
local TX, timers, native/event ordering and lease release. Timeout or cancelling
the close Future does not undo a close already requested. Repeating close is
safe: it drives the remaining cleanup, not a repeated unconfirmed teardown.
Closing an already retired object succeeds. Local retirement is not proof that
the AP received the teardown or that RF qualification passed.

GC requests close without running SDK calls from the finalizer. Native owner
and marker storage survive the JS object; the runtime worker continues cleanup.
Runtime teardown requests close for all individual and broadcast owners and waits for their
retirement. `new WiFiTwtAgreement()` is prohibited.

`wifi.twt.agreements()` returns diagnostic snapshots of current pending, active
and closing owners, including failed/expired Future requests awaiting cleanup.
It is a best-effort enumeration; an owner retired during the snapshot may be
absent. `wifi.twt.status()` describes the probe lane and the current `information` operation.

Agreement `status()` fields are defined by `WiFiTwtAgreementStatus` in the
source types. `state` is pending/active/failed/closing/closed; `flowId`, native
status/reason and decimal-string `targetWakeTimeUs` are null until setup is
observed. Timing/configuration fields use the request before that observation,
then the saved AP response. Errors distinguish submission, raw driver, handoff,
observation and cleanup. Closed objects retain their last observed setup
snapshot and report no pending cleanup. Reading status never drives SDK work.
An overlapping native result release can require retrying the snapshot.

This API remains Candidate. Builds and static links have not established AP
interoperability, RF late-frame exclusion, queue races, GC/OOM or runtime restart
acceptance. Those are part of the deferred Wi-Fi stage.


## Suspend an individual agreement

`agreement.suspend({ durationMs, timeoutMs? })` is a native-driver Future and
returns undefined after the exact local information TX callback has finished
its native suspension side effects. Direct calls wait cooperatively; use
`Future.call(agreement.suspend, agreement, [options])` for explicit Future control.

| Option | Contract |
| --- | --- |
| `durationMs` | Required integer 0..4294967. Zero requests indefinite suspension. A positive value schedules native automatic resume, subject to the negotiated service period and SDK timer conversion. |
| `timeoutMs` | Integer 1..60000, default 6000. Deadline for the local operation, including scheduling; independent of the suspension duration. |

Other keys are rejected. The maximum avoids the pinned SDK producer's 32-bit
milliseconds-to-microseconds overflow. Duration is a request, not an exact
wall-clock sleep guarantee. Zero does **not** mean resume. Use `resume()`
to resume an indefinitely or temporarily suspended agreement, or `close()` to
tear it down. Timed suspension and resume require modem sleep to remain enabled;
they never silently enable it.

Admission requires this exact active, accepted individual Agreement and its
current associated flow. Already suspended or auto-resuming flows, pending
renegotiation, old information timers/TX, closing owners and known timer faults
are rejected before sending. No all-flow operation, channel switch, Station
disconnect or shared power-save policy change is performed. One information
operation is retained globally at a time. A new request may fail busy while a
previous Future has completed but its TX buffer is still retained; it is not
automatically retried.

Timeout/cancel stops waiting. It does not reverse an information frame already
submitted, cancel the native automatic-resume timer, or close the Agreement.
The result's identity and TX storage remain until native callback/recycle
retirement. Close/GC/runtime teardown first retire the outstanding information
TX, then use the Agreement's existing teardown and timer cleanup. A missing
completion cannot authorize buffer reuse or release another owner's lease.

Native results are captured in the exact producer/TX callback scope, not
correlated from an observation event's flow bitmap. A full observation queue
cannot prevent local completion. Success does not prove AP interoperability or
RF acceptance. Errors use `WIFI_TWT_AGREEMENT_FAILED/TIMEOUT`; `stage` is
`suspend` or `deadline`, with `informationSequence` and `informationError` when
available. Deadline details never read a concurrently running worker.

`wifi.twt.status().information` reports `sequence`, `agreementSequence`
(matching Agreement `status().sequence`), `flowId`, requested `durationMs`,
`operation` (suspend/resume), `dispatching`, `txComplete`, `resumeComplete`,
`complete`, `cleanupPending`, and numeric nullable `submitError`,
`nativeError`, `observationError`, `cleanupError`. This describes the operation,
not the current RF power state. It becomes null after retirement, including
while an independent native automatic-resume timer remains alive. Agreement
`state: "active"` means the established Agreement still exists; it does not mean
the flow is currently awake. These diagnostics do not start Wi-Fi or call SDK.


## Resume an individual agreement

`agreement.resume({ timeoutMs? })` sends an Information frame selecting the
earliest wake time permitted by the negotiated service period. It is a
native-driver Future returning undefined only after the matching native timer
has processed the resume, its cleanup succeeded, and the exact flow remains
established with both suspension bitmaps clear. This is local scheduling
completion, not proof that the AP received the frame or that an RF exchange ran.

`timeoutMs` is the only accepted key: integer 1..60000, default 6000. Direct
calls wait cooperatively; `Future.call(agreement.resume, agreement, [options])`
exposes cancellation. Admission requires the exact live Agreement to be
suspended or awaiting automatic resume; an already active, closing, stale or
renegotiating flow is rejected. Suspend and resume share the same single
information lane and native TX retirement rules.

A previous automatic-resume timer is retained while the replacement frame is
in flight. Only successful native TX completion replaces it. Read-only
admission rejects unknown, stale or shared all-flow timers. New timer identity
is bound inside that exact TX callback; a queued old timer or another flow's
callback cannot complete this Future. If timer replacement fails after sending,
the error is preserved and the Agreement remains owned for diagnosis/close;
there is no unconfirmed retransmission or attempt to restore an old RF state.

The new timer can wait until the next negotiated service period, so a long
interval can exceed the public deadline. Timeout/cancel does not undo the
frame or cancel the accepted resume timer. Native storage remains independent
of the Future; close/GC/runtime teardown use the same exact owner cleanup.
Unexpected association change, timer failure or a power-save NONE policy fails
the operation instead of silently changing shared policy. The timer callback
rechecks that policy before entering the SDK's resume helper.

`status().information.operation` distinguishes resume from suspend;
`durationMs: 0` for a resume means zero added scheduling delay, not indefinite
suspension. `txComplete` may be true while `complete` is false. `resumeComplete`
is true only after the matching native resume process and its timer cleanup
succeeded. An intermediate Wi-Fi suspend observation may precede the resume
Future's completion; the operation's terminal result is never inferred from
that observation. Errors use operation `WiFiTwtAgreement.resume`, with stage
`resume` or `deadline` and the existing information diagnostic fields.
