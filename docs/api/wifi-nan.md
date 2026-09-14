# Wi-Fi NAN

`wifi.nan` provides NAN discovery Sessions, service publishing/subscribing and
discovery events. It is registered when Wi-Fi and at least one of
`CONFIG_ESP_WIFI_NAN_SYNC_ENABLE` or `CONFIG_ESP_WIFI_NAN_USD_ENABLE` are enabled;
IPv4 is not required. Among the pinned SDK's C3/S3/C5 targets, only C5 declares
`SOC_WIFI_NAN_SUPPORT` and provides a NAN-Sync driver implementation. Manually
setting macros does not give C3/S3 this capability.
USD can be enabled independently without target support for NAN-Sync. USD-only
builds do not register DataPath classes or `requestDataPath/receiveDataPath` methods;
the corresponding capabilities are false and limits are 0.
This API is currently **Candidate**. Follow-up message sending/receiving, Vendor
attributes, and open and NCS-SK-128 secure data connections are implemented. With
NAN USD enabled, `open({mode:"unsynchronized"})` selects unsynchronized discovery.
Explicit PIN pairing, bootstrap request/receive and cached credential verification
are implemented. Ordinary peer follow-up messages arrive through the service event queue.

## API

| Method | Result and behavior |
| --- | --- |
| `wifi.nan.capabilities()` | Returns `wifi-nan/1`, Candidate status, synchronized/unsynchronized capabilities, exclusive Radio ownership and Session limits |
| `wifi.nan.status()` | Active Session snapshot, live handle count, worker count and runtime closing state |
| `session.getServiceInfo(service)` | Sync native service ID/name/peerCount snapshot; null if not found |
| `session.getPeerInfo(peerMac, service?)` | Sync native peer and service-matched NDP metadata; null if not found |
| `session.getPeerRecords(service)` | Reads service information and up to 15 peers under one lock; null if the service is not found |
| `wifi.nan.open(options?)` | Activates background startup after fully constructing `WiFiNanSession`; does not wait for RF startup |
| `session.status()` | A fresh `WiFiNanStatus` snapshot; remains readable after closing |
| `session.ready(options?)` | Future-capable; waits for native readiness and returns a status snapshot |
| `session.close(options?)` | Future-capable; requests closure, waits for native cleanup and Radio release, then returns `undefined` |
| `session.cancel()` | Requests closure immediately without waiting for native cleanup |
| `session.publish(options)` / `session.subscribe(options)` | Constructs `WiFiNanService` in a ready Session, then submits the native service in the background |
| `service.status()` / `service.ready(options?)` | Current snapshot / Future-capable wait for creation to complete |
| `service.close(options?)` / `service.cancel()` | Waits for actual cancellation and reclamation / requests cancellation immediately |
| `service.requestDataPath(options)` | Initiates a request from a subscription service with data connections enabled; returns an independent connection handle |
| `service.receiveDataPath(options?)` | Receives an inbound connection handle from a publishing service with data connections enabled; null on wait timeout |
| `path.ready(options?)` / `path.respond(options)` | Waits for establishment / explicitly accepts or rejects an inbound request |
| `path.status()` / `path.close(options?)` / `path.cancel()` | Status / waits for closure / requests closure |
| `service.events` | `EventQueue<WiFiNanServiceEvent>` allocated during construction; uses existing `receive/stats/close` methods |
| `service.send(options)` | Future-capable; sends a follow-up to a specified discovered peer and returns `WiFiNanSendResult` |

`WiFiNanSession` cannot be constructed directly. Each native Session uses an identity
that is not reused within a boot. At most one Session may be active, with four live
Sessions. Closed handles still referenced by JS/Futures count toward the limit;
the shared control budget is returned after the last reference is released.
Exceeding the limit or exhausting identities causes failure.

## Sync native cache queries

The three queries above are synchronous reads. They do not initiate scans,
connections, discovery requests or Futures. The current Session must be ready and
must not have requested closure. Exact Radio token checks, native service changes
and parent Session closure are serialized. Closure initiated during a query may
be ordered after the snapshot; a Session whose `cancel()` has already been called
rejects new queries. Runtime teardown, startup failure and retired Sessions also
reject queries.

`service` is an integer ID in 1..255 or a nonempty service name (at most 255 UTF-8
bytes, no NUL). The ID selects a **current native cache record**, not the boot-unique
identity of a `WiFiNanService`; it cannot authorize mutation of an old handle or
prove that a service remains live. `peerMac` is an exactly six-byte ByteSource and
must be a nonzero unicast NMI. `getPeerInfo` accepts an optional `service`; when
omitted, it returns the first match in SDK native service-slot and peer-list order.
Use a service selector or list to distinguish multiple matches.

`getServiceInfo` returns `{serviceId, name, peerCount}`.
`getPeerRecords` additionally returns a `peers` array; the list and count are
captured under the same native data lock. An existing empty service returns
`peerCount:0, peers:[]`; a missing service returns null.
`getPeerInfo` and each entry in `peers` contain:

| Field | Meaning |
| --- | --- |
| `serviceId` / `peerServiceId` | Local and peer native service IDs |
| `peerType` | `"publish"` or `"subscribe"` |
| `peerMac` | Peer NMI as a plain six-byte array |
| `ndpId` | Native NDL record ID with matching service ownership, otherwise null; does not prove successful establishment or IP reachability |
| `peerDataMac` | NDI as a plain six-byte array when a corresponding NDP record exists, otherwise null |

The SDK's single-peer query finds NDL records by NMI alone. Its list query may also
leave the caller's previous NDP fields intact when an NDL belongs to another
service. The framework consistently uses a zero-initialized copy path that checks
service ownership. Subscribers additionally check the existing managed NDP ledger
to prevent cross-use of the same publisher/NMI across local services.
Queries do not expose keys, credentials, native pointers or SSI, and do not consume
discovery events. JS allocation/GC failure does not modify the native cache.
Temporary metadata uses bounded stack space; temporary ByteSource copies use the
shared resource budget. Native lists exceeding 15 entries, inconsistent counts or
invalid records produce errors instead of partial lists.

`capabilities().peerQueries` indicates whether Sync queries are compiled in;
`maxPeerRecordsPerService` is 15 or 0 accordingly. USD-only builds do not register
these methods. In Sync+USD builds, calling them on a USD Session fails with
`WIFI_NAN_FAILED` / `ESP_ERR_NOT_SUPPORTED`; an empty Sync cache is not presented
as a USD query result. Other native failures use the existing `WiFiNanError`,
preserving `operation`, `details.espCode/espName` and Session status. Invalid
arguments produce TypeError.

## Startup options

`open()` accepts only a plain options object with the fields below. Omitted fields
or `undefined` use defaults. Numbers must be finite integers within range; strings,
booleans and fractional values are not implicitly converted. Unknown fields are rejected.

| Field | Units, range and default |
| --- | --- |
| `channel` | SDK channel field, 1..255, default 6; the target driver validates actual NAN channels and regulatory limits |
| `masterPreference` | 0..255, default 2 |
| `scanTimeSeconds` | Seconds, 0..255, default 3 |
| `warmUpSeconds` | Seconds, 0..65535, default 5 |
| `randomizeMac` | Boolean, default true |
| `groupManagementProtection` | Boolean, default false; requires NAN security at build time; a Session-level IGTK/BIGTK policy that also advertises group data protection support for secure services |
| `timeoutMs` | Startup deadline from activation, 1..120000 ms, default 10000 |

Startup requires a healthy, stopped or uninitialized Radio with no Station/AP,
ESP-NOW, CSI, Monitor, wake lock or other native operation owners. NAN does not
implicitly close these resources. `open()` validates arguments and constructs JS
objects before activation; the worker then checks Radio conditions serially.
Native admission or startup failures appear through `ready()` errors and status snapshots.

NAN uses temporary RAM storage. This API exposes no options to erase or persist
NAN credentials. Closing first seals new SD/NDP buffer allocations and service
callback admission. It waits for entered service-match, replied, receive, NDP
application callbacks, native SD service/NAF receive and transmit handlers, null
data frame completions and timer handlers to exit. It then waits for STOP, actual
buffer reclamation, timer cleanup, SDK reset, netif retirement and observer exit,
restores the previous stopped mode/storage and finally releases the exact Radio
lease. Cleanup failures retain the original error, storage and unfinished cleanup
steps. An accepted STOP is not resubmitted because a wait timed out.

`ready` means synchronized discovery has completed native START, default
handler/event fences and the netif-up check. It does not mean a peer was discovered,
a datapath connected or IPv6 communication became possible. `warmUpSeconds` is SDK
protocol configuration and does not extend `open.timeoutMs`.

## USD unsynchronized discovery

With `CONFIG_ESP_WIFI_NAN_USD_ENABLE` enabled,
`open({mode:"unsynchronized", timeoutMs:10000})` is supported without NAN-Sync.
When NAN-Sync is enabled, `mode` defaults to `"synchronized"`; in USD-only builds,
it defaults to `"unsynchronized"`. Explicitly requesting a mode that was not compiled
in fails before native allocation or Radio mutation.
`capabilities().synchronized/unsynchronized` reports build capabilities, while
Session status `mode` reports the actual mode. USD startup accepts only `mode` and
`timeoutMs`, uses the Station MAC, exclusive Radio ownership and temporary RAM
storage, and creates no NAN netif. `wifi.nan.status().tx` describes only the
synchronized buffer pool; it is null for an active USD Session or a USD-only build.
USD send results and storage retirement are recorded by each operation's state and
the shared transport ledger.

USD services use the same `publish/subscribe/ready/events/send/close` methods, with
these additional options:

| Field | Semantics |
| --- | --- |
| `ttlSeconds` | 0..2147483647 seconds, default 60; 0 publishes once or subscribes until the first match |
| `channel` | Default 6; the driver still validates target and regulatory limits |
| `channels` | 1..42 unique channels; 5 GHz requires target support; omitted uses the default channel |
| `dwell` | Publishing only: `nMin/nMax/mMin/mMax`, 1..255, default minimum 5/maximum 10; measured in 100 TU units (1 TU = 1024 microseconds) |

Publish unsolicited/solicited and subscribe active/passive settings are passed to
the native engine. USD does not support this SDK's matching filter, singleEvent,
Vendor or NDP/security/pairing features. Nonempty/enabled options for those features
are rejected without silently falling back to an open connection. The USD-specific
fields above are rejected in synchronized mode.

A send Future completes according to its operation's native completion time; queue
saturation does not lose control completion. Native storage still in use for
transmission or awaiting original buffer reclamation remains retained after
completion. At most one follow-up copy may wait for the old buffer to retire, using
the existing wireless control budget. Stopping a service must retire its own send
records without cancelling other services. Session closure first seals the engine,
then drains Action/ROC, actual buffers and callbacks/timers, followed by STOP and
restoration of the original mode/storage. Completed cleanup steps are not repeated;
failed remaining steps are retained for continued waiting. The new implementation
is Candidate; runtime and RF acceptance have not been performed for this batch.

## Waiting, closing and GC

The only option for `ready()` and `close()` is `timeoutMs`, in 1..120000 ms, default
10000. Direct calls wait for the result; asynchronous composition uses the existing
`Future.call()` mechanism.

| Operation | Actual effect of timeout or cancellation |
| --- | --- |
| `open.timeoutMs` expires | Marks the Session `timedOut`, stops delivering ready and begins native closure; a late successful START still requires closure |
| `ready()` wait expires / its Future is cancelled | Ends only this wait; does not change the Session startup deadline or request discovery to stop |
| `close()` wait expires / its started Future is cancelled | Ends the wait while native cleanup continues; inspect `status()` or call `close()` again to wait |
| `close` Future is cancelled before start | Does not request native closure |
| `session.cancel()` | Requests native closure; subsequent `ready()` fails; use `close()` to wait for retirement |
| Last JS/Future reference is released | The registry requests closure and retains native storage needed by the worker/observer |

Ending a Future may release JS roots, but not Session or Radio bindings still
referenced by native code. OOM during `ready()` result conversion does not consume
the ready state; retain the Session to retry waiting. Successful close can be called
again. Runtime teardown likewise waits for native retirement. A runtime restart
with unknown driver faults or cleanup failures must not be presented as recovery.

## Publishing, subscribing and events

Each Session may retain at most two native services simultaneously. The runtime
allows at most eight service handles, including closed handles still referenced by
JS, Futures or event queues. A service's framework identity is not reused within
a boot. The SDK `serviceId` is for diagnostics and protocol fields only; it cannot
replace the framework identity.

| Creation field | Contract |
| --- | --- |
| `name` | Required nonempty string, at most 255 UTF-8 bytes, no embedded NUL |
| `type` | publish: default `unsolicited`, also `solicited`; subscribe: default `active`, also `passive` |
| `matchingFilter` | SDK comma-separated filter string, at most 255 UTF-8 bytes, default empty, no embedded NUL |
| `singleEvent` | Boolean, default false; uses SDK single match/replied semantics |
| `ssi` | Optional ByteSource, at most 512 bytes; copied before startup, so the input may be released after the call |
| `timeoutMs` | Creation deadline, 1..120000 ms, default 10000; expiry requests cancellation, and late success still requires reclamation |
| `dataPath` | Boolean, default false; enables data connections, with an explicit response required for inbound requests within 10 seconds |
| `security` | Optional NCS-SK-128 credential object; omitting both security and pairing creates an open service; enabling security never downgrades to an open connection |
| `vendor` | Optional `{oui, body}`; OUI must be 3 bytes, body 0..255 bytes, both ByteSource |
| `queueCapacity` | 1..16, default 4; drops the newest observation when full; service control and closing do not depend on successful enqueueing |

By default a service provides discovery only and grants no data-path admission
permission. Only explicit `dataPath:true` permits requesting or receiving inbound
connections. An active service without this setting rejects incoming NDP requests.
If rejection submission fails, it retains the original error and closes the parent
Session; it never falls back to automatic acceptance. Pairing follows the explicit
PIN and cached verification contracts below. USD does not accept synchronized-mode
fields. A service frozen during closing no longer handles new NDP indications;
peers must not treat no response as acceptance.

