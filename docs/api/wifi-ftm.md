# Wi-Fi FTM ranging

`wifi.ftm` is present when Wi-Fi and SDK FTM are enabled with either initiator
support or responder + SoftAP support. Initiator builds register `start`, `status`,
`recover` and `WiFiFtmSession`; responder + SoftAP builds register the two offset methods.
Check `wifi.ftm.capabilities()` and method presence before use. These are Candidate APIs; compilation is not RF or
lifecycle qualification. No implicit driver initialization/start is performed.

## `wifi.ftm.start(options)`

Waits for native submission and returns a Session. Also supports `Future.call`.
An already-started Station interface is required. APSTA may range on its existing
primary channel. Off-channel requests reject associated Station, AP mode and
incompatible Radio owners; the API does not authorize disconnect or move them.

| Option | Contract |
| --- | --- |
| `peerAddress` | Required nonzero unicast MAC string, exactly six colon-separated hex pairs. |
| `channel` | Required responder primary channel; target channel shape, live regulatory and owner checks apply. |
| `frameCount` | `0`, `16`, `24`, `32`, `64`; default `0` (no preference). |
| `burstPeriodMs` | `0` (no preference), or 200–10000 in multiples of 100; default `0`. |
| `maxReportEntries` | Integer 0–64, default 64. Zero keeps the summary only. |
| `timeoutMs` | Opening deadline, integer 1–60000, default 1000. Does not set the ranging duration. |

Unknown keys, coercible strings, fractional/overflow values, invalid MAC/NUL
suffixes and invalid parameter combinations are rejected before native allocation
or Radio/SDK mutation. The pinned C3/S3/C5 SDK implementation rejects a native
burst period of 1 (100 ms), despite the broader header comment; capture rejects it.

There is one active native FTM operation, at most 8 retained native Session
handles, and at most 256 reserved report entries across handles. Both Session and
report storage are allocated before start. Closed retained handles still count
against the handle limit; their report allocation is released once no worker uses
it. SDK-internal allocations and the boot control timer are separate.

## `session.receive(options?)`

Options contain only `timeoutMs` (1–60000, default 1000). The deadline begins when
the Future is scheduled. Timeout returns `null`; cancelling this wait leaves the
measurement unchanged. A completed report is returned only after native terminal,
timer/SDK/event fences, detailed report copy/discard, and exact Radio retirement.
No terminal event means no guessed successful report.

The returned plain object contains `sequence`, `radioGeneration`, `peerAddress`,
`status`, `statusId`, `rttRawNs`, `rttEstimatedNs`, `distanceCm`, `reportEntries`,
`copiedEntries`, `truncated` and `entries`. Summary RTT/distance are null for a
non-success or zero-measurement report. Status names are `success`, `unsupported`,
`configuration-rejected`, `no-response`, `failed`, `no-valid-measurement`, and
`terminated`; IDs preserve the SDK values 0–6. Native ranging failure is a report
status, distinct from framework/driver operational errors.

Each entry contains `dialogToken`, signed `rssi`, `rttPs`, signed `ppm`, and
`t1Ps`–`t4Ps`. Each timestamp is `{low, high}`, two unsigned 32-bit words containing
exact native picoseconds. These are not wall-clock values or lossy JS numbers.
`truncated` compares the SDK source count with copied entries, including intentional
summary-only capture. No native pointer or C struct padding is exported.

Repeated receive calls create fresh JS objects from the same saved native report;
they never read the SDK report again. A conversion/OOM failure leaves that report
available for retry while the Session stays open. A concurrent close prevents
further report reads and can abort an in-progress conversion. Objects already
returned are ordinary JS data and remain valid after Session close.

## `session.end(options?)` and `session.close(options?)`

Both accept only `timeoutMs` (1–60000, default 1000) and support `Future.call`.
`end` requests native termination, waits for drain and keeps the final report.
`close` immediately forbids further report reads, requests termination and waits
for drain, then returns `undefined`; it is idempotent.

