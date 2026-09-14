# Wi-Fi Mesh

`wifi.mesh` uses the sole `wifi-mesh/1` contract and is currently Candidate.
Registration requires Wi-Fi, target `SOC_WIFI_MESH_SUPPORT`, SoftAP, BSD TCP/IP and
IPv4, with a local C3/C5/S3 Wi-Fi driver supported by the pinned SDK. It is not
registered for ESP Host Wi-Fi. C5 dual-band capability does not imply 5 GHz Mesh
support; this API uses the SDK's 2.4 GHz Mesh channel range.

## API

| Method | Behavior |
| --- | --- |
| `wifi.mesh.capabilities()` | Version, status, exclusive Radio ownership and fixed resource limits |
| `wifi.mesh.open(options)` | Activates background startup after fully constructing the Session; returns `WiFiMeshSession` |
| `session.status()` | A fresh snapshot without secrets; remains readable after closing |
| `session.ready(options?)` | Future-capable; waits for native startup and returns status |
| `session.close(options?)` | Future-capable; requests closure and waits for native retirement, restoration of the original Radio configuration and owner release |
| `session.cancel()` | Requests closure immediately and returns `undefined` without waiting for native cleanup |
| `session.recover(options?)` | Future-capable; explicitly requests one retry after a known recovery failure, without recreating retired Mesh resources |
| `session.send(options)` | Future-capable; sends data and returns an independent command identity and the SDK submission result |
| `session.receive(options?)` | Future-capable; returns a message with an independent ByteView, or `null` on wait timeout |
| `session.watch({capacity?})` | Creates the sole observation queue; returns `EventQueue<WiFiMeshEvent>` |
| `session.routingTable(options?)` / `groups(options?)` | Future-capable; returns copied arrays of six-byte addresses |
| `session.addGroups({addresses,timeoutMs?})` / `removeGroups(...)` | Future-capable; adds/removes group addresses |
| `session.setToDSState({reachable,timeoutMs?})` | Future-capable; requires the actual root role at native execution time |
| `session.connect(options?)` / `disconnect(options?)` | Future-capable; requests Mesh connection/disconnection while retaining the Session |
| `session.flushUpstream(options?)` | Future-capable; explicitly discards the native pending upstream send queue |
| `session.setParent(options)` | Future-capable; selects a router or Mesh parent with explicit configuration after disabling self-organization |
| `session.scan(options?)` | Future-capable; performs a manual scan after disabling self-organization and returns scan status with an independent identity |
| `session.receiveScan({scanId,timeoutMs?})` | Future-capable; reads one AP/Mesh IE record, consumes it after full conversion and returns null when exhausted |
| `session.flushScan({scanId,timeoutMs?})` | Future-capable; discards the specified scan list and releases the scan reservation |

`WiFiMeshSession` cannot be constructed directly. At most one Session may be active,
with four live Session handles, one pending/in-flight command per Session and eight
live command handles. Routing tables are limited to 1000 entries and group addresses
to 64. Closed handles still referenced by JS/Futures continue to consume the budget;
identities neither wrap nor get reused within a boot. All Future-capable methods
support asynchronous waiting through the existing `Future.call` mechanism.

## Startup and options

`open` requires a complete plain options object. Unknown fields, implicit type
coercion, fractional numbers and overflow are rejected. ByteSource inputs are copied
during call capture; subsequent JS mutations do not change captured configuration
or commands.