After successful driver creation, the Wi-Fi task binds the SDK host service ID to
the framework identity before returning to the request worker. The event queue is
created before activation, so events arriving before `publish/subscribe` returns
can be retained. `service.events.receive()` returns `match`, `replied` or `message`,
with framework identity, per-service sequence, both service IDs, a six-byte
`peerMac` and a copied `ssi` array. Received SSI is limited to 2048 bytes. Match
additionally includes SSI version and datapath/security/FSD/GAS/NDPE fields; these
fields are null for other events. Receiving a match does not prove authentication
or data-path success. `events.stats().dropped` counts queue drops.
`service.status().droppedBeforeQueue` counts SSI-length, concurrency and observation
storage rejections before enqueueing when the owning service can be identified.
Truncated event headers that cannot identify a service are not charged to an
individual service. Closing the event queue stops observation without cancelling
the service.

Service ready/close wait options and Future cancellation rules match those of the
Session. A ready timeout ends only the wait; the independent creation deadline
remains active. After successful service cancellation, the native ID and record
remain retained until complete callbacks, SDK cancellation and the actual TX
recycler return have all finished. Completed cancellation is not resubmitted.
New service activation is rejected during this interval to prevent native reuse
of the old ID. If local reclamation cannot be proven after a creation failure, the
parent Session closes, retaining native resources until overall STOP/reclamation
completes. Parent Session closure also closes its services; a service JS finalizer
requests cancellation. Retired service handles and event queues retain only their
own snapshots and no longer extend the parent Session's Radio ownership.

## Service security and Vendor attributes

`security.credentials` must be an array of 1..4 entries. With pairing enabled, the
maximum is 3; the fourth native slot is reserved for the PASN cipher advertisement.
Each entry's `cipher` is omitted or `"ncs-sk-128"`, and must provide exactly one of
`passphrase` or `pmk`. A passphrase is a string of 8..63 UTF-8 bytes without embedded
NUL; a PMK is an exactly 32-byte ByteSource. Empty lists, other ciphers, both secrets
together, unknown fields and non-integer bytes are rejected before driver
submission. The pinned SDK's 256-bit enum is not a usable encryption implementation
and is not advertised as a capability.

Check `wifi.nan.capabilities().security` and `securityReason` before use. Builds
require `CONFIG_ESP_WIFI_NAN_SECURITY` and its SDK dependencies; service creation
fails explicitly when disabled. DataPaths for secure services must retain
`confirmRequired:true`; disabling confirmation cannot bypass the complete handshake.
Protocol authentication, MIC and key installation use the SDK; `ready` is delivered
only after they succeed.

`security.groupDataProtection` defaults to false and advertises negotiable group
data protection capability. Group-key installation also depends on peer capability.
The Session setting `groupManagementProtection:true` advertises both group management
and group data protection for secure services. These two status fields report the
advertised policy, not proof that a peer has negotiated group keys.

Arguments are copied using the shared control budget; inputs may be released or
modified after the call. Service closure waits for native retirement, then zeroes
framework credentials even if a closed JS handle remains live. Cleanup failure
retains storage required by native code. Construction failure and final reference
release also zero copied material. Status exposes only whether security is enabled,
credential count, group protection policy and Vendor body length; it never exposes
passphrases, PMKs or derived keys. The SDK build copy disables binary security
material logs. MAC/cryptographic derivation failure cannot produce a success result.
Unmatched inbound PMKIDs or missing M1 cause explicit rejection; temporary parsed
material is cleared before and after every complete NAF receive operation.
Group management key generation/installation failure prevents Session readiness.
Missing negotiated group keys, wrapping failure or installation failure prevents
connection readiness and initiates closing with the error retained, without falling
back to success with only unicast protection.

`vendor` is supported by publish, subscribe and synchronized-mode `service.send()`,
adding attributes to the corresponding transmitted frames. OUI and body are copied
before submission and retained with the native send/service owner until retirement.
Vendor bodies do not appear in status; Vendor receive events not provided by the
SDK are not promised. Vendor attributes are independent of SSI.

The pinned SDK already includes NAN pairing/PASN and requires no separate Wi-Fi
Aware component. Explicit PIN pairing, bootstrap exchange and cached verification
use the Service/Pairing APIs below. Runtime/RF acceptance remains pending, and no
credential persistence or erasure options are exposed.
`session.status().groupManagementProtection` retains this Session's group management
protection policy for services created later, even after temporary startup
configuration is zeroed. USD uses the same Session/Service interface through the
mode described above, with separate native startup/closing paths; it does not
support the security/Vendor options in this section.

## Explicit PIN pairing

Requires Wi-Fi, NAN Sync, security and `CONFIG_ESP_WIFI_NAN_PAIRING`; currently
Candidate. `capabilities().pinPairing` indicates whether this contract is compiled
in. When pairing is disabled, `preparePairing/requestPairing/receivePairing/pairingCredentials`
and `WiFiNanPairing` are not registered. USD does not support this configuration.