A timeout throws `WIFI_FTM_TIMEOUT`. End timeout/cancellation preserves the end
request and report; close timeout/cancellation preserves close/cleanup. Cancellation
before execution does not begin the operation. An opening timeout/cancellation
closes the unreturned Session. None of these can interrupt an SDK call, undo RF
work, release native references early or guarantee that the native session ended.

SDK errors do not prove that no operation was started. The active registry and
background worker retain independent native references after public Future/GC
completion. Accepted end is not repeated; successful report consumption is not
repeated when a later fence fails. Missing/conflicting native events remain
quarantined, block reuse and may block runtime destruction. Explicit physical
recovery is described below; `sys.restartRuntime()` is not a recovery guarantee.

## Status and capabilities

`session.status()` returns a non-secret snapshot: lifecycle state, requested
parameters, exact framework sequence/generation, native terminal status, report
capacity/count/readiness, end/close intent, fences and error/cleanup stages.
`physicalTermination` distinguishes physical driver cleanup from a native FTM
report. It never implies a successful measurement; a physically retired Session
cannot return a report. `wifi.ftm.recover` coordinates the native physical phases
and original Session worker retirement.
`wifi.ftm.status()` returns individually locked observations of active/worker/
cleanup state, handles/reserved entries and the active Session snapshot (or null).
It also diagnoses an opening failure that has not returned a public handle.
`wifi.status().radio.clients.wifiFtm` counts the native FTM Radio owner.

Operational errors use `WIFI_FTM_FAILED`, or `WIFI_FTM_CLOSED` when receiving from a
closed Session. Error details include `espCode`, `espName`, `stage`, sequence and
Radio generation, end/close intent and pending cleanup diagnostics. Input errors
are TypeError; allocation/conversion can also fail with the normal VM error.

`wifi.ftm.capabilities()` returns `apiVersion: "wifi-ftm/1"`, `stability: "candidate"`,
`target`, `initiator`, `recovery`, `responderConfiguration`, `responderOffset` and operation/handle/report limits.
`responderConfiguration` refers to the existing AP `ftmResponder` configuration;
`responderOffset` reports the two offset methods. Initiator limits are zero when
that role is disabled. `recovery` requires initiator support and a reviewed C3/S3/C5
target; it does not assert that a particular operation is eligible or RF-qualified.

## `wifi.ftm.recover(options)`

Explicitly recovers an outstanding native FTM operation whose ordinary cleanup
cannot complete, including missing or conflicting reports. Supports `Future.call`.
It requires the exact `sequence` and `radioGeneration` from Session/global status;
both are integers 1–4294967295. It accepts only those fields, `allowDisconnect`
(boolean, default false), and `timeoutMs` (integer 1–60000, default 10000).

The source must still be a healthy, started Station/APSTA driver with trustworthy
saved configuration. The operation must have been submitted and no native dispatch
may be in progress. Stale identities, foreign Radio owners, competing lifecycle,
active scan/connect helpers, outstanding borrowed policies, or unsafe teardown
reject admission. An established Station requires `allowDisconnect: true`.
The call authorizes restarting the managed AP and disconnecting its clients;
it does not authorize stopping unrelated ESP-NOW, CSI, Monitor, or Raw TX owners.
Already faulted/unreadable driver sources remain a recovery implementation gap.

Action/ROC, Raw TX and FTM recovery share one Future lane and the existing runtime
coordinator, configuration checkpoint, helper retirement and restoration. The
FTM-native phase performs STOP, drains a fresh timer marker and SDK task barrier,
and deinitializes the driver before marking physical termination. The original
Session worker alone consumes that proof, discards report storage and retires its
exact owner. Recovery yields until this happens, then reconstructs the interfaces
and replays saved configuration/policies. It does not reconnect Station, repeat
ranging or restore application sessions.