| Field | Range and default |
| --- | --- |
| `meshId` | Required, nonzero six-byte ByteSource |
| `routerSsid` / `routerPassword` | SSID is required, 1..32 UTF-8 bytes; password defaults to empty and must be empty or 8..64 bytes; embedded NUL is forbidden |
| `routerBssid` | Optional six-byte ByteSource; defaults to all zeros for SDK selection |
| `apPassword` / `apAuthentication` | Password is required; authentication defaults to `wpa2-psk`, with `open`, `wpa-psk` and `wpa-wpa2-psk` also supported; open requires an empty password, others require 8..64 bytes |
| `encryptIE` / `ieKey` | Encryption selection is required; true requires an explicit key of 8..64 printable ASCII bytes, false forbids a key |
| `channel` | 0..14, default 0 for discovery; the driver and regulatory configuration still determine legal channels |
| `topology` / `type` | Default `tree` / `idle`; also supports `chain`, with types `root`, `node`, `leaf` and `station` |
| `maxLayer` / `capacity` | Default 6 / 32; tree layers 1..25, chain layers 1..1000, capacity 1..1000 |
| `receiveQueue` | SDK queue setting 16..128, default 16; the framework separately reserves two 1500-byte receive slots for self/ToDS |
| `sendBlockMs` | SDK send blocking setting, 1..60000 ms, default 1000; independent of the Future timeout |
| `maxConnections` / `nonMeshConnections` | Default 4 / 0; ranges 1..10 / 0..9 respectively, with a total no greater than 10 |
| `allowChannelSwitch` / `allowRouterSwitch` | Boolean, default false; allows the SDK to change the configured channel/router respectively |
| `votePercentage` | Voting threshold set before startup, (0,1], default 0.9 |
| `fixedRoot` / `selfOrganized` / `powerSave` | Boolean, default false / true / false |
| `allowApRestart` | Default false; if the original configuration includes AP/APSTA, explicitly permits briefly starting the original AP to verify restoration |
| `timeoutMs` | Startup deadline measured from activation, 1..120000 ms, default 30000 |

The worker admits only a healthy, stopped Radio with no owners or a cold
initialization. It does not implicitly close Station, AP, ESP-NOW, CSI, Monitor,
NAN or other owners. Mesh holds the central exclusive lease throughout its lifecycle.
It uses temporary RAM configuration, saving and restoring existing STA/AP
configuration and known Radio policies. C5 first switches to 2.4 GHz, then creates
a temporary AP with this Mesh password. `ready` means only that SDK startup and
Radio handoff are complete; parent association, role, IP and Internet reachability
must be queried separately.

In `status`, `parentKnown/parentConnected` come from native control events.
`root/nodes/routes` are valid only when `snapshotValid` is true; otherwise they are
null. `type` additionally requires an observed parent connection. `txPending/rxPending`
are native send/receive queue counts and are null without a valid snapshot. SDK
getters are sampled sequentially; these fields are not an atomic device-wide
snapshot. A valid snapshot includes the native router BSSID in `routerBssid`, while
`queryError` retains getter errors. `ipReady/ipv4` describe local IPv4 state,
separately from `toDSReachable`. The root and a `station` directly connected to the
router can start DHCP; Mesh node/leaf roles do not. Management runs on the same
worker and may wait for an SDK send/scan to return. Captured disconnection or an
invalid snapshot immediately invalidates public IP readiness; Wi-Fi's own
disconnection handling does not depend on the JS observation queue. `reservedBytes`
includes only framework storage for the Session and Radio/native owners. See
`wifi.diagnostics` for the full shared budget; SDK workers, queues and device-wide
peak usage remain to be measured during phase validation.

## Sending, receiving and control results

`send` requires `data` (1..1472 bytes) and defaults to `destination:"root"`,
`protocol:"binary"` and `reliable:true`. Protocols also include `http/json/mqtt`.
Destination fields must match the following table; inapplicable
`address/ipv4/port/dropOnRootChange` fields are rejected.

| destination | Address and constraints |
| --- | --- |
| `root` | No address; must be reliable |
| `peer` / `group` | Nonzero six-byte `address` |
| `fromDS` | Nonzero six-byte `address`; requires root at SDK execution time |
| `broadcast` | No address; uses the SDK broadcast destination |
| `toDS` | Nonzero four-byte `ipv4` and `port` in 1..65535; optional `dropOnRootChange` |

An ordinary command result is `{identity,bytes,dispatched,returned,completedAtUs}`,
with time in monotonic microseconds. Send success means the SDK returned success
and accepted the data; it does not prove receipt by the peer application, and even
reliable sends do not promise end-to-end acknowledgment. Successful connect/disconnect
calls mean the request returned; observe status for actual association. Group
operations accept 1..64 unique, nonzero six-byte addresses. They do not promise
atomic rollback of individual entries if a compound SDK call fails; use `groups()`
to inspect actual values after failure.

`receive` reads self by default; `toDS:true` uses the root's ToDS receive slot. Each
of the two lanes permits one waiter. Messages contain sequence, from, toDS, optional
IPv4/port, native protocol/service/flags and `data:ByteView`. Sequence is a native
identity scoped to this Session, not an RF sequence number. A native receive slot
is consumed only after both the JS object and ByteView are fully converted.
Conversion failure, wait cancellation or timeout leaves the original message
available for retry. The returned ByteView is an independent copy and remains
readable after Session closure; call `message.data.close()` when finished to return
the shared budget. Closing discards undelivered native receive slots.

