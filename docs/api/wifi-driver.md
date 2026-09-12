# `wifi.driver` controls

## Driver discovery and restore

`wifi.driver.capabilities()` returns `apiVersion:"wifi-driver/1"`, the build's
`target`/`idfVersion`, `secretReadback`, Station/AP option flags, `operations`
with each registered method's principal `idfSymbols`, `available` and
`stateRequirements`, and `configSchemas` input-key lists. It accepts no arguments
and works before initialization or during a fault. These are build descriptions;
option flags, parameter contracts and runtime owner checks still apply. The
field lists do not advertise target-unsupported values. AP fields are empty
without SoftAP; coexistence power control availability follows its build gate.

`wifi.driver.status()` accepts no arguments and returns the same
`WiFiRadioStatus` contract and converter as `wifi.status().radio`. It includes
physical driver ownership/generation, clients, operations, lifecycle events,
configuration/rollback, fault/cleanup, retained restart bytes, and module
observations. It contains no credentials and does not initialize Station/netif.
Native Radio scalars share a snapshot; module diagnostics are separate readings.
An observation does not reserve admission for the next operation.

`wifi.driver.restore()` explicitly requests SDK Wi-Fi defaults. It requires an
initialized, fully stopped Radio, retired STOP events, zero leases and
no retained child or unrelated native cleanup, temporary policy or restart owner.
Close the children and call `wifi.stop()` first. Healthy state and faults from
standalone mode/protocol/bandwidth writes or their readback/rollback are accepted.
Unrelated configuration, antenna, initialization and native ownership faults
remain rejected. It does not stop a live application, deinitialize, start Wi-Fi
or reconnect Station. No arguments are
accepted. The physical generation/identity space stays intact. The existing
RAM/FLASH future-write policy is reselected after the loader completes; this
does not suppress the loader's own NVS writes.

The fixed C3/S3/C5 SDK internally calls STOP, then its default NVS loader.
The build-local SDK repair preserves loader allocation errors that two original
wrappers replaced with ESP_OK. The SDK loader still does not propagate all NVS
setter/commit errors. Therefore `true` means loader acceptance, valid mode
readback and storage selection; **it does not verify flash durability or certify
credential erasure**. Read the current interface configuration to inspect its
result. Workspace files, Enterprise/DPP credentials and product configuration
are outside this Wi-Fi-driver reset contract.

An admitted attempt invalidates saved STOP observations and framework-only
policy/rate/interval/offset knowledge; it never invents accepted defaults.
Old identity counters are preserved. Explicitly configure needed policies
again before operations that require a known predecessor. On error,
`WIFI_DRIVER_WRITE_FAILED` keeps the failing stage and raw `espCode`;
`status().configuration` retains `mutationAttempted`,
`persistentMutationPossible` and the original error. The Radio retains its
first fault and records the currently failed cleanup suffix. A later explicit
`restore()` retries only that suffix: defaults, mode readback, or storage selection.
An accepted defaults load is not repeated when readback or storage failed; rejected
calls cannot discard this native progress. Storage selection failure may be retried
even though the current storage setting is unknown, using the retained selected
RAM/FLASH policy. Full acceptance clears only the admitted fault and cleanup state,
leaving the driver stopped. Mutation flags describe the current call; readback-only
failure does not claim a new write. There is no guessed rollback, automatic retry,
or claim that runtime restart restores earlier NVS contents. A rejected
admission performs no SDK mutation. Dynamic/physical acceptance remains pending.


## HE statistics controls

On C5, `wifi.driver.getStatisticsConfig()` returns the actual native enabled
state as `{rx:{ordinary,multiUser},tx:{voice,video,bestEffort,background}}`.
All leaves are booleans. It reads native allocation/enable state on the Wi-Fi
task; it does not return counters, RF evidence, or a saved restart request.
Radio must be started and stable in STA/AP/APSTA mode. No arguments are accepted.
C3/S3 expose these methods with `available:false`; calls fail as unsupported.

`wifi.driver.configureRxStatistics({ordinary:boolean,multiUser:boolean})`
replaces both RX settings and returns actual `{ordinary,multiUser}`. Both keys
are required; unknown keys and coercible non-booleans are rejected before SDK
access. Every accepted replacement releases the preceding RX buffers, including
equal-value requests. Native allocation failure leaves both RX kinds disabled
and returns the original error; it does not affect TX.

`wifi.driver.setTxStatistics(category, enabled)` accepts `"voice"`, `"video"`,
`"bestEffort"`, or `"background"` and a boolean, returning the actual boolean
for that category. Only that category changes. Enabling an already-enabled
category preserves its counters. An allocation failure frees its partial native
buffers and preserves other categories. Read `getStatisticsConfig()` after an
error before deciding to repeat a write. A successful RX write can precede a
JS return-object allocation failure; retrying would replace those buffers again.

Writes require idle framework helpers and exact Station/AP/application owners;
other Radio owners, pending native operations and faults reject. These controls
do not implicitly initialize Wi-Fi or disconnect an associated Station.

Managed STOP captures the enabled configuration and releases statistics on the
Wi-Fi task before stopping the driver. Failed dispatch retains the cleanup
obligation and blocks STOP/deinit. START and explicit restart restore the saved
settings with fresh storage; counter continuity is not promised. Restoration
keeps its successful prefix so a later failed TX allocation does not reset RX
again. Shutdown, driver-default restore and device reboot discard saved intent.
These settings are RAM-only and the native SDK owns their allocations. The
configuration getter never reports saved intent as currently enabled state.
Runtime teardown uses the same Radio cleanup boundary; runtime restart does not
guarantee restoration of a former runtime's settings.

SDK write errors use `WIFI_DRIVER_WRITE_FAILED`; reads use
`WIFI_DRIVER_READ_FAILED`. Both retain native `espCode` and the failing stage.
If post-write native state cannot be established, Radio retains a diagnostic
fault for explicit cleanup. The fixed SDK HAL allocation/error repairs are
hash-gated build adapters; counters, memory peaks and runtime/RF behavior still
require Wi-Fi phase validation.

## Interface configuration

`wifi.driver.getInterfaceConfig(interface, options?)` reads one actual SDK
configuration and returns `WiFiStationDriverConfigSnapshot` or
`WiFiAccessPointDriverConfigSnapshot`. `interface` is `"station"` or
`"access-point"`; AP requires SoftAP support. Wi-Fi must already be initialized
and stable, with no native operation, lifecycle, fault or cleanup transition.
Reading does not initialize, start, associate or change mode. The SDK determines
whether an inactive interface configuration is available.

The snapshot has an `interface` discriminator, `ssid` text (null for invalid
UTF-8), lossless `ssidBytes`, and all non-reserved public Station/AP config
fields from the checked SDK mapping. Enum fields such as `scanMethod`,
`minimumAuthMode`, `authMode`, `saePwe` and `pairwiseCipher` are `{ id, name }`:
the actual numeric SDK ID is preserved, and an unknown/reserved ID has null
name. A recognized name does not authorize using it in a configuration write.
`pmfCapable` and `pmfRequired` preserve both observed flags; derived `pmf` is
null if those flags are inconsistent. Station includes `bssidSet`, `bssid` and
six raw `bssidBytes`. AP includes the SDK's `ssidLength`; zero means native
string-length inference, while `ssidBytes` contains the resulting bounded SSID.

The generated [field table](../generated/wifi-driver-config-schema.md) covers
35 Station and 21 AP public leaves, including HE/VHT and nested threshold/PMF/
BSS-idle fields. Reading a stored field does not establish that its feature is
enabled or effective on the target. Snapshot objects have a different type from
writable constructor inputs; they are not accepted as configuration patches.

The only read option is `includeSecrets`, boolean, default false. Password and
SAE H2E identifier text/byte fields are null by default, and native credential
storage is erased before returning that readback. `includeSecrets:true` requires
`CONFIG_ESP32_MQUICKJS_WIFI_ALLOW_SECRET_READBACK=y`, which defaults off. It is
rejected before SDK access when disabled. Explicit secret reads return bounded
`password`/`passwordBytes` and Station `saeH2eIdentifier`/`saeH2eIdentifierBytes`;
text is null for invalid UTF-8 while the byte array remains exact. The snapshot
reports `secretsIncluded`. Status, diagnostics, errors and default reads never
include these credentials.

`wifi.driver.setInterfaceConfig(interface, config)` replaces the entire selected
interface configuration using `WiFiStationDriverConfig` or
`WiFiAccessPointDriverConfig`. Missing fields use the same constructor defaults
as `wifi.configure`, not values from the previous SDK config. All inputs and
security dependencies are captured before native mutation. It requires an
initialized, fully stopped, zero-owner Radio with the selected interface enabled;
mode and storage selection are preserved. It does not stop resources, connect,
enable another interface or weaken an unsupported security policy.