Both publisher and subscriber must explicitly specify `pairing: true`, enabling
setup, cached verification and an in-RAM NIK/NPK cache. The framework advertises
`securityRequired` and appends a native NCS-PK-PASN-128 cipher descriptor; no
placeholder static password or PMK is needed. Up to three NCS-SK-128 static
credentials may also be supplied. `credentialCount` counts only these explicit
static credentials; `pairingEnabled` records the pairing policy. An existing cache
does not silently disable setup, and services do not automatically accept
authentication frames. Publishers advertise PIN display; subscribers advertise PIN keypad.

| Method | Behavior |
| --- | --- |
| `service.preparePairing({peerServiceId, peerMac, timeoutMs?})` | Returns an unconfirmed independent handle; the subscriber is the initiator and the publisher the responder. Requires a ready service, the exact discovered peer ID and a nonzero unicast six-byte NMI |
| `pairing.confirm({accept:true, pin})` | Accepts only a six-character ASCII digit string, preserving leading zeros; copies and queues submission without waiting for pairing to complete |
| `pairing.confirm({accept:false})` | Rejects this operation, forbids a PIN and does not start authentication |
| `pairing.status()` | Returns this operation's native state and cleanup snapshot without PIN, NIK, NPK or ND-PMK |
| `pairing.ready(options?)` | Future-capable; waits for protocol pairing and required subsequent frames to complete/be reclaimed, then returns status |
| `pairing.close(options?)` | Future-capable; success means this operation's remaining native work and actual transmit buffers have retired |
| `pairing.cancel()` | Requests closure and returns immediately |

The preparation `timeoutMs` covers the entire confirmation and pairing deadline:
1..120000 ms, default 30000. If no one confirms, expiry closes the operation; there
is no default PIN. `confirm` is a one-time decision, submitted only after full
argument validation. Repeated confirmation or confirmation after closing fails.
Applications should call accept only after obtaining actual user confirmation.
This low-level API sends no bootstrap negotiation frames: the application
coordinates both sides, confirms the responder first, checks its `nativeActive`,
then confirms the initiator. Both sides use the same user-confirmed PIN. The native
responder also has an SDK establishment timeout of 10 seconds; the overall
timeoutMs does not extend it.

`ready/close` wait timeouts default to 10000 ms and accept 1..120000 ms. Cancellation
or timeout ends only that wait. `ready` does not undo submitted confirmation, and
the independent operation deadline remains active. An already started `close`
continues cleanup. Use cancel/close to terminate authentication. Ending a wait may
release JS roots; the Session registry retains state still referenced by native code.

At most one pairing may be active, with eight live handles. Releasing an old handle
must not affect a later operation. After successful pairing, call close to release
the lane before establishing a data connection to that peer. Successfully derived
keys are cached per service; closing a service clears its own derived records.
Pairing failure or cancellation clears derived cache entries from the started but
unsuccessful operation. Replacing pairing keys is rejected while the same peer has
an unretired NDP.

`authenticated` and `paired` are native milestones. `trafficPending` means required
transmission remains incomplete; `paired` alone is not `ready`. After receiving the
peer's NIK, native code may still need to respond with its own NIK. Readiness waits
for the actual TX result and reclamation of this response, preserving the original
error on failure. A service closes its own pairing first. Parent Session closure
revokes admission, continues physical STOP and then drains all sends, avoiding
mutual waits before STOP. `nativeRetired` alone describes only the PASN context;
successful close proves complete cleanup of this operation. Global status
`pairingHandles/activePairings` counts live handles and active lanes respectively.

Race, GC/OOM, two-peer interoperability and RF acceptance remain deferred; see the
implementation record for the exact build coverage.

### PIN bootstrap requests and reception

`capabilities().pinBootstrap` indicates whether this flow is compiled in. Both
services must enable `pairing:true`.

| Method | Behavior |
| --- | --- |
| `subscriber.requestPairing({peerServiceId, peerMac, timeoutMs?})` | Returns an unconfirmed Pairing and queues a PIN bootstrap request; argument ranges match preparePairing, and credentialId is not accepted |
| `publisher.receivePairing(options?)` | Future-capable; delivers one unconfirmed Pairing; returns null on wait timeout, default wait 10000 ms, range 1..120000 ms |

After receiving a request, the publisher obtains actual user confirmation and calls
`confirm({accept:true,pin})`. It sends an acceptance response only after the native
responder starts. The subscriber must also obtain user confirmation and enter the
same PIN. The initiator starts only after local confirmation, peer acceptance and
request-frame reclamation are all complete. `peerAccepted` means only that a
negotiation response was received; it does not replace user confirmation,
authentication or `ready`. There is no default PIN or unconfirmed opportunistic pairing.

An inbound operation has a fixed 30000 ms deadline measured from request arrival.
The receive wait deadline is independent; claiming a request does not extend its
lifetime. Unconfirmed operations close on expiry. A rejection response is sent on
a best-effort basis when possible; it may be dropped directly if the send lane is
occupied or the parent is closing. After publisher confirmation, the native
10-second establishment timeout still applies, so the subscriber should confirm
promptly. Explicit rejection, cancellation or timeout does not authorize other connections.

