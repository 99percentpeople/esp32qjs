# Wi-Fi roaming

`wifi.roaming` is Candidate and exists when the build enables at least one of
`CONFIG_ESP_WIFI_RRM_SUPPORT`, `CONFIG_ESP_WIFI_WNM_SUPPORT`, or
`CONFIG_ESP_WIFI_11R_SUPPORT`. Check both module and method presence.

- `capabilities()` returns `apiVersion: "wifi-roaming/1"`, `stability: "candidate"`,
  target, build flags `rrm11k/btm11v/fastTransition11r`, and `maximumBtmCandidates`
  (16 with WNM, otherwise 0). Build support does not mean the connected AP supports it.
- `isRrmSupported()` is registered with RRM; `isBtmSupported()` with WNM. These
  observe the current AP through the supplicant task, returning false when Wi-Fi
  is normally stopped or the Station is unassociated. They do not initialize or
  start Wi-Fi. Pending lifecycle/operation/helper cleanup rejects the query.
- `sendBtmQuery(options)` is registered with WNM. It synchronously submits a BSS
  transition management query to the current AP. Station configuration must
  already have `btmEnabled: true`; the method does not change configuration.

```js
if (wifi.roaming && wifi.roaming.sendBtmQuery && wifi.roaming.isBtmSupported()) {
  var result = wifi.roaming.sendBtmQuery({ reason: "rssi", candidates: [] });
  console.log(result.accepted, result.completion);
}
```

The single options object accepts only:

| Field | Contract |
| --- | --- |
| reason | Default `"unspecified"`; also `"frame-loss"`, `"delay"`, `"bandwidth"`, `"load-balance"`, `"rssi"`, `"retransmissions"`, `"interference"`, `"gray-zone"`, `"premium-ap"` |
| candidates | Default empty array; at most 16 entries, preserved in caller order |
| allowApChannelChange | Boolean, default false; APSTA must explicitly allow later AP channel movement |

Each candidate contains `bssid` (six-octet colon-separated, distinct nonzero
unicast MAC), `bssidInformation` (uint32), `operatingClass` (integer 1–255),
`channel` (integer 1–233), `phyType` (integer 0–255), and optional `preference`
(integer 0–255). Omitted preference does not add a preference subelement;
explicit 0 remains meaningful. Unknown fields, holes, invalid numbers, duplicate
MACs and strings with extra/NUL suffixes are rejected before submission.

These channel/class values describe remote candidates; supplying them does not
change local regulatory policy or guarantee that the AP can select that channel.
There is no raw candidate string or automatic supplicant scan-cache list. The
internal SDK encoding preserves all 32 information bits despite its signed
32-bit parser. Sixteen candidates encode at most 288 neighbor-report bytes.

Submission requires healthy started Station ownership and idle helpers under the
shared Radio mutation mutex. Existing non-STA/AP/application owners, pending
operations, temporary TX rate, wake locks, promiscuous capture and fixed/conflicted
channel ownership block submission. APSTA additionally requires the explicit
option above; it does not promise AP client continuity if the Station roams.
These are submission-time gates. An accepted query can prompt later autonomous
AP/Station behavior; it cannot reserve or freeze future channel choices, and this
API has no cancellation or timeout option. Existing link/channel events remain
observations, including changes caused by unsolicited BTM requests.

Result: `{ accepted: true, completion: "sdk-submit", reason, candidateCount }`.
It confirms SDK submission only, not RF transmission, AP acceptance, a response,
or successful roaming. Input capture and all success-result allocation happen
before native submission. No JS pointers escape into supplicant storage.

Failures throw `WIFI_ROAMING_FAILED` with `stage`, `espCode/espName`, optional
`radioGeneration`, `nativeEntered`, `submissionAttempted`, and raw signed `sdkCode`
when submission was attempted. The SDK return space includes general failures;
`-1` alone is not proof the AP lacks BTM. Do not retry a dispatched but unconfirmed
query automatically. Configuration copied for the BTM-enabled check is wiped.

## Neighbor Report requests

RRM-enabled builds register `wifi.roaming.requestNeighborReport(options?)` and
`WiFiNeighborReportRequest`. Both opening and `receive()` are native-driver
Future operations; direct calls cooperate with the runtime while waiting.

`wifi.roaming.status()` exposes handle/byte counts, worker/callback activity and
the current native request status, including cleanup after a failed opening
Future. These observations do not reserve the Radio.

Opening accepts `timeoutMs` (1..60000, default 1500) and `maxReportBytes`
(1..4096, default 4096). It requires an associated Station with `rmEnabled:true`,
healthy framework helper/Radio ownership and no conflicting operation. It does
not enable RRM or start/reconfigure Wi-Fi implicitly. The returned handle confirms
SDK submission, not reception of a report or RF delivery.

- `request.status()` returns native terminal, sequence/generation, submission,
  callback, capacity, error and cleanup diagnostics. `transmissionAttempted`
  includes a request followed by an SDK error; it is not RF success.
- `request.receive({timeoutMs?})` uses the same timeout range/default and returns
  a copied `WiFiNeighborReport` only after native retirement. Repeat reads copy
  the retained report again; they never send another request.
- `request.cancel()` synchronously requests cancellation. It does not wait for
  cleanup. It does not replace an already completed terminal result.
- `request.close()` immediately revokes native report access and requests
  cleanup. `cleanupPending` remains true until exact SDK retirement. Previously
  returned JS reports remain usable. Closed handles cannot reopen.