The shared Radio transaction verifies native acceptance and security, uses the
dedicated SDK operation for explicit PMF disable, and restores the previous
configuration after a failed write/readback. It returns the actual accepted
snapshot with secrets always hidden, even in a secret-read-enabled build.
Credentials and temporary snapshots use the shared control budget and are
securely erased and freed after capture, native or JS-conversion failure.

```javascript
// No open child resources. Configure an initialized, stopped Station driver.
wifi.configure({ mode: "station", start: false });
var applied = wifi.driver.setInterfaceConfig("station", { ssid: "example-network" });
var observed = wifi.driver.getInterfaceConfig("station");
print(observed.ssid, observed.scanMethod.id, observed.pmf);
```

Read errors use `WIFI_DRIVER_READ_FAILED`, the exact operation/interface and
`admission`, `secret-readback`, `interface-config` or `decode` stage. Native write
errors use `WIFI_DRIVER_WRITE_FAILED` with original stage/code and separate
mutation/persistence/rollback fields, also available in `radio.configuration`.
Unsupported captures retain the shared `WIFI_CONNECT_UNSUPPORTED` or
`WIFI_AP_UNSUPPORTED` code with operation `wifi.driver.setInterfaceConfig`.
Malformed input throws before driver mutation. A failed FLASH write can leave
NVS uncertain even after local rollback; failed rollback retains Radio fault and
cleanup. An OOM delivering the successful result can occur after the config has
changed: inspect current settings before deciding on another action. Runtime,
GC/OOM, concurrency and hardware qualification remain pending the Wi-Fi tests.

## BSS color collision reporting

`wifi.driver.setBssColorCollisionReporting(enabled)` requires one strict boolean,
a stable started Station (Station or APSTA), and exact framework owners with no
competing feature/native operation. A connected Station stays connected. HE
builds support the operation; C3/S3 capabilities report it unavailable and calls
reject with `ESP_ERR_NOT_SUPPORTED` before any SDK mutation.

When enabled, native Station reports detected BSS color collisions to its AP.
This does not create a JavaScript event subscription. Every accepted call,
including a repeated value, clears the native collision bitmap. The return is
SDK acceptance; there is no native policy getter or RF-delivery guarantee.
`wifi.status().radio.policies.bssColorCollisionReporting` retains the existing
policy revision, accepted value/history, and uncertain/error fields. A failed
write preserves its original SDK error and faults Radio; no guessed rollback.

Managed `driver.restart()` captures a known policy before deinit and replays it
only after Station START, before owner publication. A restart mode that omits
Station rejects before destruction. Ordinary deinit/restore invalidates current
framework knowledge; it does not assert a particular native default. Closed
or uncertain sources do not authorize replay. Runtime and RF tests are deferred.

## Global scan defaults

`wifi.driver.getScanParameters()` reads actual Station scan defaults.
`wifi.driver.setScanParameters(config)` replaces all four millisecond fields:

```javascript
var actual = wifi.driver.setScanParameters({
  activeMinMs: 0, activeMaxMs: 120, passiveMs: 360, homeChannelDwellMs: 30
});
var defaults = wifi.driver.setScanParameters(null);
```

These defaults also affect connection scans. Each field is required and must be
an integer. Active minimum/maximum and passive time are 0..1500 ms. Zero maximum,
passive or home dwell selects 120, 360 or 30 ms respectively; zero minimum stays
zero. Minimum cannot exceed the effective maximum. Nonzero home dwell is 30..150
ms. Unknown keys and invalid values reject before taking ownership or calling SDK.
`null` uses the SDK reset path. Results contain the four actual, normalized fields.

Both methods require a stable started Station (Station or APSTA); neither starts
Wi-Fi or reads a disabled Station from a cache. Writes require exact framework
owners and idle helpers; a connected Station can remain connected. Active scan,
connect, cleanup, competing feature owners or native operations reject. SDK errors
retain their original stage/code. After a failed write, unchanged previous state
is accepted as cleanup evidence; otherwise rollback requires readback. Unproven
rollback faults Radio. JS allocation failure when delivering a successful result
cannot undo the native write; read the current defaults before retrying.

The settings are RAM-only even with FLASH storage selected. Managed STOP/START
and `driver.restart()` preserve observed values. Restart saves them before deinit,
restores them after Station activation, and verifies before publishing owners.
If Station stays disabled, saved intent waits for its next activation and is never
reported as a live getter result. A failed source observation is retained as an
error, not replaced with defaults. Ordinary physical deinit, explicit `restore()`
and device reboot reset this policy. No SDK call is made inside a critical section.

## Driver restart

`wifi.driver.restart(options?)` is a **Candidate** synchronous operation returning
`WiFiStatus`. It reconstructs the physical driver and restores/starts the saved
Station, SoftAP or APSTA mode. It does not reconnect Station, restore AP clients,
resume closed capture/TX sessions or automatically rearm RSSI notifications.

The source can be clean and uninitialized, or a healthy, initialized, fully stopped
driver configured for Station, SoftAP, APSTA or off. Complete, unchanged same-generation STOP observations
allow capture without another source START. Healthy configurations changed after
STOP, or configured but never started, obtain fresh runtime observations as
described below. All Application/STA/AP/ESP-NOW/CSI/Monitor/Raw TX and
other leases, wake locks, native operations and borrowed-setting restore
obligations must have exited. AP/Station helper cleanup and detach errors also
reject admission. There is no `force`, mode selection or implicit child close.
`wifi.capabilities().features.driverRestart` advertises this callable operation,
not unrestricted recovery or hardware qualification.

A healthy, started, unassociated Enterprise Station is also admitted with its
exact managed Application/Station/AP leases and installed profile. An active
SoftAP requires `allowApRestart:true`. Restart holds the Enterprise configuration
control, retires the old native binding, then rebuilds and reinstalls the same
credentials and security policy before publishing new leases. It does not
associate Station or restore AP clients. Ordinary stop still disables EAP;
restart from that stopped state does not implicitly enable it. See
[Enterprise restart ownership](wifi-enterprise.md).

On the reviewed fixed SDK for C3/S3/C5, subsequent `setStorage()`,
`setRssiThreshold()` and `setEventMask()` calls preserve STOP observations: they do not change the saved
power, band, channel or inactive-time fields. Restart captures the latest accepted
storage policy and event mask. Mask write/readback failures with verified rollback
preserve eligibility; uncertain rollback still faults Radio and rejects restart.
Only the existing probe-request observation bit is writable. A failed storage write still makes storage unknown and blocks
restart until explicit successful storage selection repairs that fault. An RSSI
write error does not prove acceptance or armed state, but does not invalidate RF
history either. These calls never create missing history or revive history already
invalidated by another writer. Other targets retain conservative invalidation
until their SDK implementation is reviewed. See
[stopped control writes](../investigations/2026-09-09-w07-stopped-controls.md) and
[event-mask restoration](../investigations/2026-09-09-w07-stopped-mask.md).

When STOP history is missing or invalidated, the operation prepares Station/AP
helpers, allocates the checkpoint, and captures readable configuration and known
policy/rate/interval records before source mutation. It temporarily selects RAM
storage and starts the configured source under the same exclusive lifecycle.
Station is not connected; an enabled AP may advertise during this temporary
START. The checkpoint reads actual post-START configuration, power, channel and
inactive-time values, including SDK normalization, before the ordinary
STOP/rebuild/replay phases. It does not restore unavailable pre-STOP RF values
from guesses. The originally selected storage is retained for final commit.

For a clean uninitialized source, restart uses the same Station/RAM cold defaults
as `wifi.start()`. It reserves the exact zero-owner lifecycle before selecting
that path; creates no predecessor configuration/credential snapshot; initializes
the driver before preparing Station handlers/netif; and publishes helper leases
only after START and its existing event/readback barriers. It does not associate
with an AP. The first physical driver keeps its initial generation; an earlier
successful shutdown has already advanced the generation. Failure keeps the same
cleanup reservation and raw error, with no automatic second initialization.

For an initialized off source, restart preserves the real saved interface/global
configuration and ends **initialized, stopped and off**, with no Application/STA/AP
leases. It temporarily selects and starts Station in RAM to obtain runtime-only
observations, then rebuilds and verifies the restored driver before STOP, event
drain and mode=off readback. It restores the original RAM/FLASH selection only
after that final off readback. The same lifecycle and secret checkpoint remain
reserved until helper retirement; intermediate failure does not publish owners
or release the checkpoint early.