A Session retains only one unclaimed request, sharing the admission limit with its
single active pairing. New requests are dropped while busy.
`maxPendingPairingRequests` is 1. Service status `pairingRequestPending` and the
saturating counter `pairingRequestsDropped` expose undelivered and dropped requests.
Reception does not consume ordinary events queue capacity; a full queue cannot
block confirmation, closure or native completion. JS result construction failure
or cancellation of the receive wait releases the claim reservation; an unexpired
native request may be claimed again. The handle is transferred only after successful
result construction.

The SDK bootstrap receive callback has no operation cookie. Each Service records
at most eight distinct `(peerServiceId, peerMac)` pairs, reserved when an operation
activates. The same pair is never reused for bootstrap during that Service's
lifetime; a new pair fails explicitly once capacity is exhausted.
`maxBootstrapPeersPerService` is 8. Closing and recreating the service begins a new
service lifetime, but does not prove old wireless frames have disappeared. NPBA
itself is unauthenticated; explicit local consent and fresh PASN authentication
remain required. A COMEBACK response ends the operation as not-supported because
the current SDK callback lacks sufficient information to resume its cookie/delay flow.

Pairing status adds `bootstrap/incoming/delivered`, `bootstrapAttempted/bootstrapSent/
bootstrapTxPending` and `peerResponded/peerAccepted`. Send success and buffer
reclamation are tracked separately. `ready` waits for negotiation frames and
subsequent authentication frames to complete; `close` waits for actual reclamation
of this operation's resources. Wait timeouts, late responses and JS handle release
cannot permit early reuse of send storage still referenced by native code.

### Cached credentials and verification

`capabilities().cachedVerification` indicates whether this flow is compiled in.
`maxCachedPairings` is 2, shared across the entire NAN Session. When pairing is
disabled, `pairingCredentials` is not registered.

- `service.pairingCredentials(options?)` is Future-capable and returns an array of
  completed, unexpired credentials with the same service hash. Each entry contains
  only `credentialId`, the `peerMac` from the last successful authentication and
  `expiresAtUs` on the device's monotonic clock (null for no finite lifetime). It
  never returns NIK/NPK/ND-PMK. An empty array means no credentials are available.
  Wait timeout throws a timeout error; cancellation or timeout ends only this read wait.
- `preparePairing({peerServiceId, peerMac, credentialId, timeoutMs?})` creates an
  unconfirmed verification operation. `credentialId` must come from the local cache.
  The MAC may be a randomized address obtained through rediscovery; the native
  phase must still pass NIRA and NPK/PASN checks using the selected NIK. Status
  `mode` is `verification`; omitting credentialId selects `pin`.
- Verification uses `confirm({accept:true})` and forbids a PIN; PIN mode still
  requires a six-digit string. Both modes allow explicit rejection and neither
  accepts by default. The application confirms the responder and observes
  nativeActive before confirming the initiator. Verification continues to use
  manually coordinated preparePairing; credentialId is not used for PIN bootstrap.
- Cache entries are committed only after protocol completion, actual TX success
  and buffer reclamation. Intermediate failure, expiry or send failure does not
  overwrite previously valid credentials. On success, `ready()` returns the
  committed credentialId. Key refresh assigns a new ID; the old ID is rejected
  before native submission. IDs do not wrap and exhaustion fails explicitly.
- Verification does not extend the original NIK lifetime. Expired entries cannot
  be read, recognized or used for authentication; a new pairing may reclaim their
  slots. If other credentials occupy both valid slots, the operation fails
  explicitly without automatic eviction. Credentials are valid only in the current
  Session's RAM. After service closure, a service with the same name in the same
  Session can read them again; closing the Session invalidates all of them.

These APIs are connected to production code. Consolidated runtime/GC/OOM/race,
hardware and RF validation remains scheduled under the Wi-Fi phase plan.

## Sending messages

`service.send({peerServiceId, peerMac, ssi?, vendor?, timeoutMs?})` requires a ready
service. Synchronized mode also requires an SDK record for that peer's service.
USD uses the caller-supplied peer address/ID and rejects vendor. `peerServiceId` is
the peer ID from a discovery event, in 1..255. `peerMac` is an exactly six-byte
ByteSource and must be a nonzero unicast NAN interface MAC; zero-valued wildcard
selection is unsupported. `ssi` is an optional ByteSource, 0..2048 bytes, default
empty. It is copied during Future capture and may subsequently be modified or
released. The `vendor` contract is described above; unknown fields are rejected.
`timeoutMs` is 1..120000 ms, default 10000.