```js
// Connect with rmEnabled:true before requesting an AP's neighbor report.
var request = wifi.roaming.requestNeighborReport({maxReportBytes:4096});
try {
    var report = request.receive({timeoutMs:1500});
    console.log(JSON.stringify(report));
} finally {
    request.close();
}
```

The report contains `sequence`, `radioGeneration`, `dialogToken`, `reportLength`,
`skippedElements`, `neighbors` and `correlation:"sdk-dialog-token"`. Up to 64
neighbors expose the same reviewed fields as `wifi.watch()`: `bssid`, unsigned
`bssidInformation`, `operatingClass`, `channel`, `phyTypeId`, nullable
`candidatePreference` and `skippedSubelements`. Unknown IEs/subelements are
counted, not copied. Malformed lengths or duplicate preference subelements
reject the conversion; an allocation/conversion failure does not resend or
consume the native report. Exceeding the report buffer is an explicit error,
not a successful truncated result.

The SDK matches an 8-bit dialog token. Native sequence identity prevents reuse
of callback storage, but **does not prove RF freshness after dialog-token wrap
or connection reset**. An old on-air report can be ambiguous under that SDK
matching policy. This Candidate API does not claim stronger RF correlation.

A local Future deadline throws `WIFI_NEIGHBOR_TIMEOUT` and requests cancellation
of this request. Cancelling a started opening/receive Future also requests cancellation. Cancelling
a still-queued receive Future drops that waiter without changing native intent;
an abandoned queued opening releases its unstarted request.
Native storage and the Station reservation can outlive the public Future.
A receive Future retains its JS receiver until the driver ends, so dropping
the caller's handle reference does not trigger premature finalizer closure.
`WIFI_NEIGHBOR_FAILED`, `WIFI_NEIGHBOR_CLOSED` and
`WIFI_NEIGHBOR_INVALID_REPORT` distinguish other failures. Details include the
native status and `operationError`. SDK null reports mean `native-no-report`:
the fixed SDK uses the same callback for its 1-second timeout and connection
reset, so this is not reported as a proven local deadline expiration.

One native request may be active. At most 8 native handles and 16384 retained
report bytes exist across active, completed, closed and queued-observation
owners. GC closes/releases the handle; it cannot release an outstanding native
reservation. No raw SDK/JS pointer is stored in the callback context.

`wifi.watch()` receives best-effort Neighbor Report observations after native
retirement and after currently captured opening/receive Futures leave their
waiter records. Publication runs in the runtime poller after Future polling,
not in driver `finish`/`destroy` or the SDK callback. Queue/allocation failure
drops observation without changing completion. Close/cancel/oversized reports
may suppress observation; runtime teardown discards queued observations. A
later `receive()` may read an already observed retained report.

## Roaming observations

`wifi.roaming.watch(options?)` exists whenever the roaming module exists. It
returns `EventQueue<WiFiRoamingEvent>` using the existing Wi-Fi event broker.
It does not initialize/start Wi-Fi, scan, connect, reserve a Radio lease or
change the event mask/RSSI threshold. It survives ordinary driver stop/restart;
close the queue to release its subscription. Runtime teardown closes it.

| Option | Contract |
| --- | --- |
| events | Default `"all"`, meaning the roaming events below; or a nonempty array of distinct listed names |
| capacity | Integer 1..64, default 16 |
| overflow | Only `"drop-newest"` |

The names are `WIFI_EVENT_STA_START`, `WIFI_EVENT_STA_STOP`,
`WIFI_EVENT_STA_CONNECTED`, `WIFI_EVENT_STA_DISCONNECTED`,
`WIFI_EVENT_STA_AUTHMODE_CHANGE`, `WIFI_EVENT_STA_BSS_RSSI_LOW`,
`WIFI_EVENT_STA_BEACON_TIMEOUT`, `WIFI_EVENT_HOME_CHANNEL_CHANGE`, and
`WIFI_EVENT_STA_BEACON_OFFSET_UNSTABLE`. RRM builds additionally include
`WIFI_EVENT_STA_NEIGHBOR_REP`; selecting it without RRM rejects the options.
Unknown fields, raw-export options, duplicate names, holes and NUL suffixes
are rejected before native subscription allocation.

Event names, category, metadata and typed `data` retain the `wifi.watch()`
contract. No raw SDK data is exported. Link/channel observations include
ordinary connects, unsolicited transitions and recovery; they do not identify
which BTM query caused a change or prove BTM acceptance/completed roaming.
The fixed public SDK does not provide those completion events. An event is
not a substitute for a request's own completion/status.

Both watch APIs share four subscriptions, the 16-entry native ingress and the
sole two-slot Neighbor Report pool. `wifi.status().watch` reports the aggregate
broker counts. A subset excluding Neighbor Report does not open that pool.
Retained events can keep the sole retired pool alive and block a new
Neighbor-enabled subscription until released. Queue saturation drops observations
without preventing native terminal updates or Future completion. The request
publication ordering described above also applies to this queue.

```js
if (wifi.roaming) {
    var events = wifi.roaming.watch({
        events: ["WIFI_EVENT_STA_CONNECTED", "WIFI_EVENT_STA_DISCONNECTED"],
        capacity: 8
    });
    // Use the EventQueue receive API while observing the connection.
    events.close();
}
```

11r remains `wifi.connect({ftEnabled:true,...})` / Station config. RF dialog-token
isolation and full Host/VM, target matrix and hardware roaming verification
remain unfinished. Runtime tests are deferred until all Wi-Fi APIs are implemented.
See [request implementation evidence](../investigations/2026-09-10-w08-neighbor-public.md)
and [watch implementation evidence](../investigations/2026-09-10-w08-roaming-watch.md).