If a saved AP 11b policy or FTM responder offset requires temporary AP activation
from off, pass `allowApRestart: true` (strict boolean, default false). Without it,
admission rejects before a lifecycle is claimed or any driver mutation. With it,
the temporary work mode is APSTA and the saved AP can advertise during restoration;
the successful final state remains off. An already configured stopped AP/APSTA
source retains its existing restart semantics. Unknown required policies still
reject. This option does not force-release any owner or authorize Station association.

Clean uninitialized is distinct from failed initialization. The shared NVS boot
helper caches its first result for the boot; an NVS error is not retried by a
runtime/Radio restart. Failed `esp_wifi_init` leaves ownership unproven. Both
remain diagnosable with `restartRequired`, and require a device reboot (an NVS
storage fault may also require explicit repair). Wi-Fi never erases NVS to retry.

A faulted driver with no retained complete restart checkpoint, or an incompletely
retired native predecessor, still rejects new-source admission. A retained complete
checkpoint has the explicit retry path below. A required policy value that cannot be read or recovered from a
known record can also reject capture after lifecycle admission. Missing RF
history alone no longer rejects an otherwise healthy configured source.

`timeoutMs` is an integer from 1 through 60000, default 10000.
`allowApRestart` is the explicit off-source AP permission described above.
It provides a shared budget for native event/netif waits across reconstruction.
Synchronous SDK calls and mutex acquisition are not preempted, so this is not a
hard wall-clock deadline. Input validation, including unknown keys, completes
before native admission.

```javascript
// Wi-Fi is initialized and fully stopped; all owners have exited.
var restarted = wifi.driver.restart({ timeoutMs: 10000 });
```

The operation reserves its lifecycle atomically with source/mode/registry
validation, captures configuration before deinit, retires old helpers, rebuilds
the driver, attaches new helpers, replays configuration and verifies startup
before publishing owners. Replay/verification uses RAM storage, then commits
the original storage policy. This does not claim that old-driver shutdown never
writes NVS, or that RF settings remain continuous during reconstruction.

Native errors use `WIFI_RESTART_FAILED`, operation `wifi.driver.restart`, with
`stage`, `espCode`, `espName`, `lifecycleAdmitted`, `checkpointAttempted`,
`replayAttempted`, `resumeAttempted`, `cleanupPending`, `restartRequired`,
`restartSnapshotBytes`, `radioFaultStage`, `radioFaultError` and `configuration`.
Phase flags mean the phase was entered; checkpoint entry alone does not mean an
SDK STOP write happened, and can include the source START. Failures during that
preparation report `restart-source-ram`, `restart-source-start` or the specific
readback stage; native START/event faults remain separately available.
`configuration` contains only a record reached during
this call and is null before checkpoint. Radio fault fields describe current
state and can predate an admission rejection. No credential contents are exposed.

After admission, a failure retains the same lifecycle reservation and any frozen
configuration. Source mutation failure retains its partial checkpoint until
cleanup; incomplete snapshots cannot be replayed. If the original complete checkpoint
is still retained, an explicit new `wifi.driver.restart()` call can retry the same
lifecycle. It rechecks zero native/lease/wake owners, helper idleness, no foreign
recovery coordinator and no reboot-required or unproven-init state. It uses the
original work mode, configuration, policies and storage rather than reading the
failed driver's state as a new source. Off restoration still ends off and requires
`allowApRestart:true` again if its work mode includes AP.

An Enterprise restart keeps its profile control across failures. A retained
failed installation is cleared before retry STOP/deinit, even when it belongs
to a newer physical driver generation. The original lifecycle remains the
owner. Failure before the first checkpoint can retry its unfinished credential
clear/AP-release suffix; after capture was attempted, only a complete checkpoint
permits rebuilding. Every Enterprise APSTA retry requires `allowApRestart:true`.
`restart-enterprise-install` records an installation failure in Radio fault and
configuration diagnostics; runtime stages distinguish clear, checkpoint and
resume. Runtime closing bars reinstall and continues cleanup instead.

The retry first completes STOP/event drain, then retires old or partially attached
helpers, rebuilds, replays and verifies. Accepted STOP and successful unregister /
deinit/helper cleanup prefixes are not repeated because a later suffix failed.
Each explicit call authorizes one reconstruction attempt; timeout, cancellation or
a failed phase never starts another one automatically. The configuration diagnostic
is reset for the new call; original Radio fault/cleanup diagnostics remain until
successful physical shutdown. Lease identity exhaustion rejects before retry writes.

`wifi.stop()` or runtime cleanup can instead consume the unfinished cleanup suffix.
Successful cleanup releases the snapshot and deinitializes the driver; after that,
restart uses the resulting clean source and cannot recover the discarded predecessor. If `restartRequired`
is true, device reboot may still be needed. OOM while converting a successful
result to JS can occur after the driver is already running: inspect `wifi.status()`
before another action, rather than assuming that every exception means no change.

## Country details

`wifi.driver.setCountryDetails(details)` takes one complete country object and
returns `WiFiCountryStatus` from actual SDK readback. It requires initialized,
fully stopped Wi-Fi, with every owner, operation, lifecycle, wake lock and
temporary-rate restoration obligation released and no fault or cleanup pending.
Station, SoftAP, APSTA and off modes are eligible. Callers explicitly stop their
resources before this operation; it does not initialize or stop the driver.

Required fields are `code` (two uppercase ASCII letters or `"01"`), `startChannel`
and `channelCount` (integers 1..14, with the last channel at most 14). Optional
`policy` is `"auto"` (default) or `"manual"`; `environment` is `null` (default),
`"indoor"`, `"outdoor"` or the SDK's literal third octet `"X"`. On a 5 GHz target,
`ghz5ChannelMask` is an optional unsigned 32-bit SDK channel mask, default zero;
unknown bits are rejected and a nonzero mask requires manual policy. Other
targets reject this field. Unknown keys and `maxTxPowerDbm` input are rejected.

This is the SDK's explicit-details operation. It validates representation and
target support; the SDK does not validate a supplied range against each country's
rules. Callers provide the appropriate local configuration. The code-only
`wifi.setCountry()` uses SDK country defaults and has its own started-Station
admission. Auto policy permits adopting a connected AP's country on a later
connection. The returned `maxTxPowerDbm` is the SDK's read-only observation.

Country setters persist independently of RAM/FLASH storage. A semantic no-op
reads the existing settings without writing. A changed request snapshots the
previous settings, applies and verifies the new ones, and attempts rollback on
write/readback failure. Rollback also checks the previous read-only power value;
an unconfirmed rollback faults Radio and retains cleanup. Country writes
invalidate prior STOP observations used by `restart()`; they do not invent a
recoverable restart source.

Errors use `WIFI_DRIVER_WRITE_FAILED`, operation `wifi.driver.setCountryDetails`,
interface `null`, original `espCode`/`stage`, `mutationAttempted`,
`persistentMutationPossible` and separate rollback fields. The same record is
available in `wifi.status().radio.configuration`. Readback and successful rollback
prove only local SDK settings, not durable NVS restoration. OOM delivering a
successful result can occur after mutation; inspect the current country before
another action. Runtime, fault-injection and RF qualification are pending the
Wi-Fi stage tests.

## PMF control

`wifi.driver.disablePmf(interface)` takes exactly one `"station"` or
`"access-point"` string and returns `undefined`. It explicitly removes protected
management frame capability and the required flag from that interface's existing
SDK configuration. Call it after configuration and before START. It requires an
initialized, fully stopped, zero-owner Radio, without lifecycle, operation, wake,
promiscuous, temporary-rate, fault or cleanup obligations. AP requires SoftAP
support; inactive interface configuration can be selected too.

The current configuration must use a reviewed non-WPA3 family: open, WEP,
WPA/WPA2 personal (including their mixed mode), WPA or WPA2 enterprise. Station
must have `disable_wpa3_compatible_mode=true`; AP must have
`wpa3_compatible_mode=false`. WPA3, WPA2/WPA3 transitions, OWE, DPP, WAPI and
unknown/reserved security encodings are rejected before mutation. This operation
does not configure enterprise credentials or enable other authentication features.
It never silently changes authentication or transition policy to make disabling
PMF possible. The explicit call can clear `required` on an eligible configuration.

The dedicated SDK API is used: writing `pmf.capable=false` in a config alone is
not an equivalent operation. Both flags and all other configuration fields are
verified afterward. An already disabled eligible config needs only its initial
read. Success has no JS result allocation, and temporary credential copies are
securely erased on every exit. This is configuration readback, not RF proof.