Each Session admits only one native message at a time. The entire runtime may
retain at most eight captured/send records. Identities are not reused within a
boot; the SDK context is a reusable buffer address and is not a public identity.
The actual follow-up allocation is associated with an exact identity/ticket.
Completion state is recorded after the full native TX callback exits; buffer
ownership is returned only after the actual recycler returns. A successful operation
result has `txSubmitted` set to true. `completion.status` is success / failed /
unknown, while `bufferRetired` may still be false. A failed MAC completion also
returns a normal result. Synchronized NAN observes only boolean completion, so
`completion.native` is null; USD preserves the code/name from
`wifi_action_tx_status_type_t`. In error snapshots, completion is null if no TX
completion was observed. Native codes for send operation errors appear in
`details.native`, with domain/code/name. See the shared [TX completion contract](tx-completion.md).
TX success does not mean the peer application processed the message. No automatic
retransmission or deduplication is provided. `submitStarted` means the worker entered
the submission phase; `txSubmitted` means the SDK returned success. The latter may
still be false if a wait expires during submission. Completion time is recorded
when the native callback exits. Public waits read completion state directly and
do not depend on the background cleanup worker's retry interval.

| Situation | Behavior |
| --- | --- |
| Future cancelled before start | Does not submit a native send; releases captured data |
| Timeout/cancellation after enqueueing but before native submission starts | Marks the operation finished; the worker discards the message |
| Timeout/cancellation after submission | Ends the public wait without withdrawing transmitted or pending frames; retains the native record until actual reclamation |
| TX completed but not yet reclaimed | May return success; continues retaining the native record and rejects the next send |
| Service closes | Cancels the public send wait and cleans up the service; still waits for its native frames to retire |
| Session closes / runtime teardown | Releases send and service records only after STOP, complete callbacks and actual buffer reclamation |

`service.status()` fields `sendIdentity`, `sendPending` and `sendCleanupPending`
distinguish pending sends from retained cleanup. `wifi.nan.status().messageHandles`
includes historical results retained by Futures. An unreclaimed native send is not
retried because its wait timed out. Close the parent Session to terminate the
entire discovery operation. OOM during result conversion likewise does not resubmit
a completed send. Error details retain send identity, TX/reclamation flags and the
original error without exposing SSI content. Data connections use the service's
security policy; pairing remains pending implementation.

## Status and errors

State is `opening`, `ready`, `closing` or `closed`. `identity` identifies this
Session. `operation`/`radioGeneration` is the most recently completed native
snapshot and remains available for historical diagnostics after closing. While
the worker is executing inside the SDK, the native phase snapshot may still be the
previous one; interpret it together with `workerBusy`.

`error`/`stage` retains the primary startup or native runtime error.
`cleanupError`/`cleanupStage` reflects the most recent cleanup or worker enqueue
failure. `startAttempted`, `startAccepted`, `stopped`, `nativeReset`, `netifRetired`,
`observerRetired`, `modeRestored` and `storageRestored` distinguish progress through
individual stages. A flag may remain false if its stage was never entered, even
when the Session has no resources left to clean up. `reservedBytes` counts the
Session and its retained Radio control storage, excluding SDK heap and separately
accounted Future storage. See the source type `WiFiNanStatus` for all fields.

`wifi.nan.status().tx` returns the current SD/NDP buffer tracking pool's `capacity`,
`tracked`, `unidentified`, `submissions`, `rejected`, `reservedBytes`, `closing`,
`identityExhausted` and `error`. The fixed limit is 32 buffer tracking slots; this
is neither a service count nor SDK queue capacity. It tracks management-frame
allocations for native service-discovery and datapath objects, including NDP
default rejection, request, response, confirmation, security installation and
termination frames, as well as ordinary/null data frames entering the native data
queue. The SDK retains ownership of its original frames. Data frames may occupy
at most 24 slots, with 8 reserved for management frames. Beacons are not counted.
The tracking pool is allocated in internal memory from the shared control budget
at NAN startup. Slots are released only after the native recycler returns; the
pool itself can be released only after all buffers, timer handles and entered
callbacks retire. `reservedBytes` also includes operation and timer control records
in the pool, excluding internal SDK timer heap allocations. Address reuse cannot
replace an exact ticket.
`unidentified` includes frames that have not finished construction or reached TX
submission, and multicast data without a single connection ID. Management frames
whose service identity is unconfirmed conservatively delay service ID reuse.
Multicast data belongs to the Session and does not prevent retirement of an
individual connection or service whose native reclamation has completed. Tickets
do not wrap within a boot; exhaustion rejects new buffers.

Global `serviceCallbacks` counts service discovery/receive, NDP application callbacks,
the service-handling scope of native SD send callbacks, complete NDP management
frame receive/transmit, null data frame TX completion and native timer handling,
including peer writes/retries after application callbacks return. Nested calls are
counted separately. Freezing one service still permits in-flight NDP completion.
Global closing seals NDP callback admission and waits for entered callbacks to exit
before STOP. Independent follow-up completion notifications remain enabled.
Global Session, TX and callback fields are sampled sequentially, not as an atomic
snapshot across tasks.