## Timeouts, closing and recovery

All wait methods accept `timeoutMs` in 1..120000 ms, default 30000. Receive returns
null on timeout; other waits throw `WIFI_MESH_TIMEOUT`. Cancelling a ready/receive
wait does not close the Session. Cancelling an already requested close/recover wait
does not stop native cleanup. The startup deadline belongs to open and is
independent of the ready wait deadline.

Cancelling a send/control operation prevents commands that have not been submitted.
An already submitted SDK call cannot be preempted; its data and parent Session
reference remain retained until it actually returns. Timeout does not mean the
send or control had no effect. Error details include
`operationIdentity/dispatched/returned/nativeError/waitTimedOut`, preserving both
the SDK error on return and the wait timeout separately. JS roots may be released
when the Future ends, while the worker continues to own native job storage. Status
retains startup errors, cleanup errors and original stages without exposing credentials.

Session closure first rejects new commands. After an in-flight call returns, it
continues native retirement, physical STOP, netif detach/fence, restoration of the
original configuration and release of the exact owner. Completed steps are not
repeated. An originally cold Radio returns to uninitialized; an originally healthy,
stopped Radio stays stopped after its configuration is restored. Any brief AP
startup to verify restoration has been explicitly authorized by allowApRestart.

Known configuration replay/START failures retain the frozen snapshot. `recover`
explicitly requests one physical recovery attempt without restarting Mesh. Unknown
init/deinit outcomes or failures where native cleanup cannot be proven retain
diagnostic state. `restartRequired:true` requires a device restart; a runtime restart
does not guarantee recovery. Runtime teardown also waits for live native work to
actually retire and does not release data still referenced by the worker.

## Observation queue and validation status

Each Session supports at most one watch, with capacity 1..16, default 8. At most
four watch storage objects may remain live globally. Queue and context budgets
remain reserved until the storage is actually destroyed. Observations include the
pinned SDK event id, sequence, a field bit mask and allowlisted
MAC/channel/layer/reason/tableSize/tableChange/value/duty fields. Absent fields are
null; the corresponding bits are 1/2/4/8/16/32/64.

Native control updates precede the eight-slot observation ring and SDK post. A full
ring or EventQueue drops observations without blocking disconnection, send return
or closing. Event conversion failure likewise does not affect native control. A
watch is not a reliable transaction log; its observation queue is requested to close
when the Session begins closing.

Host/VM, GC/OOM, race, full three-target and disabled-build checks, plus hardware/RF
validation, remain scheduled for consolidated execution after all Wi-Fi APIs are
complete. Long soak testing is deferred to the BLE phase.

## Manual parent selection and scanning

These methods require native self-organization to be disabled at execution time;
use `setSelfOrganized({enabled:false})`. Mesh continues to hold the exclusive Radio
lease. Scanning makes a blocking SDK call on the existing worker and does not use
observation events to decide completion. Results become readable only after an
explicit SDK success return. A scan whose termination is unconfirmed retains its
inputs and reservation; close the Session and complete physical STOP before reusing
the scan slot or returning to self-organization.

`setParent` options:

| Field | Contract |
| --- | --- |
| `ssid` | Required, string or ByteSource, 1..32 bytes without NUL; ByteSource supports non-UTF-8 SSIDs |
| `password` | Defaults to empty; must be empty or 8..64 bytes without NUL; a non-root parent's open/encrypted setting must match this Session's AP authentication setting |
| `bssid` | Optional, nonzero unicast six-byte ByteSource; must identify the same parent as the SSID |
| `channel` | Required, 1..14, still restricted to currently legal channels |
| `meshId` | Optional, nonzero six-byte ByteSource; defaults to the current Mesh ID |
| `type` / `layer` | Required; root layer is 1, node layer must exceed 1 and be below maxLayer, leaf layer is 1..maxLayer; the SDK may change the layer after joining |
| `timeoutMs` | 1..120000, default 30000 |

The native setter may change mode, disconnect the old parent and initiate a new
association. Success means only that the request returned. Use root/layer=1 to
select a router; the SDK does not support specifying station in setParent. Full
Station driver options are not forwarded: the SDK root path rebuilds router
configuration, so other Station security fields are not supported parameters of
this API. Input passwords are zeroed after the native call returns.

`scan` accepts an optional options object:

| Field | Range and default |
| --- | --- |
| `ssid` / `bssid` | Optional filters; SSID follows the setParent byte rules, BSSID is a nonzero unicast six-byte ByteSource |
| `channel` / `channels` | channel defaults to 0 to scan all permitted 2.4 GHz channels; otherwise 1..14. channels accepts 1..14 unique channels and is mutually exclusive with a nonzero channel; 5 GHz is explicitly skipped |
| `showHidden` / `mode` | Default true / passive; mode also supports active |
| `activeMinMs` / `activeMaxMs` | Active mode only; default 0 / 120, ranges 0..1500 / 1..1500, with min no greater than max |
| `passiveMs` | Passive mode only; default 360, range 1..1500 |
| `homeChannelDwellMs` | Default 30, range 30..150 |
| `coexistenceBackgroundScan` | Default false; forwards the SDK background scan option and does not imply validated coexistence across modules |
| `timeoutMs` | 1..120000, default 30000; a wait deadline that does not cancel an already submitted native scan |

Returns `{identity,running,completed,uncertain,retained,total,remaining,error}`.
The same snapshot appears in `status().scan`, which is null when no scan exists.
If a scan was submitted before the wait timed out, its identity remains available
from status. completed means only that the native scan finished; error may also
record a subsequent count-query failure. total is the size of this SDK list;
remaining includes a record already taken from the SDK but still awaiting full JS
conversion. Scan identities are not reused within a boot.

`receiveScan` and `flushScan` require the matching `scanId`. While a scan reservation
exists, status and read-only configuration/routing queries are allowed, but new
scans and other control mutations are rejected. Even after all results are read,
explicitly flush before setParent or re-enabling self-organization. Once scan
termination is confirmed, flush can discard a list with a read error. Successful
flush also requires a native list count readback of zero; otherwise the scan
reservation remains for diagnosis and explicit retry.

Each receiveScan returns `{scanId,sequence,accessPoint,bssidBytes,association,meshIE}`.
accessPoint reuses ordinary Wi-Fi scan record fields. association decodes only the
pinned SDK's tree/chain IE, including Mesh ID, role/layer, capacity, RSSI, root/vote
and ToDS fields. It is null for unknown or absent Mesh IEs and does not authenticate
a parent's identity or security. meshIE is an independent ByteView of the raw IE
returned by the SDK, limited to 257 bytes; this limit is also exposed as
capabilities.maxScanIEBytes.

A bounded native record is allocated before the destructive SDK read. It remains
retained until the JS object and ByteView are fully converted and committed using
the exact scanId/sequence. Conversion failure, cancellation or wait timeout permits
rereading the same record without silently skipping an AP. With no records remaining,
receiveScan returns null immediately. The returned meshIE is unaffected by flush
or Session closure; call `record.meshIE.close()` when finished. Undelivered records
return their budget after successful flush or retirement during closing.

## Configuration, topology and power controls

All methods below are Future-capable and share the Session's command slot. Options
may include timeoutMs, default 30000. Except for read-only queries, success returns
WiFiMeshCommandResult, indicating that the SDK call returned. Continue observing
status/watch to determine network convergence, role changes or channel-switch
completion. Cancellation and wait timeout do not undo submitted mutations. Layer
limits, capacity, topology, AP password/authentication/connection limits, RX queue,
PS enablement and voting threshold are startup configuration; close the Session
and create a new one with updated open options to change them.