Errors use `WIFI_DRIVER_WRITE_FAILED`, operation `wifi.driver.disablePmf`, the
selected interface and raw SDK code. Stages are `pmf-admission`, `pmf-snapshot`,
`pmf-security`, `pmf-write` and `pmf-readback`. A failed write/readback faults the
Radio: a flag or persistent prefix may already have changed. There is no automatic
retry or guessed rollback. `persistentMutationPossible` is true for an attempted
write under FLASH storage, including failure; RAM readback cannot prove NVS state.

Later explicit configuration can re-enable PMF; disabling is not a permanent
override of future requests. Internal restart replay and configuration rollback
preserve an SDK-observed disabled predecessor by calling the dedicated API when
restoring config has re-enabled PMF, before their normal full readback. Restoration
uses RAM storage and preserves the existing failure/cleanup rules.
`startAP({ pmf: "disabled" })` also uses the dedicated API inside its stopped
configuration transaction, with its documented security conditions. Raw `wifi.configure()` Station/AP configs also accept the three-state `pmf`
policy and execute the same stopped transaction. Direct Station connect also uses
the stopped PMF transaction. The Candidate restart above preserves the captured
PMF state; complete recovery and runtime qualification remain pending.

## Antenna observations

`wifi.driver.getAntenna()` and `wifi.driver.getAntennaGpio()` take no arguments.
They require an initialized, stable Radio without an operation, lifecycle,
fault or cleanup transition. They snapshot the **SDK-stored shared PHY
configuration**, using `esp_phy_get_ant()` and `esp_phy_get_ant_gpio()`. They do
not initialize Wi-Fi, acquire owners, reserve or configure GPIOs, change RF
settings, or establish a board's external-switch wiring.

`getAntenna()` returns all five public fields as `WiFiAntennaConfig`: `rxMode`
and `txMode` (`"ant0"`, `"ant1"`, `"auto"`), `rxDefault` (`"ant0"`, `"ant1"`), and
`enabledAnt0`/`enabledAnt1` (raw four-bit selection values, 0..15). These selectors
are not GPIO numbers and are not limited to four GPIO-array indices. Unknown
enum values and the SDK-invalid TX-auto/RX-fixed combination fail decoding.
The fixed SDK stores TX `auto` when RX is also `auto`, but its PHY update code
does not implement automatic TX selection in that branch; a stored `auto`
observation is not a promise of effective automatic TX behavior.

`getAntennaGpio()` returns `{ gpios: [...] }` with exactly four entries in SDK
switch-line order, each `{ selected: boolean, gpio: number }`. GPIO is the raw
seven-bit value (0..127), including for disabled entries. It is not a claim that
the pin exists on this target, is free, or is currently routed as recorded.
The two getters are independent observations, not a combined atomic snapshot,
and neither is an RF measurement or proof of applied/pending PHY state.

Errors use `WIFI_DRIVER_READ_FAILED` with the exact operation, interface `null`,
raw SDK code, and `admission`, `antenna`, `antenna-gpio` or `decode` stage. Native
partial output is discarded on failure. JS conversion uses rooted bounded
objects/arrays; failed allocation has no native state to clean up.

`wifi.driver.setAntenna(config)` and `wifi.driver.setAntennaGpio(config)` each require one complete
configuration with the same fields as their getter. Unknown keys, missing
fields, fractional/out-of-range numbers, non-boolean selections and invalid
mode combinations are rejected. Every property is captured once and the result
object is allocated before native mutation; the result is a separate normalized
copy, returned only after native readback succeeds.

Both writes require initialized, fully stopped Wi-Fi with every Radio owner,
wake lock and operation released, and a fully closed BLE adapter/controller.
They do not stop another modem implicitly. The transaction holds the SDK PHY
access lock, rejects an awake modem, and checks logical BLE ownership as well:
modem sleep is not equivalent to a closed controller. When IEEE 802.15.4 is built
in, its native radio must also be disabled (idle/sleep are still owned states).
The antenna selection is
stored in shared PHY RAM and applied on the next PHY enable; it is not an NVS
write or proof of physical antenna switching. The TX-auto limitation above
also applies to a successful setter.

Selected GPIO entries must name distinct, output-capable pins. A new pin must
be unreserved, configured as a disabled GPIO, and have no input consumer,
interrupt, wake, hold, active RTC/LP mux or active USB pad. Release a pin from
its previous peripheral and configure/reset it as a disabled GPIO first.
Already owned antenna pins can be reused or reordered. Native stored routes
created outside this API, or a pin changed externally since this API acquired
it, are rejected without overwriting that route. Unselected entries preserve
their raw 0..127 GPIO values and do not claim those pins.

The framework atomically reserves new pins, saves their original pad/output
routes, verifies the new SDK configuration and output signal routing, and
restores pins removed by a replacement configuration. Four unselected entries
release all owned pins and the bounded control-budget allocation. This shared
PHY configuration and its pin ownership survive Wi-Fi deinit and JS runtime
restart; `wifi.stop()` does not release the antenna configuration.

Errors use `WIFI_DRIVER_WRITE_FAILED`, interface `null`, with the original stage,
SDK error and rollback details, also recorded in `wifi.status().radio.configuration`.
A failed write restores the verified prior configuration/routes. If rollback
cannot be confirmed, reservations remain held and both Wi-Fi initialization and
BLE open require a **device reboot** (`antenna-device-restart-required` /
`BLE_PHY_RESTART_REQUIRED`); `driver.restart()` or JS runtime restart cannot
repair that state. These APIs are Candidate pending the collective runtime and
hardware tests.

## Mode selection

`wifi.driver.setMode(mode)` accepts exactly one of the mode strings returned by
`getMode()` and `wifi.status().radio.mode`: `"off"`, `"station"`, `"softAP"` or
`"station+softAP"`. It requires an initialized, fully stopped, zero-owner Radio,
with no lifecycle, operation, wake, promiscuous, temporary-rate or cleanup
obligation. AP modes require the SoftAP build feature. NAN remains part of its
separate lifecycle work; no raw NAN mode is exposed through this setter.

The setter does not start Wi-Fi or create interface helpers. `"off"` selects
SDK mode NULL while retaining the initialized driver. Later `wifi.start()` or
`wifi.configure()` still performs its own interface/configuration admission.
Unlike the input options of those methods, this Driver method uses the existing
Radio observation strings above; it does not accept `"none"`, `"ap"` or `"apsta"`.

The current SDK mode must agree with the framework before a write is attempted.
An already matching mode is returned after that read, without another write.
Otherwise the setter writes and reads back the requested mode. SDK error or
mismatch triggers rollback to the verified predecessor and another readback.
Successful runtime rollback does not prove NVS rollback; a failed FLASH-policy
attempt retains a fault even if runtime mode was restored. Failed rollback
records `mode-rollback` cleanup and blocks further mutation. An inconsistent or
unknown initial mode faults at `mode-snapshot` without attempting a write.

Success returns the rooted input after verification, without allocating a result
after mutation. Failures use `WIFI_DRIVER_WRITE_FAILED`, operation
`wifi.driver.setMode`, interface `null`, and the original error plus separate
rollback stage/error. After failure, inspect those diagnostics before relying
on the cached Radio mode. Coercion, NUL suffixes and extra arguments are rejected
before SDK calls. No live AP activation, disconnect or RF continuity is implied.

## Storage selection

`wifi.driver.setStorage("ram" | "flash")` selects the storage policy for future
SDK configuration writes and returns the accepted string. It requires an
initialized, fully stopped Radio with no owner, operation, lifecycle, wake lock,
promiscuous claim or pending temporary rate restore. It does not initialize or
stop Wi-Fi, flush RAM configuration to flash, or copy/clear credentials.

The SDK has no storage getter. `wifi.status().radio.storage` reports the last
accepted selection, not NVS readback. On SDK failure it becomes `null`, with
`initialized: false`, `driverOwned: true` and the original `storage-write` fault;
other mutations remain blocked. An explicit accepted `setStorage()` replacement
can repair this specific fault when the same stopped/exclusive requirements
hold. It cannot clear an unrelated fault or pending cleanup. SDK acceptance
does not prove persistence of any configuration written later.

Arguments are exactly one lowercase string; coercion, embedded NUL suffixes and
extra arguments are rejected before the SDK call. Success returns the rooted
input without allocating a JS result after mutation. Failure uses
`WIFI_DRIVER_WRITE_FAILED`, operation `wifi.driver.setStorage`, interface `null`,
the raw SDK error and `mutationAttempted`; no rollback is claimed.