NDP establishment/idle timers carry only non-reused numeric identities through two
asynchronous queues. After deactivation, old notifications no longer gain access
to native objects. Stop/delete failures retain actual handles; closing retries only
unfinished steps. Session service processing reads timer or native pool faults and
initiates closure without depending on observation event enqueueing. Individual
connection reclamation verifies deletion, buffer/callback retirement and exact
timer cleanup in the native Wi-Fi task, retaining the record on failure.

## Data connections

Explicitly set `dataPath: true` when publishing or subscribing. Default discovery
services continue to reject data connections. A data connection uses its owning
Service's security credentials and policy; per-connection injection of different
keys is unsupported. Without configured security, the connection is open. Pairing
integration remains pending; disabling confirmation or omitting key matching cannot
lower a secure service's requirements. Each Session may occupy at most two
connection slots, including connections awaiting a response, establishing or
cleaning up. The entire runtime may retain at most eight connection control objects,
including closed JS/Future handles that have not been released.

A subscription service calls
`requestDataPath({ peerServiceId, peerMac, confirmRequired?, timeoutMs? })`.
The peer ID is the publish ID from a discovery event, in 1..255. The MAC is a
six-byte ByteSource and must be nonzero unicast. `confirmRequired` defaults to true.
`timeoutMs` is the establishment deadline, default 10000, range 1..120000 ms.
Returning a handle does not yet guarantee native submission or successful
connection; use `path.ready()` to wait for confirmation. The SDK still checks the
matching publish record actually discovered by that subscription service.

A publishing service receives an inbound handle with
`receiveDataPath({ timeoutMs? })`. It supports `Future.call` and returns null on
wait timeout. Requests are first retained in bounded native records; the shared
worker then creates control objects, independently of `service.events` or the
default observation queue. Conversion failure or wait cancellation releases this
receive reservation so a later call can receive the request again. A request
successfully transferred once cannot be claimed by a second Future.

An inbound handle calls `respond({ accept: true|false, ssi?, timeoutMs? })`.
`accept` must be an explicit boolean; SSI is limited to 512 bytes. Each connection
accepts only one response. Acceptance waits for native confirmation; rejection
waits for this connection's cleanup before returning status. Requests that are not
received, remain unanswered or exceed 10 seconds from native claim are rejected
by default. Native protocol timers may terminate them earlier. Insufficient shared
capacity or control-object allocation failure likewise causes rejection; cleanup
responsibility is not placed in a queue that can drop records.

`ready/respond/close` support `Future.call`. Wait timeout/cancellation ends only
that wait; submitted operations and the connection's own deadline remain active.
Call `cancel()` or `close()` to end the connection. Successful `close()` means native
deletion, complete send callbacks, actual buffers, timers and host records have
all retired. Construction failure occurs before activation; finalizers request
closure while the native registry independently retains references. A service
closes its own connections first; parent Session closure and runtime teardown take
over all connections. Native termination failure or unproven local cleanup closes
the parent Session, affecting other connections in that Session. Original errors
and closure progress remain in both connection and Session status.

`status()` returns an independent identity, owning Session/service, native ID,
direction, state, NMI/NDI, IPv6 identifier, most recently captured SSI,
submission/termination/reclamation flags and primary/cleanup errors. `connected`
means native connection confirmation was received; it does not prove peer IPv6
traffic or application protocol validation. Global `wifi.nan.status()` fields
`dataPathHandles/activeDataPaths` count retained objects and active slots
respectively. See the source type `WiFiNanDataPathStatus` for all fields.

The `details` of `WIFI_NAN_FAILED`, `WIFI_NAN_TIMEOUT` and `WIFI_NAN_CLOSED` errors
include status and the original `espCode`, `espName` and `waitTimedOut`. `timedOut`
and `waitTimedOut` refer to the startup deadline and an individual wait respectively;
they are not interchangeable. Invalid arguments produce TypeError; JS allocation
failure preserves VM OOM. Status, error and capability results contain no credentials.

## Example

Run on a stopped Radio with no other owners:

```js
var discovery = wifi.nan.open({ channel: 6, timeoutMs: 10000 });
try {
    var state = discovery.ready({ timeoutMs: 10000 });
    print(state.state);
} finally {
    discovery.close({ timeoutMs: 10000 });
}
```

Consolidated VM/scheduling/GC/OOM checks, the full target build matrix and hardware
RF acceptance for this API have not yet been performed. Services, messages and
DataPaths are connected to their respective identity and callback/storage retirement
paths, and the affected C5 production units have passed compilation and partial
linking. These results do not prove runtime behavior, peer interoperability or IPv6
data communication. Native observer exit also does not mean all TX buffers have
been reclaimed; closing barriers still require separate verification.