| Method | Options and actual behavior |
| --- | --- |
| `configuration({includeSecrets?,timeoutMs?}?)` | Sequentially reads native configuration, capacity/connection limits, fixed-root policy, self-organization, PS, voting threshold, association expiry and root healing delay; not an atomic snapshot |
| `setRouter({ssid,password?,bssid?,allowRouterSwitch?,timeoutMs?})` | Fully replaces Mesh router configuration; SSID 1..32 UTF-8 bytes, password empty or 8..64 bytes; omitted password means empty, omitted BSSID requests SDK selection |
| `setMeshId({address,timeoutMs?})` | Changes the network ID dynamically; nonzero six-byte ByteSource |
| `setType({type,timeoutMs?})` | `idle/root/node/leaf/station`; the native driver decides whether the current state allows it |
| `setSelfOrganized({enabled,selectParent?,timeoutMs?})` | selectParent defaults to false and is allowed only with enabled=true; true may cause the current root to relinquish its role and seek a new parent |
| `setFixedRoot({enabled,timeoutMs?})` | Changes fixed-root configuration; devices on the same network must use the same setting, with the application assigning the root when enabled |
| `setRootConflicts({enabled,timeoutMs?})` | Allows/disallows multiple roots on the same network |
| `setAssociationExpiry({seconds,timeoutMs?})` | 10..2147483 seconds; a child with no data for this period may be disassociated. The SDK recommends longer periods for encrypted networks, such as 30 seconds |
| `setRootHealingDelay({milliseconds,timeoutMs?})` | 0..2147483647 ms; delay before root healing begins |
| `setIEEncryption({enabled,key?,timeoutMs?})` | true requires a key of 8..64 printable ASCII bytes; false forbids a key and disables IE encryption |
| `waiveRoot({attempts?,percentage?,timeoutMs?}?)` | Requires the actual root role at execution time; voting attempts 15..2147483647, default 15; threshold (0,1], default 0.9. The current root may remain if no better candidate exists |
| `switchChannel({channel,beaconCount?,routerBssid?,timeoutMs?})` | Only the actual root may request network-wide CSA; channel 1..14, beaconCount 1..255, default 15; accepts an optional nonzero six-byte BSSID for a new router |
| `setDeviceDuty({duty,type,timeoutMs?})` | duty 1..100; type=`request` uses network duty, `demand` requests this device's duty; 100 stops power saving |
| `setNetworkDuty({duty,durationMinutes,timeoutMs?})` | Network-wide duty 1..100, duration 1..2147483647 minutes; only the actual root may use -1 to continue until a new policy takes over |
| `signalDuty({forwardCount,timeoutMs?})` | Forwards duty signaling, count 0..254; the SDK command stores count + 1 in a single-byte field |
| `subnet({address,timeoutMs?})` | Reads an associated child's subnet addresses; returns number[][] with a limit of 1000. Count and list are read separately, so topology changes may cause failure |
| `hasGroup({address,timeoutMs?})` | Queries whether this device belongs to the group; returns boolean |
| `upstreamCapacity({address,timeoutMs?})` | Native upstream capacity of this device or an associated child; returns `{available,lastSequence}`, preserving the SDK's signed available value |
| `powerStatus(options?)` | Sequentially reads `{enabled,active,deviceDuty,deviceType,networkDuty,durationMinutes,networkType,appliedRule,runningDuty}`; type/rule are numeric SDK values |
| `tsfTime(options?)` | Signed SDK microseconds; throws if outside the JS safe integer range rather than rounding |

configuration returns raw routerSsidBytes and a strictly decoded UTF-8 routerSsid
(null if decoding fails). Addresses are copied number[] values. apAuthentication
is the native numeric enum. sendBlockMs is the value successfully set by this
Session; other readable policies come from SDK getters. By default,
secretsIncluded=false and routerPassword/apPassword/ieKey are null; secrets are
read and returned only with explicit includeSecrets=true. The caller manages the
returned JS strings. Status, watch and error details do not contain credentials.

The IE key is written before encryption is enabled; failure in the second step
does not roll back the key write. Error details controlStage and completedSteps
record the failure point and steps that returned success. configuration's encryptIE
and ieKeyLength reflect native settings confirmed by this Session. Disabling
encryption does not claim to erase the SDK's internal key. Input secrets are zeroed
after the setter returns. Temporary secrets from configuration queries are zeroed
immediately on failure or on the default hidden path; explicitly returned copies
are zeroed in command storage when the command is released. Native storage for
both command types remains retained until the SDK actually returns.

The pinned SDK's esp_mesh_waive_root explicitly requires is_rc_specified=false and
does not implement selecting a replacement root. The esp_mesh_set_network_duty_cycle
parameter contract supports only ENTIRE. These APIs therefore have no designated
root candidate or UPLINK options; unknown fields are rejected. The corresponding
declarations and comments in the pinned esp_mesh.h define these limits.

Inspection of the pinned C5 SDK binary also confirmed that the association-expiry
setter can return ESP_OK even if ioctl allocation fails, and discards the ioctl
return code. This API therefore reads back the actual expiry after the setter
returns. A mismatch reports WIFI_MESH_FAILED /
controlStage=mesh-association-expiry-readback, completedSteps=1. This means the
change is unconfirmed, not that it necessarily had no effect; it is not retried
automatically. Relevant SDK instructions and hashes are retained in this batch's
build/w08-mesh-controls-evidence.json. Runtime proof on other targets remains part
of phase acceptance.