`clearFastConnect()` is not exposed: the fixed SDK documents it as a stub, and
the inspected C5 implementation only checks initialization and returns. There
is no cache-clearing effect to report as success.

## Observations

The following getters return live SDK observations. They require an initialized,
stable driver, share the Radio mutation mutex, reject lifecycle/operation/fault/
cleanup transitions, and never initialize Wi-Fi or acquire an owner.

| Method | Result and additional state requirement |
| --- | --- |
| `wifi.driver.getBand()` | `"2.4GHz"` or `"5GHz"`, current band |
| `wifi.driver.getBandMode()` | `"2.4GHz-only"`, `"5GHz-only"` or `"auto"`, configured band policy |
| `wifi.driver.getPowerSave()` | `"none"`, `"minimum"` or `"maximum"`, SDK power-save configuration |
| `wifi.driver.getTxPower()` | Configured maximum TX power in dBm, 0.25 dBm resolution; driver started |
| `wifi.driver.getRssi()` | Most recent beacon RSSI in dBm; started Station, requires association |
| `wifi.driver.getAid()` | Current Station AID; Station enabled, SDK returns 0 when not associated |
| `wifi.driver.getNegotiatedPhy()` | `WiFiNegotiatedPhy`; started and associated Station |
| `wifi.driver.getTsfTime(interface)` | SDK TSF microseconds; requested interface enabled and driver started |
| `wifi.driver.getInactiveTime(interface)` | Current inactivity configuration in seconds; requested interface enabled and started |

The first seven accept no arguments; the last two require exactly one `station`
or `access-point` string. AP requires the SoftAP build feature. The SDK is read on
2.4 GHz targets too; the framework does not manufacture a constant band response.
Unknown or target-inconsistent enum values fail decoding. The RSSI, AID and PHY
calls are independent observations, not one atomic connection snapshot and not
identities suitable for completing a connect operation or targeting a client.
Native association and beacon state can change independently of framework calls.

TSF `0` preserves the SDK's unavailable sentinel: Station has not associated or
has not received its first beacon. TSF is not UTC, does not share an epoch with
host time, and must not be compared across restarts/associations without an explicit
clock contract. Power save can affect its accuracy as described by the SDK.
Negative TSF or values above 9,007,199,254,740,991 fail with `decode`, avoiding
silent integer rounding. Reading TSF does not calibrate the RX wire timestamp.

These methods use `WIFI_DRIVER_READ_FAILED`, with original `espCode`, `stage` and
`interface` in details. The interface is null for Radio-wide observations, Station
for link getters, and the supplied interface for TSF/inactive time. SDK stages are
`band`, `band-mode`, `power-save`, `tx-power`, `rssi`, `aid`, `negotiated-phy`,
`tsf-time` and `inactive-time`; framework stages are `admission` and `decode`.
Partial SDK output is discarded on failure. Allocation failure during conversion
does not change native configuration. Band setters have the stricter admission rules below.

`wifi.driver.setDynamicCarrierSense(enabled)`,
`wifi.driver.configure11bRate(interface, disabled)` and
`wifi.driver.setCoexistencePowerManagement(enabled)` accept strict boolean values
and return the boolean accepted by the SDK. Numbers, strings and missing/extra
arguments are rejected. `configure11bRate` accepts only `"station"` or
`"access-point"`; `true` disables 11b rates and `false` enables them. This rate
control is separate from the protocol bitmap and explicit TX-rate configuration.

| Control | Required state |
| --- | --- |
| Dynamic carrier sense | Started driver with exact framework Application/Station/AP owners |
| 11b rate | Initialized, fully stopped driver, zero live owners; requested interface enabled; AP requires SoftAP support |
| Coexistence power management | `CONFIG_ESP_COEX_POWER_MANAGEMENT`; initialized and fully stopped with zero owners, or started with exact framework owners |

Every path requires idle Wi-Fi helpers, a healthy Radio and no lifecycle/operation
transition or pending cleanup. Started controls reject unrelated feature owners,
wake locks, promiscuous capture and temporary TX-rate leases. Stopped controls also
reject pending native stop work. They share the Radio mutation mutex and do not
initialize, stop, disconnect, retire other owners or change interface mode.

These SDK controls have **no public getter**. Each explicit call performs one SDK
write, including a repeated value; a successful `false` return is acceptance of
`false`, not failure. No readback, observed current value, deduplication, known SDK
default or reversible temporary lease is invented. On an SDK error, policy may
already have changed; Radio retains the original error and faults without a guessed
rollback or automatic replay. Device restart is currently required for this fault.
Write-state revision tracking is exposed in `wifi.status().radio.policies`.
The Candidate `wifi.driver.restart()` above captures known boolean policy records
and replays them before/after start for an admitted predecessor. Recovery is
limited to the explicit sources described above; uncertain policy writes reject
new-source restart. Runtime/RF qualification remains pending. See
[atomic admission](../investigations/2026-09-09-w07-restart-admission.md). Ordinary
deinit outside this restart does not replay these records. `wifi.status().radio.restartSnapshotBytes` reports native
Station/AP credential and global-setting checkpoint storage retained by that internal lifecycle,
including after a failed restoration, and is zero after successful handoff or
explicit cleanup. It never exposes the checkpoint contents and excludes fixed
control state. This is not a whole-driver memory total. The internal checkpoint
also captures country, interface MACs, power-save mode and the safe event mask;
replay and final readback precede owner publication. Country maximum TX power is
read-only; exact quarter-dBm power is separately captured before shutdown and
restored/read back after START, before owner publication. A stopped driver without
qualified history obtains actual runtime observations through the documented
source START before deinit; unreadable required values reject capture.
This does not guarantee an RF power ceiling between SDK START and its setter.
See [TX power restoration](../investigations/2026-09-09-w07-restart-tx-power.md).
Connectionless interval also restores a frozen, unborrowed accepted value using
the sole writer; final verification checks its accepted revision, with no SDK
getter or RF timing claim. See [interval restoration](../investigations/2026-09-09-w07-restart-interval.md).
TX rate also replays frozen accepted Station/AP records before START; absent or
uncertain values are not replaced with guessed defaults. Country/PHY sources
that cannot be captured safely reject restart. Internal PHY checkpoint/replay now
captures the visible predecessor, temporarily enables Station/AUTO when needed
to read hidden bands, then restores all target PHY fields and the original band
mode. Temporary cycles wait for SDK STOP/START and a matching queue marker
(`radio.eventPhase:"restart"`); no owner is published during preparation.
Final AP restoration starts AP-only with the saved configuration and uses CSA to
restore the saved actual channel, waiting for band/current/home equality. APSTA
adds Station afterward with a separate `sta-start` barrier. It does not update
live AP configuration, which the pinned SDK can implement as AP STOP/START.
RAM storage remains selected through START and final verification; only then is
the original storage policy restored and the owner handed back. Failures retain
the checkpoint and lifecycle obligation. The Candidate public entry uses this
helper coordinator; full recovery and target/RF verification remain pending. See [AP activation](../investigations/2026-09-09-w07-ap-activation.md)
and [storage commit](../investigations/2026-09-09-w07-restart-storage-commit.md).

The snapshot has `revision`, `identityExhausted`, and four records named
`dynamicCarrierSense`, `station11bDisabled`, `accessPoint11bDisabled`, and
`coexistencePowerManagement`. Every SDK attempt receives a unique, non-wrapping
revision across all records. Repeated requests still perform an SDK call. Input,
feature and owner rejection do not consume a revision. Exhaustion fails with
`policy-record-admission` before an SDK call and does not itself fault the driver.

Each record contains `generation` and `revision` for its last SDK attempt,
`acceptedRevision`, `configured` (at least one accepted write), `known`,
`uncertain`, `requested`, `value`, `lastAcceptedValue`, and the original `error`.
Before any attempt the revisions/generation are zero, requested/values are null,
and flags are false. A successful false write is a known false value, not missing
data. SDK failure retains the last accepted value and its revision as history,
sets `uncertain`, and exposes `value: null`. This includes a setter that changed
native state before reporting an error; there is no automatic rollback.

Successful physical deinit clears known/uncertain and makes `value` null, while
retaining historical values, errors and revisions. Failed deinit does not retire
the record. `known` describes a same-driver-generation accepted write, not SDK
readback, RF behavior, or proof that policy survives stop/start, mode or protocol
changes. Historical values alone do not authorize restoration. The status is one
mutex snapshot, independent of the other Radio status sections. Four record slots
do not imply four available capabilities: disabled features remain unconfigured.
This bounded ledger contains no credentials, heap storage, or retained JS roots.