The result is `{sequence, previousRadioGeneration, radioGeneration}`. A physically
retired Session cannot provide a report; its `physicalTermination` flag does not
fabricate a native terminal event or successful measurement. A Future/report
already delivered is not rewritten.

`WIFI_FTM_RECOVERY_FAILED` and `WIFI_FTM_RECOVERY_TIMEOUT` identify operation
`wifi.ftm.recover`. Details include `espCode`, `espName`, `stage`, the selected
identity, `lifecycleAdmitted`, `checkpointAttempted`, `replayAttempted`,
`resumeAttempted`, `cleanupPending`, `restartRequired`, `radioFaultStage` and
`radioFaultError`. No credentials are exposed. Admission belongs to this attempt;
cleanup/fault fields are current shared observations.

Cancellation before admission has no driver effect. After admission, timeout or
cancellation ends this restoration attempt and transfers the exact obligation to
central cleanup. It cannot undo disconnect/STOP/deinit, release an original owner
early or automatically resume restoration. Inspect `wifi.status()` and complete
`wifi.stop()` cleanup before starting again. Cleanup retries only unfinished
suffixes. An unresponsive SDK task or failed netif detach may still require a
device reboot. SDK calls are not preemptible; the timeout is a scheduled wait
budget, not a hard execution limit. Result conversion can fail after successful
reconstruction; inspect status before another mutation.

## Responder offset

`wifi.ftm.setResponderOffsetCm(centimeters)` synchronously writes a signed integer
from -32768 to 32767 and returns `WiFiFtmResponderOffsetStatus`. It requires an
initialized, healthy, fully stopped AP/APSTA configuration with zero Radio owners,
no pending operation/lifecycle, wake lock or promiscuous claim. The SDK mode is
checked before writing. This API does not start or enable the AP responder.

`wifi.ftm.responderOffsetStatus()` reads the framework record without calling the
SDK. `valueCm` is null unless the current value is known from an accepted write;
`lastAcceptedCm` and `acceptedRevision` are historical even after physical deinit.
`requestedCm`, `radioGeneration`, boot-monotonic `revision`, `configured`, `known`,
`uncertain` and `error` explain the last attempt. No public SDK getter exists, so
this record is not readback or a measured calibration. An untouched offset is
unknown, not an inferred zero.

A failed SDK call marks the value uncertain and preserves the prior accepted
history. An explicit replacement remains possible under the same admission rules.
`WIFI_FTM_OFFSET_FAILED` details contain raw `espCode`/`espName`, `stage`,
`mutationAttempted` and the offset record for this attempt. Conversion/OOM after
SDK acceptance does not undo the write; inspect status before deciding what to do.
No Future timeout or cancellation is offered for this synchronous write.

Existing supported restart paths capture a known current value and replay it
before AP START in the new physical generation. Unknown/uncertain history or an
exhausted revision space rejects capture; an offset-bearing snapshot requires AP
in the restored mode. Successful replay is not repeated when a later suffix
fails. Physical deinit invalidates live knowledge but retains history. This adds
offset restoration to the existing restart boundary; it does not expand the
remaining unsupported restart source states.

The pinned SDK converts centimeters to its T1 picosecond correction internally.
The API forwards signed centimeters unchanged; it does not replace the SDK's
conversion, assert RF accuracy, or write while a responder can be ranging.

```js
if (wifi.capabilities().features.ftmInitiator) {
    var ranging = wifi.ftm.start({peerAddress: "02:11:22:33:44:55", channel: 6});
    try {
        var report = ranging.receive({timeoutMs: 10000});
        if (report !== null) print(report.status, report.distanceCm);
    } finally {
        ranging.close({timeoutMs: 2000});
    }
}
```

During runtime destruction, cancellation is requested first; an already admitted
physical recovery cleanup and original-owner retirement continue even while
another Future is pending. Runtime storage remains retained until all drain
conditions are met. This does not initiate a new recovery or resume restoration;
unresponsive native calls and failed helper teardown can still prevent destruction.