Coexistence power management is a global coexistence policy, not a per-interface
power-save mode or wake lock; RF/power behavior can affect coexistence participants.
The pinned Kconfig defaults the option off and requires software coexistence for
it. The current C5 representative context has it disabled, so that method returns
NOT_SUPPORTED without an SDK write. Enabling the compile gate does not establish
coexistence hardware qualification. No capability claim is inferred from this
method's registration.

Errors use `WIFI_DRIVER_WRITE_FAILED`; `details.interface` is the requested
interface for 11b and `null` for the other two. Stages are `helper-admission`,
`policy-admission`, `dynamic-cs-write`, `11b-rate-write` or
`coexistence-power-write`. Radio results update `wifi.status().radio.configuration`.
JS error allocation may fail after the native call; the native fault/transaction
record is retained. Runtime restart is not physical recovery.

`wifi.driver.setConnectionlessWakeInterval(milliseconds)` accepts one integer
0..65535; 0 explicitly requests the SDK default mode. It returns the accepted
value, not a measurement or SDK readback. Radio must already be initialized and
stable: fully stopped with zero owners, or started with only the exact framework
Station/AP/application owners and idle helpers. Live ESP-NOW, Monitor, CSI, Raw TX,
wake locks and lifecycle/native operations exclude this write. No implicit
initialization, stop or disconnect is performed.

This interval is shared by all connectionless modules. Initialization explicitly
writes default mode after RAM storage is configured; only SDK acceptance establishes
a baseline. ESP-NOW power save borrows it with an exact generation/Radio-owner/token
identity, retains the original baseline across updates, and restores it on disable
or close. A live borrowing token prevents new Radio/wake leases and cannot be
retired through ordinary Radio release. SDK errors retain the restoration duty.

`wifi.status().radio.connectionlessInterval` reports `generation`, non-wrapping
`revision`, `known`, `uncertain`, `milliseconds` (null unless known),
`previousMilliseconds` (null unless borrowed), `ownerIdentity`, `tokenIdentity`,
`restorePending`, `error` and `restoreError`. These fields record accepted writes,
not driver readback or effective RF power savings; status sections are independent
snapshots. IDs are zero when unowned. Revisions survive physical deinitialization.

Write failures use `WIFI_DRIVER_WRITE_FAILED`, null `details.interface`, and
`helper-admission`, `interval-admission` or `interval-write` stage. Failed writes
make interval knowledge uncertain; an explicit unowned replacement can establish
it again. Borrowed uncertain state must restore its captured value first. Neither
runtime restart nor a guessed default proves restoration. Exhausted revisions
cannot wrap/reuse and may require a physical device reboot to release a failed
restoration. SDK default mode and effective sleep behavior depend on the build;
disconnected Station operation requires its SDK power-management feature.

`wifi.driver.getEventMask()` returns the raw SDK unsigned 32-bit event mask.
`wifi.driver.setEventMask(mask)` accepts exactly one integer, `0` or `1`, and
returns the confirmed mask. In the pinned SDK, `0` enables delivery of all events
and bit 0 (`1`) suppresses AP probe-request events. Bits are **SDK mask constants**,
not `1 << WIFI_EVENT_*` event IDs. The default SDK mask is `1`.

The SDK's `MASK_ALL` (`4294967295`) and every other unknown/reserved bit are rejected
before any SDK call. This prevents the public setter from suppressing events needed
for internal Future/lifecycle completion. The getter preserves unknown bits for
diagnosis; it does not certify that a raw mask is safe to write back.

Both methods require a healthy, initialized driver in stable started/stopped state,
with no lifecycle/Radio operation transition or pending fault/cleanup. Radio owns
the global setting and serializes access through its mutation mutex. Because the
only writable bit controls probe observations, existing RF owners, wake locks and
watch subscribers do not block it. No interface, implicit start/stop or new lease
is involved. This is a persistent setting within the current driver lifetime, not
a per-watch lease; closing a watcher does not restore it. The admitted Driver
restart captures and replays the safe event mask before owner publication.

The setter reads the previous mask first. A same-value request returns without a
write. Otherwise it writes once and verifies readback. On failure, a previously
safe `0`/`1` is restored and verified, preserving the original error separately
from any rollback error. Restoring the value cannot recover suppressed observations
or retract already queued probe events. If the previous mask contains unsafe bits,
an explicit safe replacement is allowed, but failure never replays that old mask.
Unverified restoration faults Radio; there is no background retry or implicit
physical recovery. Device restart is currently required for such a fault.

`wifi.watch({events})` remains the filter for an individual subscription. Enabling
probe delivery can increase ingress traffic; the existing bounded watch queue and
drop counters remain in effect. Internal control completion is recorded before
observation delivery and does not depend on a watch queue accepting an event.
No promise of lossless probe delivery or replay of masked events is made.

Read errors use `WIFI_DRIVER_READ_FAILED`; write errors use
`WIFI_DRIVER_WRITE_FAILED`. Both have `details.interface: null`. Stages include
`admission`, `event-mask-read`, `event-mask-write`, `event-mask-readback`,
`rollback-event-mask` and `rollback-event-mask-readback`. Write results update
`wifi.status().radio.configuration`; ordinary reads leave that transaction record
alone. JS result/error allocation can fail after a write; inspect native diagnostics
and `getEventMask()` when admission permits before retrying.

`wifi.driver.getMode()`, `getCountry()`, `getChannel()` and `getHomeChannel()`
accept no arguments. They require a healthy, initialized driver in stable stopped
or started state; `getHomeChannel()` additionally requires started state. They
share the Radio mutation mutex, reject lifecycle/operation transitions and pending
fault/cleanup, and neither initialize the driver nor acquire an owner.

| Method | Result and meaning |
| --- | --- |
| `getMode()` | SDK mode: `"off"`, `"station"`, `"softAP"` or `"station+softAP"`; `off` means SDK NULL mode, not proof of deinitialization |
| `getCountry()` | `WiFiCountryStatus`, the current local country configuration; not an AP scan record |
| `getChannel()` | `WiFiChannelStatus`, current channel and callback-aware observation revision |
| `getHomeChannel()` | Same channel fields, with `channelGeneration: null`; direct SDK home-channel observation |

Mode decoding rejects unknown modes and AP modes when SoftAP is not built. NAN
mode remains outside the current framework lifecycle contract. Country decoding
checks code, environment encoding, policy and 2.4 GHz channel bounds. It preserves
the SDK TX-power field and raw 5 GHz mask, with `null` for that mask on 2.4 GHz-only
targets. Zero or an automatic-policy mask does not establish an authoritative
list of allowed channels. `environment: null` covers SDK unspecified encodings
(NUL, space); `I`/`O` map to indoor/outdoor and `X` remains the literal `"X"`. This read does not change
regulatory policy or validate local compliance.

Current and home channel can differ during off-channel activity such as scanning.
Current-channel reads reuse the existing callback-aware refresh; if a newer refresh
finishes during an older SDK read, the newer observation wins. This can update the
current-channel cache and mark a fixed-channel owner's conflict, but performs no
RF write. `channelGeneration` is an observation revision, not an operation identity
or timestamp; unchanged samples keep it, and callers must not infer missing RF
history from it. Home reads never update that cache or borrow its revision.
Separate method calls are not an atomic Radio snapshot. A stopped current-channel
read can fail if the SDK has no valid channel; no default channel is invented.

Unknown channel/secondary encodings or target-unsupported channels fail decoding.
A channel read reports an observation without an additional country query; it does
not claim the sampled channel is legal or stable for a later operation. Errors
reuse `WIFI_DRIVER_READ_FAILED` with `details.interface: null`; stages are
`admission`, `mode`, `country`, `channel`, `home-channel` or `decode`. SDK errors are
preserved, partial native output is discarded, and failed JS conversion does not
change native configuration. Country conversion is shared with the existing scan
and country setter results, using rooted JS object construction.

`wifi.driver.setBand(band)` accepts exactly one `"2.4GHz"` or `"5GHz"` string.
`wifi.driver.setBandMode(mode)` accepts exactly one `"2.4GHz-only"`, `"5GHz-only"`
or `"auto"` string. They return the confirmed band or mode. Unknown names, case
variants, embedded NUL suffixes and non-string values fail before SDK mutation.
5 GHz and AUTO requests require a 5 GHz target. `setBand` additionally requires
current AUTO mode, including a same-value request; use `setChannel` when the
intention is to choose a particular channel, as recommended by the pinned SDK.

Both setters require a healthy, started **Station-only** driver and the exact
framework Application/Station owners. AP must be explicitly stopped first.
Scanning, connecting, draining helpers, lifecycle transitions, unrelated feature
owners, fixed-channel leases, wake locks, promiscuous capture and temporary rate
leases reject admission. An AP client count snapshot cannot prevent a new client
association between the check and band mutation, so live AP/APSTA switching is
not provided by this contract. There is no implicit initialization, disconnect,
AP stop or owner retirement.

After reading the current band and mode, a same-value request returns without a
write; this is allowed on an associated Station. An actual change also requires
the SDK to report Station not associated. Switching band mode can select a saved
home channel or SDK defaults (channel 1 for 2.4 GHz, 36 for 5 GHz). After a write,
the framework reads band/mode and the callback-aware current channel, checks
consistency and the current regulatory constraints. These are point-in-time
observations; they do not establish RF qualification or undo a transient channel
change. The SDK remains responsible for enforcing its regulatory configuration.

A setter error or failed post-write readback faults Radio and retains the original
error in `wifi.status().radio.configuration`. Hidden inactive-band state cannot
be fully captured through the public SDK, so no rollback or automatic replay is
attempted. This fault is outside defaults recovery and new-source restart
admission; device reboot is required. JS runtime restart
is not physical recovery. `persistentMutationPossible` records FLASH storage;
it does not assert whether an unsuccessful SDK call persisted a change.

Errors use `WIFI_DRIVER_WRITE_FAILED`, `details.interface: "station"`, and stages
`helper-admission`, `band-admission`, `band-snapshot`, `band-station-link`,
`band-write`, `band-mode-write`, `band-readback`, `band-channel-readback`, or
`band-channel-regulatory`. Result/error allocation can itself fail after a native
write; this cannot undo the write, so inspect Radio diagnostics before retrying.
Band getters remain available under their separate, wider read admission rules.

`wifi.driver.setInactiveTime(interface, seconds)` changes the started interface's
inactivity threshold: Station accepts integer 3..65535 seconds, AP 10..65535.
Station may disconnect after missing beacons for this period; AP may deauthenticate
clients after receiving no data for this period. With FLASH storage, the pinned
SDK can attempt an NVS write even though its header describes this setting as
non-persistent. `persistentMutationPossible` records that possibility as soon as
the write is attempted, including SDK failure and rollback paths. With RAM
storage, it is false. The method preserves the selected storage policy.
The method requires the target helper's exact live Radio lease, matching existing
application/Station/AP leases, no other feature owner, wake lock, promiscuous
capture or temporary rate lease, and no helper/Radio operation or cleanup pending.
It supports the framework's shared APSTA configuration without stopping either
interface. AP is gated by the SoftAP build feature. It never initializes a helper
or driver implicitly.

The method reads the old value, writes the request, and verifies readback under
one Radio mutation lock. On failure after a write attempt it restores and verifies
the old value. Success returns the observed seconds; failure uses
`WIFI_DRIVER_WRITE_FAILED` with the same transaction details as the PHY setters.
`rollbackComplete` means the running threshold value was restored; it does not
prove NVS restoration or undo a disconnect or client deauthentication that
occurred in the meantime. Failed
rollback retains a Radio cleanup fault and blocks new owners. No automatic
reconnect, client resurrection or force-reset is performed by the setter.

`wifi.driver.setRssiThreshold(dbm)` accepts an integer from -100 through 10 dBm.
The SDK emits `WIFI_EVENT_STA_BSS_RSSI_LOW` when average RSSI falls below the
threshold. Receive it through `wifi.watch()`'s existing Station event, with
`data.rssi`. To request another notification after one fires, call this setter
again explicitly; the same value is valid and is sent again. No automatic rearm
or retry is performed. A saturated watch queue can drop an observation and does
not cause the framework to rearm the SDK.

RSSI threshold is an observer control and can coexist with unrelated RF owners
and wake locks. A running Station requires its exact helper lease and any existing
framework application/AP leases must match; initialized, stopped Station mode
also permits configuration without a helper. Helper and Radio transitions or
faults still reject the call. It does not connect, disconnect or change RF policy.
Success returns the accepted dBm value. The SDK has no public threshold/armed-state
getter; acceptance does not prove the notification is still armed when JS resumes.
A failed SDK write may already have applied the threshold, so the error records
`mutationAttempted:true`, performs no invented rollback, and leaves RF ownership
intact. Another explicit call is a new rearm request.

`wifi.status().radio.rssiThreshold` records the last explicit SDK request:

| Field | Meaning |
| --- | --- |
| `revision` | Boot-scoped SDK attempt number; starts at zero and never wraps, including across driver reconstruction. |
| `identityExhausted` | No further request identity can be allocated. |
| `generation` | Physical driver generation of the last attempt, or null before any attempt. |
| `generationActive` | That request belongs to the currently owned physical generation; this does **not** mean armed, connected or started. |
| `requestedDbm` | Last requested threshold, or null before any attempt. |
| `accepted` | Last SDK call returned `ESP_OK`; false before the first attempt. This remains historical after deinit. |
| `espCode`, `espName` | Last SDK result, or null before any attempt. |

Every SDK attempt consumes one revision, including a failed write and repeated
values. Input/helper/owner rejection consumes none and preserves the previous
record. At revision exhaustion, `rssi-threshold-identity` fails before mutation
with `ESP_ERR_NO_MEM`. Status takes a native snapshot under the Radio mutex,
without a driver call; converting it to JS cannot rearm or change the record.

RSSI notification requests are excluded from automatic restart replay. Physical
reconstruction leaves the prior request as history with `generationActive:false`;
request new notifications explicitly with `setRssiThreshold()` after the desired
Station mode is ready. Ordinary STOP/START in the same generation does not provide
armed-state evidence either. A JS runtime change alone is not the definition of a
new physical generation, and does not reset the boot-scoped revision.

Already queued low-RSSI observations can still be delivered. They carry no request
cookie, so the framework does not attribute them to `revision`, mark a request as
consumed, or promise cancellation of queued delivery. A failed write can also have
changed the SDK threshold; `accepted:false` is not proof that no event can occur.
The status record is diagnostic history, not a subscription or a persistent
configuration value. See [RSSI request semantics](../investigations/2026-09-09-w07-rssi-request.md).

Both controls capture and validate all arguments before SDK mutation and reuse
`WIFI_DRIVER_WRITE_FAILED`. Stages include `helper-admission`, `control-admission`,
`inactive-time-snapshot`, `inactive-time-write`, `inactive-time-readback`,
`rollback-inactive-time`, `rollback-inactive-time-readback`, and
`rssi-threshold-identity`, and `rssi-threshold-write`. Radio-level results
are retained in `wifi.status().radio.configuration`; no threshold JS object,
callback cookie or background restore/rearm job is retained. Runtime/queue/RF
behavior remains pending the Wi-Fi stage tests.

`wifi.driver.getProtocol(interface)`, `wifi.driver.getProtocols(interface)`,
`wifi.driver.getBandwidth(interface)` and `wifi.driver.getBandwidths(interface)`
read the SDK's configured PHY settings. They require
an initialized, stable driver and an interface enabled by its mode. Running and
fully stopped states are accepted. Reads share the Radio mutation mutex and
reject lifecycle/operation transitions, faults and pending cleanup. They acquire
no owner, initialize nothing and leave configuration untouched.

| Method | Result |
| --- | --- |
| `getProtocol` | Configured `WiFiProtocol[]`, with exact SDK flags mapped to names |
| `getProtocols` | `{ ghz2?: WiFiProtocol[], ghz5?: WiFiProtocol[] }` |
| `getBandwidth` | Configured MHz, `20` or `40` |
| `getBandwidths` | `{ ghz2MHz?: 20 \| 40, ghz5MHz?: 20 \| 40 }` |

The singular SDK getters reject dual-band AUTO mode; use the plural methods in
that mode. Plural reads query band mode and the SDK getter in the same mutation
lock. Inactive bands are omitted, including 5 GHz on a 2.4 GHz target; untouched
SDK output storage is never reported as a default configuration. On 2.4 GHz-only
targets the plural methods adapt the singular SDK getters. These are configured
limits, not negotiated link PHY/bandwidth or over-the-air measurements. Separate
method calls are separate observations, not one atomic combined snapshot.

All four methods accept exactly one `station` or `access-point` string. AP requires
the SoftAP build feature. They throw `WIFI_DRIVER_READ_FAILED` with `operation`
and `details.{interface,espCode,stage}`; stages are `admission`, `band-mode`,
`protocol`, `protocols`, `bandwidth`, `bandwidths` or `decode`. SDK error codes are
preserved. Unknown protocol bits or bandwidth enums fail decoding instead of
silently dropping fields. Partial native readback is discarded on failure. JS
allocation failure has no native configuration side effect.

`wifi.driver.setProtocol(interface, protocols)` and
`wifi.driver.setProtocols(interface, config)` accept the same array/per-band shape
as their getters. Each supplied array is a nonempty, unique set of exact protocol
names. `wifi.driver.setBandwidth(interface, mhz)` and
`wifi.driver.setBandwidths(interface, config)` accept `20`/`40` MHz or a nonempty
per-band object. Every setter requires exactly two arguments. Unknown keys,
duplicates, sparse arrays, stringified numbers, non-integers and NUL suffixes are
rejected before any native write; original getter/allocation exceptions survive.

These setters require initialized, fully stopped Wi-Fi with the interface enabled
in its configured mode, zero Radio leases/wake locks/operations/lifecycle/capture,
and no fault, pending cleanup or temporary rate obligation. They do not stop or
disconnect other owners. The singular form addresses the current single-band
mode and rejects AUTO. Plural fields must refer to active target-supported bands;
inactive fields are rejected rather than silently ignored. Omitted active bands
retain their existing configuration. Protocol sets are complete sets, not the
SDK's maximum-protocol shorthand: for example use `["11b", "11g", "11n"]`.
Supported families and prerequisite bits follow the target SDK; AX requires HE
support. Changing protocols preserves bandwidth, and changing bandwidth preserves
protocols. A 40 MHz result requires 11n and excludes 11ac/11ax; incompatible requests
are rejected before writing. To leave HT40 for an AX configuration, explicitly
select 20 MHz first, or use one `wifi.configure()` transaction supplying both.

The transaction snapshots both settings for all active bands, validates that the
predecessor can be restored, writes through the shared stopped PHY transaction
helper, and verifies both settings by SDK readback. On success it returns the
corresponding getter shape, including omitted-but-preserved active bands for the
plural form. On failure after a write attempt it reapplies and verifies the old
settings; even successful rollback still throws the original error.

`WIFI_DRIVER_WRITE_FAILED` includes `operation` and
`details.{interface,espCode,stage,mutationAttempted,rollbackAttempted,rollbackComplete,
rollbackStage,rollbackError,persistentMutationPossible}`. `rollbackComplete` proves
runtime configuration restoration only. A failed rollback, or a failed Flash
storage transaction whose prior NVS image cannot be proved, faults Radio and
blocks new owners. These setters cannot bypass that fault. The retained native
transaction is also available in `wifi.status().radio.configuration`; no JS
configuration object is retained. Explicit physical shutdown/rebuild recovery is
a separate lifecycle operation; runtime restart alone is not a recovery guarantee.

Successful native writes precede JS result allocation. If conversion runs out of
memory, inspect the getters and Radio status before retrying. Setters operate on
configuration; RF behavior and persistence require their separate stage tests.

`wifi.driver.configureTxRate(interface, config)` explicitly sets the interface's
802.11 TX PHY/rate and returns `WiFiTxRateStatus`. Interfaces are `station` and
`access-point`; AP requires the SoftAP build feature. These are synchronous
controls. They neither initialize nor stop Wi-Fi. Before writing, the driver
must be initialized, fully stopped, free of all Radio leases, wake locks,
operations, promiscuous capture and lifecycle transitions. Release resources and
complete `wifi.stop()` first. Calling configure with `start:false` can still hold
an application owner; stopping releases that owner. Other-owner admission failure
performs no SDK rate write.

`config` has required `phy` and `rate`, plus strict boolean `ersu` and `dcm`
(default false). Unknown fields, embedded NUL suffixes, missing names and invalid
PHY/rate families fail before native mutation. Accepted families:

| PHY | Rate names | Target condition |
| --- | --- | --- |
| `lr` | `lr-250k`, `lr-500k` | LR enabled in this interface's 2.4 GHz protocol set |
| `11b` | `1m-long`, `2m-long`, `5.5m-long`, `11m-long`, `2m-short`, `5.5m-short`, `11m-short` | CCK |
| `11g` | `6m`, `9m`, `12m`, `18m`, `24m`, `36m`, `48m`, `54m` | OFDM |
| `11a` | OFDM names above | 5 GHz target support |
| `ht20`, `ht40` | `mcs0-long` / `mcs0-short` through `mcs7-long` / `mcs7-short` | HT |
| `he20` | MCS names through 9 | HE target support |
| `vht20` | MCS names available in the target SDK enum, through 9 | 5 GHz target support |

`ersu:true` or `dcm:true` requires `he20`. The SDK additionally validates current
band/protocol, PHY/rate and ERSU/DCM combinations. These names map exact SDK enum
constants; numeric values differ between targets. An accepted configuration does
not prove an over-the-air PHY, packet delivery or peer acknowledgment. This
control uses `esp_wifi_config_80211_tx`; it does not set ESP-NOW peer rates or the
private driver-wide fixed-rate control.

Stopped configuration uses the saved protocol set even after STOP has released
the SDK interface object. On a fresh dual-band AUTO driver, no current RF band
may exist yet. B/G/LR and A/VHT identify their validation band; common PHYs need
one enabled band or equivalent protocol contexts on both bands. Different
contexts or ambiguous HT40 require selecting a band through the normal started
Station controls, then stopping before the rate write. Validation does not start
RF or select a channel implicitly.

LR requires this interface's `lr` protocol flag; enabling LR on the other
interface is insufficient. Only `lr-250k`/`lr-500k` without ERSU/DCM are accepted,
and a selected 5 GHz band rejects LR. The pinned SDK bridge repairs its LR
validation using the interface's protocol getter and preserves the requested
configuration through the native writer. It does not enable LR automatically.

`wifi.driver.txRateStatus(interface)` returns a copied framework record without
SDK calls or initialization, including during Radio faults. Fields:

- `known`, `source:"framework-write" | null`, `config` (all four fields, or null).
- `radioGeneration`, `writeGeneration`, `writeIdentity` (zero if no retained write).
- `uncertain`, `error`, `rollbackError` (native error numbers or null).
- `temporaryLease`: null, or a same-mutex snapshot containing `radioGeneration`,
  `radioLeaseIdentity`, `writeIdentity`, `previous` (all config fields),
  `restorePending` and `restoreError`. This identifies a Raw TX Session's retained
  rate obligation, including a failed open with no JS Session object.

The SDK has no public rate getter. Initial rate is unknown. Knowledge records a
successful framework write or successful reapplication of a known previous value
in the same driver generation. Physical deinit clears records; stop alone keeps
them. Write identities are boot-monotonic and never wrap; exhaustion rejects
further writes before SDK entry. External native writes bypassing this boundary
invalidate the sole-writer premise and are unsupported.

A failed write attempts rollback only with a known same-generation predecessor.
The method still throws `WIFI_TX_RATE_FAILED` when rollback succeeds. The error
contains the record plus `espCode`, `stage` (`admission` / `write`), `attempted`,
`driverAccepted`, `rollbackAttempted`, `restored` and `operation`. An unknown
result or failed rollback sets `uncertain:true` and faults Radio with
`tx-rate-uncertain`, blocking new owners. An explicit configure call can repair
this rate fault while the driver remains fully stopped and owner-free; unrelated
faults cannot be bypassed. All uncertain interfaces must be repaired before Radio
admits owners again. Physical shutdown can also discard the configuration.

The returned record is constructed after mutation. If JS allocation fails, the
native write may already have succeeded: inspect `txRateStatus()` before retrying.
Rate is interface-global and remains set across ordinary stop/start.
[Raw TX Session rate leases](wifi-raw-tx.md#temporary-session-rate) can temporarily
replace a known Station or AP rate and restore it while stopped on close. Such a lease
blocks this direct setter until restoration finishes; the direct setter cannot
repair a Session's retained rate obligation. Internal restart now freezes known
Station/AP rates, replays them before START and verifies accepted identities before
owner publication. There is no native getter or RF rate proof. Public restart
uses the same coordinator; temporary AP rate leases stay owned by their Raw TX
Session until restoration completes. See
[restart rate implementation](../investigations/2026-09-09-w07-restart-rate.md).

```javascript
// Prerequisite: all child resources are closed. This may briefly start Wi-Fi.
wifi.start();
wifi.stop();
var rate = wifi.driver.configureTxRate("station", { phy: "11g", rate: "6m" });
print(rate.known, rate.source);
print(wifi.driver.txRateStatus("station").writeIdentity);
```

Availability is implementation status. Runtime, allocation-failure, concurrency
and RF qualification remain pending the Wi-Fi stage test phase.
