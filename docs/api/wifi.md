# `wifi` Module

Wi-Fi uses RAM storage by default. Explicit `wifi.configure({storage:"flash", ...})`
can persist configuration in NVS; inspect `wifi.status().radio.storage`.
`wifi` owns 802.11 Station and shared-radio controls. IP addresses, routes,
interface readiness, and later Ethernet/PPP state belong exclusively to
`net.status()` and `net.watch()`.

- `wifi.smartConfig` — Candidate credential acquisition Sessions; see [SmartConfig ownership and pending integration](wifi-smartconfig.md).
- `wifi.driver.capabilities()` / `wifi.driver.status()` / `wifi.driver.restore()` — build capabilities, shared Radio diagnostics and explicit stopped default reset; see [Driver discovery and restore](wifi-driver.md#driver-discovery-and-restore).
- `wifi.driver.configureTxRate(interface, config)` / `wifi.driver.txRateStatus(interface)`
- `wifi.driver.getInterfaceConfig(interface, options?)` / `wifi.driver.setInterfaceConfig(interface, config)` — full configuration snapshots and stopped writes; see [interface configuration](wifi-driver.md#interface-configuration).
- `wifi.driver.restart(options?)` — Candidate reconstruction of healthy initialized stopped Wi-Fi, with a temporary source START when STOP observations are unavailable; see [restart admission and failure handling](wifi-driver.md#driver-restart).
- `wifi.driver.getProtocol(interface)` / `wifi.driver.getProtocols(interface)`
- `wifi.driver.getBandwidth(interface)` / `wifi.driver.getBandwidths(interface)`
- `wifi.driver.setProtocol(interface, protocols)` / `wifi.driver.setProtocols(interface, config)`
- `wifi.driver.setBandwidth(interface, mhz)` / `wifi.driver.setBandwidths(interface, config)`
- `wifi.driver.setDynamicCarrierSense(enabled)`
- `wifi.driver.configure11bRate(interface, disabled)`
- `wifi.driver.setCoexistencePowerManagement(enabled)`
- `wifi.driver.setConnectionlessWakeInterval(milliseconds)`
- `wifi.driver.getEventMask()` / `wifi.driver.setEventMask(mask)`
- `wifi.driver.setStorage(storage)` — stopped/zero-owner selection; see [Driver storage semantics](wifi-driver.md#storage-selection).
- `wifi.driver.disablePmf(interface)` — explicit stopped/zero-owner PMF control; see [PMF control](wifi-driver.md#pmf-control).
- `wifi.driver.setMode(mode)` — stopped/zero-owner mode selection with readback and rollback; see [Driver mode semantics](wifi-driver.md#mode-selection).
- `wifi.driver.getMode()` / `wifi.driver.getCountry()`
- `wifi.driver.getScanParameters()` / `wifi.driver.setScanParameters(config)`
- `wifi.driver.setBssColorCollisionReporting(enabled)`
- `wifi.driver.getStatisticsConfig()` / `wifi.driver.configureRxStatistics(config)` / `wifi.driver.setTxStatistics(category, enabled)`
- `wifi.driver.setCountryDetails(details)` — complete stopped/zero-owner country configuration; see [country details](wifi-driver.md#country-details).
- `wifi.driver.getAntenna()` / `wifi.driver.getAntennaGpio()` — SDK-stored shared PHY configuration; see [antenna observations](wifi-driver.md#antenna-observations).
- `wifi.driver.setAntenna(config)` / `wifi.driver.setAntennaGpio(config)` — stopped Wi-Fi and closed BLE, verified shared-PHY writes and GPIO route restoration; see [antenna control](wifi-driver.md#antenna-observations).
- `wifi.driver.getChannel()` / `wifi.driver.getHomeChannel()`
- `wifi.driver.getBand()` / `wifi.driver.getBandMode()`
- `wifi.driver.setBand(band)` / `wifi.driver.setBandMode(mode)`
- `wifi.driver.getPowerSave()` / `wifi.driver.getTxPower()`
- `wifi.driver.getRssi()` / `wifi.driver.getAid()` / `wifi.driver.getNegotiatedPhy()`
- `wifi.driver.getTsfTime(interface)` / `wifi.driver.getInactiveTime(interface)`
- `wifi.driver.setInactiveTime(interface, seconds)` / `wifi.driver.setRssiThreshold(dbm)`

See [driver controls](wifi-driver.md) for protocol/bandwidth transactions, explicit
pre-start rate configuration, framework write records and failure recovery.

- `wifi.capabilities()`
  Read actual firmware API availability without initializing, starting or
  connecting Wi-Fi. No arguments are accepted. Returns `WiFiCapabilities`:
  `apiVersion:"wifi/1"`, `target`, `idfVersion`, `modes`, `interfaces`, `bands`,
  `countryError`, `features`, `stationOptions`, `accessPointOptions`, `limits` and `namespaces`.
  Modes contain `station` and, when SoftAP is compiled in, `softAP` and
  `station+softAP`; interfaces use `station` / `access-point`. These advertise
  implemented modes, not simultaneous ownership by every feature. Namespaces includes
  `driver`, `rawTx`, `monitor`, `vendorIe`, `action`, optional `csi`/`ftm`, and
  `twt` on the reviewed C5 HE build and `nan` with SDK NAN sync enabled.
  `features.nan` describes [Candidate NAN discovery, services and datapaths](wifi-nan.md);
  unsynchronized discovery additionally requires SDK NAN USD. NAN status is also available in
  `wifi.diagnostics.snapshot().nan` (null when not compiled).
  `individualTwt` is true there for Candidate
  individual setup/status/close; `broadcastTwt` advertises Candidate broadcast
  discovery/setup/close on the same C5 build. See [TWT](wifi-twt.md). Probe availability
  is exposed by `wifi.twt.capabilities().probe`; individual suspension and resume are exposed by
  `WiFiTwtAgreement.suspend` and `WiFiTwtAgreement.resume`.
  Feature flags describe the implemented public APIs: station/scan/watch/wakeLock/
  setMac, Monitor, Raw TX, Action/ROC and Vendor IE are available; accessPoint,
  CSI, FTM, TWT, roaming, provisioning, NAN and Mesh follow their individual
  implementation and build gates. An underlying ESP-IDF symbol alone does not
  enable a public feature flag. In particular,
  promiscuous means the public Monitor API; CSI capture source availability
  is separately described by `wifi.csi.capabilities()`. Availability is neither
  current operation admission nor hardware/RF qualification.
  Limits share the native admission constants: 32 scan records, 16 wake locks,
  and 4 AP clients (0 in a SoftAP-disabled build), plus `maxWatchers:4`,
  `maxWatchCapacity:64`, `watchIngressCapacity:32`, `watchNeighborSlots:2`,
  `watchMaxNeighbors:64`, `watchMaxReportBytes:4096`.
  Bands describe physical target bands, not which AP options are implemented.
  Each entry contains `band` and `channels`: valid 2.4 GHz country bounds are
  enumerated; 5 GHz requires a manual, nonzero native country mask. Missing,
  invalid or implicit regulatory information yields null, never an unrestricted
  range. `countryError` is the raw SDK/admission error, or null on a successful
  read. An uninitialized/faulted/retiring driver still returns static discovery,
  with null channels. The read holds the Radio mutex and JS conversion uses a
  copied country value after unlock. Snapshots may become stale immediately.
  `stationOptions` reports availability of listenInterval/failureRetryCount,
  RM/BTM/MBO/FT/OWE and H2E identifier input, plus accepted `pmf`, `saePwe` and
  `minimumAuthModes` arrays. These follow the same build gates as capture;
  they do not promise roaming management APIs, AP security modes or RF success.
  `accessPointOptions` separately reports authModes/pairwiseCiphers/pmf/saePwe,
  FTM responder availability and beacon/DTIM bounds. Choices are individually
  available, not a promise that every combination is valid. AP arrays are empty
  when SoftAP is compiled out. `features.ftmResponder` describes the AP option;
  `features.ftmInitiator` separately reports the build-gated [FTM Session API](wifi-ftm.md).
  Security choices come from the explicit option arrays; protocol and bandwidth
  controls are documented in [Driver controls](wifi-driver.md). Capability flags
  do not identify the immutable Build Context or establish RF qualification.
- `wifi.watch(options?)`
  Create an `EventQueue<WiFiEvent>` without initializing/starting or acquiring
  Radio. No initial status event is emitted; use `wifi.status()` for a snapshot.
  At most 4 subscriptions are active. Options are `events` (default `"all"`,
  or a nonempty array of distinct exact `WIFI_EVENT_*` names), `capacity`
  (integer 1–64, default 16), `overflow` (`"drop-newest"` only), and
  `includeRawEventData` (boolean, default false). Unknown options, unsupported
  names, duplicates, sparse arrays and NUL suffixes fail before native setup.
  Names include every one of the fixed SDK's 53 public Wi-Fi events; an unknown
  native ID is named `WIFI_EVENT_UNKNOWN` and is delivered only to `"all"`.
  IP events remain in `net.watch()`.
  Each event contains `sequence`, `timestampUs`, `id`, `name`, `category`,
  `data`, and `dataUnavailableReason`. Sequence is a boot-scoped exact JS integer
  with possible gaps; it never wraps/reuses and admission fails at exhaustion.
  Timestamp is callback capture time from `esp_timer_get_time`, not RF/TSF time.
  Typed data is provided for scan completion, Station connect/disconnect/auth
  change, AP client connect/disconnect/probe, low RSSI and home channel change.
  SSIDs are length-bounded, MACs use lowercase hex-colon, RSSI is dBm, and enum
  fields ending in `Id` preserve native numeric values. AP AID remains an
  observation, not a durable client identity. Additional reviewed snapshots:

  | Event | Typed data |
  | --- | --- |
  | FTM_REPORT | peerAddress, statusId, rttRawNs, rttEstimatedNs, distanceCm, reportEntries |
  | ACTION_TX_STATUS | interfaceId, statusId, operationId, channel |
  | ROC_DONE | statusId, operationId, channel |
  | AP_WRONG_PASSWORD | address only |
  | STA_BEACON_OFFSET_UNSTABLE | beaconSuccessRate |

  FTM measurements/count are null unless the SDK reports FTM_STATUS_SUCCESS.
  RTT values are unsigned nanoseconds and distance is centimeters, as documented
  by the pinned SDK. This observer does not fetch or release the detailed report.
  Action/ROC omit the opaque native context; operationId is only the SDK's small
  observed ID, not a framework Future identity. Successful Action TX may publish
  both TX_DONE and DURATION_COMPLETED. Neither event completes a public operation
  through this lossy queue. Beacon success rate preserves the finite SDK float;
  no percentage/fraction range is inferred. Nonfinite payloads are unavailable.
  AP_WRONG_PASSWORD includes the client MAC, never a credential.
  TWT setup/teardown/probe/suspend/wakeup also provide typed snapshots. Setup
  includes statusId, reason, configuration and targetWakeTime. Individual setup
  success is the SDK's value **1**, not ESP_OK; unsuccessful setups have null
  configuration/time. Successful configuration preserves the command, trigger,
  flow type/ID, interval exponent/mantissa and duration count. Individual setup
  additionally includes wakeDurationUnitId, twtId and timeoutMs; duration unit 0
  is 256 us and 1 is 1024 us. Broadcast duration uses the SDK's 256 us count.
  targetWakeTime is the native 64-bit word as `{low, high}` unsigned 32-bit halves,
  preserving values beyond JS's exact integer range. No conversion to host time,
  callback timestamp or inferred native clock/unit is performed.
  Individual setup failures retain the SDK reason; broadcast setup retains it
  only for TXFAIL. Probe failure retains reason, success uses null. Teardown
  exposes statusId and flowId/broadcastId. Suspend exposes raw statusId,
  flowIdBitmap and exactly eight actualSuspendTimeMs slots; unselected slots are
  null. Selected values are reported SDK durations, not a separate success claim.
  On the reviewed C5 build, native individual suspend/information observations
  are posted without waiting for queue space. A saturated observer queue can
  drop these observations while native cleanup continues; receipt is not an
  operation completion token.
  Wakeup exposes typeId and flowId. These observation converters do not register
  the TWT operation namespace, negotiate agreements or own native sessions.
  Neighbor report events expose `{received, reportLength, dialogToken,
  neighbors, skippedElements}`. Each neighbor contains `bssid`, unsigned
  `bssidInformation`, `operatingClass`, `channel`, `phyTypeId`, nullable
  `candidatePreference` and `skippedSubelements`. Unknown IE/subelement contents
  are omitted and counted. `reportLength` includes the one-byte dialog token.
  SDK no-report/timeout notifications use `received:false`, length 0, token null
  and an empty list; token-only reports use `received:true` and an empty list.
  Malformed TLVs reject the entire data with `unsupported-layout`. Reports over
  4096 bytes or 64 neighbors use `too-large`, without truncation. Two shared
  normalized report slots are allocated when the first interested watch opens;
  exhaustion delivers metadata with `data:null`, reason `capacity`. Multiple
  subscribers share the immutable native snapshot. Conversion produces owned JS
  objects and releases its slot reference, including on allocation failure.
  This observes SDK reports; it does not request reports or perform roaming.
  Payloadless lifecycle events
  contain `{}`; absent/invalid payloads use null with `unsupported-layout`.
  WPS/DPP/NAN events contain metadata only with `data:null` and
  `dataUnavailableReason:"sensitive"`. Other unreviewed layouts use
  `"not-reviewed"`; their pointers and bytes are never read or freed by watch.
  Raw export has no approved layouts yet. When requested, the event includes
  `rawEventDataUnavailableReason:"sensitive"|"not-reviewed"`, without a raw
  buffer or schema. Credentials remain the responsibility of dedicated APIs.
  A shared 32-entry native ingress uses nonblocking, drop-newest publication;
  runtime polling fans out value copies and retained neighbor snapshots to the
  independently bounded queues.
  Scan/disconnect observations follow native control updates and wait for the
  matching public Future's native registration to end before fanout. Filtering
  and saturation cannot remove a native completion obligation. This is a
  Wi-Fi event stream: STA_CONNECTED is association, not connect Future success
  (which also requires IP readiness).
  `queue.stats().dropped` reports subscriber queue overflow;
  `wifi.status().watch` contains `{subscribers, ingressQueued, ingressCapacity,
  ingressDropped, sequenceExhausted, neighborSlotsUsed, neighborPoolBytes,
  neighborPoolRetired}`. Ingress drops include callback lock
  contention and saturation and are counted separately, saturating at uint32 max.
  Close the queue to unsubscribe. Already queued value snapshots may still be
  drained under the existing EventQueue contract. Conversion never refers to
  the freed subscription. Last close frees ingress; GC/runtime teardown also
  detach subscriptions. Last close also retires the neighbor pool; queued or
  received-but-unconverted reports retain it until drained/disposed. A new watch
  interested in neighbor reports fails with `WIFI_WATCH_FAILED` while that pool
  is retired; release old queues/receivers first. Watches filtering neighbor
  reports out can still open. The pool is freed after the final native owner,
  independently of JS result lifetime. One boot-owned mutex/default-loop observer remains
  for later subscriptions, with no runtime, JS, driver or Radio lease reference.
  Native registration failures throw `WIFI_WATCH_FAILED`; capacity exhaustion
  throws an internal error and allocation failure preserves OOM.
- `wifi.diagnostics.resetFrameworkCounters()`
  Start new observation histories without stopping Radio or consuming data.
  Takes no arguments, requires the active runtime and returns undefined with no
  JS allocation on the success path. It resets the following existing counters:

  - Registered runtime EventQueue drops/peaks, including non-Wi-Fi queues using
    this shared runtime, and Wi-Fi watch ingress drops/peak.
  - `wifi.status().connectionCounters`: actual framework connection submissions,
    submissions after a previously observed association, immediate SDK submission
    failures, and valid framework-observed STA_CONNECTED events. All saturate at
    UINT32_MAX. Admission/configuration failures before `esp_wifi_connect()` do
    not count as submissions. These counters do not count SDK RF retries or add
    an automatic reconnect policy. Reset preserves the boot association marker,
    so a new submission can be a reconnect even with `associations:0` after reset.
  - Readable active/retained CSI and registered Monitor callback, publication,
    delivery, byte, filter and loss histories. Live leased-frame/publisher counts,
    callback activity, sequence/generation exhaustion, decimation phase and rate
    limiting timestamps retain their original values.
  - Global managed-memory allocation failures, migration count/bytes and evictions,
    plus shared wireless reservation denials and peaks. This includes non-Wi-Fi
    users of the same memory manager. It does not reset physical heap minima.

  Queue peaks restart at current depth and memory peaks at current reservations,
  including pending alloc/free. Limits, role subtotals, DMA reservations, owner
  counts, queued payloads, Radio faults, cleanup obligations, protocol results and
  operation progress (including Raw TX job iteration counts) retain their values.
  SDK/driver statistics and BLE-specific session histories are not reset.

  Providers reset independently under their existing locks/atomic counters;
  this is not a transaction across modules or all fields of a frame. An operation
  spanning reset can publish later outcomes into the new history, and counters
  may already be nonzero on return. For comparisons, stop traffic and use the same
  warmed idle state. Monitor uses temporary control references; their release can
  finish retirement of an already closed, otherwise unowned pool.

  `snapshot().counterReset` reports a saturating boot-lifetime `count` plus
  boot-relative `startedUs`/`finishedUs` (null before the first reset).
  `unavailableMonitorGenerations` counts controls that could not be temporarily
  retained; `unavailableCsiGenerations` counts allocating/retiring entries whose
  storage was not safe to access. Those histories are skipped, with existing
  ownership and retirement preserved. Reset metadata survives runtime restart.

- `wifi.diagnostics.dumpDriverStats(mask?)`
  Invoke the SDK statistics log dump once. The optional exact integer combines
  BUFFER=1, RXTX=2, HW=4, DIAG=8 and PS=16 (0..31). Omitted/undefined, `-1` or
  `4294967295` selects SDK ALL. Strings, booleans, null, fractions and other bits
  are rejected before driver access. Zero is passed through as an empty mask.
  Returns true on ESP_OK; admission/SDK failures throw `WIFI_DIAGNOSTICS_FAILED`
  with `details.espCode`, `stage` (`admission` or `driver-dump`) and normalized
  unsigned `mask`. Conversion failure preserves OOM and never retries the dump.

  Radio must already be initialized, stable and free of pending lifecycle,
  operation or fault cleanup. The dump runs under the Radio mutation mutex;
  it does not initialize/start Radio, acquire leases or reset framework counters.
  Driver output uses the existing SDK logging path; when runtime log capture is
  enabled, SDK log messages enter `runtimeLogs` subject to its existing log
  levels, truncation and ring capacity. Success does not guarantee a retained
  log entry. No console parsing or structured interpretation of driver text.

- `wifi.diagnostics.idfApiCoverage()`
  Return generated `wifi-idf-coverage/1` metadata and counts from the reviewed
  public SDK inventory and coverage map. `scope` is `reviewed-inventory`:
  `total` counts unique C symbols across recorded configurations, with separate
  `headers`, `tasks` and `referenceVariants` groups. A symbol appearing in two
  variants is counted once in the total and once in each variant; variant counts
  must not be summed to obtain the total.

  Each counts object reports `symbols`, `functionSymbols`, `registeredSymbols`,
  the six dispositions (`mapped`, `frameworkOwned`, `buildTime`,
  `removedOrDeprecated`, `privateExcluded`, `targetUnsupported`), the three
  implementation states (`planned`, `inProgress`, `implemented`) and the three
  contract states (`reviewRequired`, `contractPending`, `reviewed`). Each of those
  three classifications sums to `symbols`. Task defaults and symbol overrides
  are resolved before counting. `registeredSymbols` counts entries whose
  `jsPath` is in the all-features manifest; multiple C symbols can map to one JS
  method. Registration does not imply implementation, target enablement or
  runtime/RF qualification. No completion percentage is inferred from counts.

  `reviewedIdfRevision`, `inventorySha256`, `mapSha256` and `manifestSha256`
  identify the exact inputs. `target` and `idfVersion` describe this firmware.
  `capabilities` independently uses `wifi.capabilities()` and its current
  target/build and live country-availability semantics. Recorded variant names
  do not claim to describe this build. `unexpandedHeaders` retains conditional
  inventory gaps with their reasons; a gap is not classified as unsupported.

  Takes no arguments, starts no Radio and owns only returned JS values. Result
  allocation can fail with OOM; it acquires no native operation or result owner.
  Generate the flash-resident summary with
  `python scripts/generate_idf_wifi_api_map.py --write-runtime` after updating
  the reviewed map/inventory or all-features manifest. The existing coverage
  check and Wi-Fi CMake configure reject stale summaries or changed SDK inputs.

- `wifi.diagnostics.snapshot()`
  Return a `WiFiDiagnosticsSnapshot` with independent, nonsecret observations
  from existing native ledgers. This Candidate API does not start Radio,
  connect, consume received data, reset counters or request cleanup. It takes
  no arguments; allocation failure returns the original exception without
  explicitly cancelling native operations. Returned objects own value copies only.

  `apiVersion` is `wifi-diagnostics/1`; `startedUs` and `finishedUs` are
  boot-relative `esp_timer` microseconds bracketing sampling and conversion.
  This is not an atomic snapshot across tasks or modules. GC during conversion
  can release otherwise unreachable owners after an earlier field was sampled.
  Use warmed, equivalent idle states for memory comparisons.

  `wifi` has the `wifi.status()` schema, including scan/drain, AP clients,
  watch queues, Radio owner counts/errors and Raw TX/Action quarantine.
  `vendorIe`, `ftmInitiator`, `ftmResponder`, `twt`, `enterprise`, `smartConfig`,
  `wpsStation`, `wpsAccessPoint` and `dpp` reuse existing global status schemas.
  An optional provider is null when its feature/target gate is absent.
  `runtimeQueues` reports registered runtime EventQueue open/drop/depth/capacity
  counters and the sum of individual queue peaks. `wifi.watch.ingressHighWater`
  reports the shared ingress peak; `wifi.connectionCounters` reports the
  framework connection history described above.
  `memory` is the global managed-memory ledger also exposed by
  `sys.status.memory.manager`, including owner entries and allocation failures.
  Neither field is a Wi-Fi-only subtotal, and unmanaged driver/heap allocations
  are not all represented by that ledger.

  `memory.wireless` exposes shared internal/PSRAM quota, control reserve, current
  reservations, peak reservations since boot/last explicit reset, denied attempts
  and role subtotals.
  CSI/Monitor retained pools keep their reservation until actual free returns.
  Current integrated owners and requested-byte accounting are listed in the
  [system memory reference](sys.md); coverage does not yet include every wireless
  module/SDK heap allocation, and does not include the complete
  driver heap. Wireless EventQueue RTOS storage/receive state, registered native
  Future capture/bookkeeping, Raw TX owned storage, module control/copy allocations
  and CSI/Monitor ByteView/Source/read-lease wrappers participate in admission.
  Source and data owners keep their individual reservations until actual release.
  Wi-Fi helper synchronization/completion queues and watch ingress are included.
  `wireless.runtime` accounts for the shared Future service, including common
  receive handles/roots, slot/dispatch storage and persistent worker stack/TCB
  storage. It includes non-wireless calls using that common service.

  `monitor.sessions` includes every registered generation, including closed
  Sessions with retained Frame/View/Source storage. Each entry reports native
  control and pool bytes, free slots, leased frames, publishers, close/retirement
  flags and RX/drop counters. Temporary observation references are released
  before JS conversion. `unavailableSessions` counts registered controls whose
  temporary reference could not be acquired; they are not reported as empty.
  Closed contexts with no payload owners can remain with `poolBytes:0`.
  This is the pool's published ledger: zero can precede an in-progress
  allocator free returning and is not a measurement of free heap.

  `csi` reports the current or last driver control generation and a `generations`
  array for all allocating, active, retained and retiring CSI pools. Pool slots
  share `slotBudget`; `generationBudget` bounds resource controls. Each entry's
  `storageBytes` reserves its resource control, slots, samples and optional packet allocation until
  allocator cleanup returns. `freeSlots` and RX/owner counters are null during
  allocation/free. Aggregate `activeStorageBytes`, `retainedStorageBytes` and
  `pendingStorageBytes` add to `storageBytes`; these count reservations, not free
  heap. `controlBytes` counts the boot-owned CSI Session, pool registry, native RX receipt
  registry and their locks.
  A closed retained pool allows a new open if the CSI budget has capacity.
  `identityExhausted` requires device reboot and is not reset by runtime restart.
  CSI budget totals exclude queue/Frame/Source/JS/allocator overhead and driver
  storage. Monitor's pool ledger has the distinct release semantics described above.

  Counter reset, driver dump and SDK coverage are exposed by the diagnostics
  methods above. Queue peaks, connection history and shared wireless quotas
  have the documented scopes; complete SDK heap attribution remains unavailable.
  Compilation does not establish runtime, GC/retirement or RF acceptance.
- `wifi.startAP(options)`
  Start an exclusive SoftAP and return `{ started, ssid, channel, hidden,
  authMode, maxConnections, beaconIntervalMs, dtimPeriod, csaCount, pairwiseCipher,
  pmf, saePwe, ftmResponder, transitionDisable, gtkRekeyIntervalSeconds,
  saeExt, wpa3CompatibleMode, bssMaxIdlePeriod, bssMaxIdleProtectedKeepAlive }`.
  With no Station/application owner, this API requires a stopped, exclusive
  Radio, reserves it before netif allocation, writes RAM configuration while
  stopped, then starts AP. With a live Station, it can reopen an already stored
  AP configuration that matches the requested settings without a configuration
  write. On the pinned C3/S3/C5 SDK, different settings are installed and read
  back on the Wi-Fi task before native AP allocation. Explicit PMF disable uses
  its dedicated AP operation in that same window after security validation.
  This path preserves Station with the default `allowDisconnect:false`.
  Other SDK/target combinations have no private pre-start entry and reject a
  mismatch before mutation.
  Reopening requires idle Station helpers (an established connection is allowed),
  no pending Future/native drain, wake locks, CSI/ESP-NOW or other foreign owners.
  It attaches AP netif/handlers before APSTA mode, waits for AP_START and its
  marker while Station remains live, and returns the actual primary channel,
  which follows Station. Storage and Station/Application identities are retained.
  Set top-level `allowDisconnect:true` to explicitly replace a running AP or
  explicitly select the disconnecting configuration path. This uses the common stopped
  configuration transaction: disconnect Station, wait for its native drain,
  STOP and retire helpers, configure/read back AP while stopped, then START.
  It preserves the configured Station mode and saved Station configuration;
  the final mode is AP or APSTA. It does not reconnect Station or preserve its
  IP/netif/lease identities. Existing storage is retained (including FLASH),
  with AP/RAM defaults on a fresh driver. Even a matching request takes this
  transaction when permission is true; AP clients can be disconnected. Foreign
  Radio owners, pending native operations and cleanup still prevent admission.
  Permission is a strict boolean and cannot appear inside `driver`. Full input
  capture and native validation happen before owner transfer or disconnect.
  New AP configuration is checked before it broadcasts; failure is left with
  the shared cleanup coordinator, never automatically retried with credentials.
  The pre-start configuration path above supports changing AP settings while
  keeping an established Station connection. Short XIAO C5 APSTA tests preserve
  the Station IP and Radio generation; other targets and RF combinations still
  require their own qualification.
  The top-level channel accepts 2.4 GHz channels 1–11; nested driver channels
  use the raw range described below. Full target/RF acceptance is pending.
  SSID is required: 1–32 UTF-8 text bytes without NUL, or a `ByteSource` of
  1–32 bytes. Binary AP input may contain NUL. Auth modes are open, wpa,
  wpa2, wpa/wpa2, wpa3, wpa2/wpa3, and owe, subject to build gates. Default is
  WPA2-CCMP with a password, open otherwise. Open/OWE cannot have a password;
  pure WPA3 accepts 1–63 UTF-8 bytes, other password modes require 8–63,
  all without NUL. Credentials are never returned or logged.
  `channel` is 1–11 (default 1), also checked against driver country; `hidden`
  is boolean (default false); `maxConnections` is 1–4 (default 4). Unknown
  options fail before resources/driver effects. `driver` accepts the implemented
  `WiFiAccessPointDriverConfig` fields except `ssid`, which stays at the top level.
  Defined fields may occur at either level, never both, even if equal. This also
  rejects top-level `beaconIntervalMs` together with `driver.beaconIntervalTu`.
  Undefined values are absent. `driver.beaconIntervalTu` is an exact integer
  100–60000 TU in multiples of 100; it does not round-trip through milliseconds.
  `driver.channel` is 0–14 or a supported 5 GHz channel, subject to target, band,
  country and lifecycle admission. Zero delegates channel selection to the SDK.
  Nested empty password means absent credentials, matching raw configure; the
  final authentication policy must still permit an empty password. Other fields
  use the same authentication/PHY validation after merging. Capabilities expose
  `accessPointOptions.driver`; this is input support, not RF qualification.
  `beaconIntervalMs` must be finite and positive, at most 61440. It rounds up
  to the next 102.4 ms (100 TU) quantum required by the SDK. For example,
  100 ms becomes 100 TU / 102.4 ms. Default is 102.4 ms. `dtimPeriod` is 1–10
  beacon intervals (default 1); `csaCount` is 1–255 (default 3), not a promise
  that every client received a channel-switch announcement.
  `pairwiseCipher` accepts tkip, ccmp, tkip/ccmp, gcmp or gcmp-256; GCMP requires
  its build gate. WPA and WPA/WPA2 default to TKIP/CCMP, other protected modes
  to CCMP. Pure WPA requires a TKIP-containing choice; WPA3, WPA2/WPA3, OWE or
  required PMF reject TKIP-containing choices. Open rejects an explicit cipher
  and returns null for this field.
  `pmf` accepts disabled/optional/required on RSN modes. `disabled` is explicit,
  requires WPA2 or WPA/WPA2 without WPA3 compatible mode, and invokes the dedicated
  PMF disable API after configuration and before START. Both flags must read back
  false; failure follows the stopped transaction's rollback/cleanup boundary.
  Optional/required readback must retain capability. Pure WPA3/OWE defaults to
  required and rejects optional; WPA2/WPA2-WPA3 defaults to optional. Open/pure
  WPA reject explicit PMF and return null. Deprecated capable=false does not
  implement PMF disable; this option performs the dedicated operation instead.
  `saePwe` accepts hunting-and-pecking/hash-to-element/both for WPA3 or compatible mode,
  gated by SoftAP SAE/H2E support. Its return is null for non-SAE or unspecified
  native enum. `transitionDisable:true` requires WPA3, WPA2-WPA3 or compatible mode.
  `ftmResponder:true` requires compiled FTM responder support. Both default false.
  `gtkRekeyIntervalSeconds` is 0 (default, disabled) or 60–65535; open rejects
  nonzero rekeying. Unsupported build choices throw WIFI_AP_UNSUPPORTED before
  acquiring Radio; malformed/conflicting inputs report TypeError, preserving OOM.
  `saeExt:true` requires SAE/H2E, SoftAP SAE and GCMP hardware/build support.
  It defaults omitted authMode to pure WPA3, pairwiseCipher to GCMP-256, PMF to
  required and saePwe to hash-to-element. Explicit weaker/conflicting choices
  are rejected; PWE both is also valid. It cannot be combined with compatible
  mode. False is the default and remains accepted without extension support.
  `wpa3CompatibleMode:true` explicitly authorizes the SDK's WPA2/CCMP base
  profile, with SAE/required PMF supplied through RSN override to compatible
  peers. It is not a pure WPA3-only AP. Input authentication may be WPA2, WPA3
  or WPA2-WPA3; passwords must be 8–63 bytes, cipher must be CCMP, and SoftAP
  SAE/compatible support must be built. Readback must identify the WPA2 base
  and retain the compatible flag. A requested PMF requirement is never lowered
  to accept readback. Returned authMode and wpa3CompatibleMode describe these
  separate facts. False is the default and is accepted without this build feature.
  `bssMaxIdlePeriod` is integer 0 (default, disabled), or 10–65535 units of
  1000 TU (1.024 seconds). It controls the AP's native inactivity policy.
  `bssMaxIdleProtectedKeepAlive` is boolean, default false; true requires a
  nonzero period and encrypted authentication. This does not replace the
  independently requested PMF policy. Nonzero/true requires BSS max idle support;
  explicit 0/false remains valid without it. Availability is reported by
  `capabilities().accessPointOptions.saeExt`, `wpa3CompatibleMode` and `bssMaxIdle`.
  A shared native validator also runs at driver admission. RAM config is read
  back before start: SSID/password/auth/cipher and requested PMF/PWE/transition/
  rekey security policy and exact extension/compatible/idle settings are checked
  before broadcasting. Read failure or a
  mismatch reports `ap-config-readback` and retains AP resources for stopAP.
  Returned nonsecret fields describe driver readback, not RF/client negotiation.
  This configuration snapshot is typed as `WiFiAccessPointStartResult`; current
  sampled state and query/cleanup diagnostics use `WiFiAccessPointStatus` in
  `wifi.status().accessPoint`.
  `started` requires the native START events and subsequent Radio marker; IP/DHCP readiness belongs to
  net.status. A returned-status allocation failure can occur after start;
  inspect wifi.status().accessPoint and use stopAP.
  Without explicit `allowDisconnect:true`, repeated startAP requires stopAP
  first. The permission does not bypass pending cleanup: inspect status and
  finish stop/stopAP before making another configuration request. A shared reopen setup/activation error
  retains the same token/netif for stopAP; an attempted activation is never
  replayed by startAP. A rejected pre-start transaction with confirmed native
  AP absence uses an event marker and AP-only resource retirement; it does not
  wait for an AP_STOP from an AP that never started. Successful configuration
  rollback under RAM storage plus completed retirement clears this transaction's Radio fault,
  while `radio.configuration` retains the original error and rollback result.
  Failed rollback, possible FLASH mutation, unknown native completion or a later activation error keeps
  the fault. If AP termination cannot be proved, disconnect Station and use
  wifi.stop for whole cleanup. Shared stages use ap-reopen-admission/start in AP
  details and ap-reopen-* / ap-prestart-* in Radio diagnostics;
  eventPhase may be ap-start. A SoftAP-disabled build returns
  WIFI_AP_UNSUPPORTED without starting the driver.
- `wifi.stopAP(options?)`
  Remove AP and release its netif, then return `WiFiStatus`; repeated calls
  are harmless. `options.timeoutMs` is an integer from 1 to 2147483647 ms,
  default 1000. It shares one budget across AP_STOP/fence, AP netif retirement
  and any remaining coordinated cleanup; a retry gives the retained suffix a
  fresh budget. Synchronous SDK calls, mutex acquisition and result construction
  are not preemptible, so this is not a hard wall-clock deadline. Invalid values
  and extra arguments fail before admission, including in SoftAP-disabled builds.
  SoftAP-disabled stopAP remains an idempotent status query.
  For APSTA, switch to Station mode,
  wait for AP_STOP and its event fence, then retire only AP resources. Station
  helper/IP resources and Station/Application lease identities remain in place.
  A pending Station operation/Future, wake lock or foreign owner blocks admission.
  An exclusive AP is stopped and deinitialized. Radio admission stays closed
  throughout either cleanup path. Failed native
  start/cleanup retains bounded resources for stopAP or runtime cleanup. Errors
  use `WIFI_AP_FAILED` with `details.stage` and the raw `details.espCode`.
  A failed netif detach is not blindly retried: the fixed SDK may have freed
  its driver even when returning an error. Such an error reports
  `details.restartRequired:true` and retains exclusion until device reboot.
  This is separate from `radio.restartRequired`, which describes Radio faults.
  A failed partial close retains its token and AP resources; retry `stopAP`
  to finish the uncompleted suffix. Accepted mode changes are not replayed.
  `wifi.stop` may take over whole cleanup after Station has disconnected and
  native operations have drained. Runtime teardown waits for the same native
  terminal barrier before taking over; it does not silently preserve a dying
  runtime's Station. Failed cleanup prevents new runtime attachment. Actual
  Station continuity, RF and lifecycle qualification remain pending.
- `wifi.apClients(options?)`
  Return a bounded snapshot of this running SoftAP's clients as
  `{ address, aid, rssi, phy, ip? }[]`; an empty running AP returns `[]`.
  `includeIp` is an optional boolean, default false. With true, `ip` is a dotted
  IPv4 string from a later local DHCP lease lookup by MAC, or null when absent.
  With false/omitted, the field is absent and no DHCP query runs. These are
  separate association/AID/DHCP observations: a lease can outlive association,
  and this does not discover a peer's static IP or prove reachability. DHCP
  server support is required for includeIp:true; otherwise WIFI_AP_FAILED has
  espCode ESP_ERR_NOT_SUPPORTED before Radio access. DHCP lookup errors fail
  the whole query with stage clients-ip and the original espCode. Unknown
  options and non-boolean includeIp are rejected before native queries.
  An absent or closing AP throws `WIFI_AP_FAILED`; builds without
  SoftAP support throw `WIFI_AP_UNSUPPORTED`. `address` is a lowercase
  colon-separated MAC, `rssi` is the SDK average in dBm, and `phy` contains the
  reported flags (`11b`, `11g`, `11n`, `11a`, `11ac`, `11ax`, `lr`), not a
  negotiated rate. `aid` is looked up after the list snapshot and is null if
  the MAC has already left. It is an observation, not a durable client identity;
  the list and AIDs are not an atomic snapshot of association state. The driver read holds the Radio
  mutex after exact AP lease validation; JS conversion happens after unlock.
  Allocation failures do not change AP/client state.
- `wifi.deauthClient(address)`
  Request deauthentication of the MAC currently associated when the native
  command executes. Exactly one nonzero unicast MAC string in
  `xx:xx:xx:xx:xx:xx` form is required; hex digits may use either case.
  AIDs, broadcast/multicast addresses, extra arguments and NUL suffixes are
  rejected before native access. Returns false if the MAC is absent, or true
  if the SDK accepts the targeted request. True does not confirm delivery of
  a management frame or departure of the client, which may reconnect.

  The MAC lookup and deauth run in one Wi-Fi-task command; the reviewed SDK
  executes both calls inline on that task. AID is never retained for a later
  command, and zero is never passed to the SDK's all-client selector. The
  command holds the managed AP's exact Radio owner and shares its mutation
  lane. It does not stop AP, disconnect Station or change the channel.

  An absent/closing AP, conflicting operation or SDK failure throws
  `WIFI_AP_FAILED`, with `details.espCode`, `stage`, `requestAccepted` and
  `handoffUnknown`. `requestAccepted` is null for an unconfirmed dispatch;
  otherwise it reports SDK acceptance, without promising absence of partial
  effects on error. An unconfirmed command remains allocated and holds the
  AP owner; `wifi.status().radio` retains `ap-deauth-ipc-unconfirmed` and the
  active operation. Polling/runtime cleanup consumes a later receipt without
  resubmitting. If no receipt arrives, device reboot may be required.
  Builds without SoftAP return `WIFI_AP_UNSUPPORTED`. Candidate: association
  races, allocation/queue failure and RF behavior await concentrated validation.
- `wifi.getMac(iface)`
  Read the initialized driver's current `"station"` or `"access-point"` MAC
  and return a lowercase colon-separated string. Exactly one argument is
  required. This read never initializes Wi-Fi or acquires an owner, and works
  while a healthy initialized driver is stopped. A missing driver, lifecycle
  cleanup or driver fault throws `WIFI_MAC_FAILED` with the raw ESP-IDF code.
  Reading the AP MAC in a build without SoftAP reports `ESP_ERR_NOT_SUPPORTED`.
  NAN interface access remains planned.
- `wifi.setMac(iface, address)`
  Set a Station or AP interface MAC and return the actual lowercase
  colon-separated address. Exactly two arguments are required. The address
  must contain six two-digit hex groups separated by colons, be nonzero and
  unicast, and differ from other enabled-build interfaces' addresses. The
  locally administered bit may be either value. Unknown interface names,
  whitespace, NUL suffixes and multicast/broadcast addresses are rejected.
  The initialized driver must be fully stopped with zero owners and no native
  operation, lifecycle transaction, promiscuous claim or cleanup/fault. For
  example, call start then stop to initialize an idle driver; setMac never
  implicitly starts/stops it. Running AP/Station/CSI/ESP-NOW remain untouched
  on admission failure. AP selection is unsupported when SoftAP is compiled out.
  Collision checks include Station, compiled SoftAP and compiled NAN interfaces;
  a getter failure rejects the write. Validation, write and exact readback use
  the Radio mutation mutex. `WIFI_MAC_SET_FAILED` includes raw `espCode`, stage
  and `driverAccepted`; a post-write error does not imply rollback. This changes
  the driver interface address, not the chip's eFuse/base MAC, and does not
  promise persistence across deinit/reboot.
- `wifi.acquireWakeLock()`
  Acquire one native force-wakeup reference on an already running Radio and
  return a `WiFiWakeLock` with readonly `acquired` and idempotent `close()`.
  It accepts no arguments and never initializes, starts or connects Wi-Fi.
  It can accompany existing Station, AP, CSI or ESP-NOW activity and does not
  change mode, channel or power-save configuration. There are at most 16 live
  references; capacity/identity exhaustion fails without another SDK acquire.
  Each token is unique across runtime restarts and tied to its Radio generation.
  Close all wake locks before stop/stopAP/shutdown; they keep the Radio alive
  without appearing as data-feature owners. A close error throws
  `WIFI_WAKE_LOCK_FAILED` with the raw ESP-IDF code and retains the reference
  for retry. `acquired` remains true while that exact native token is live.
  The finalizer attempts release; if it fails, bounded native storage remains
  after the JS object is freed. Runtime teardown releases outstanding references,
  and the next runtime attach retries only failed entries before Radio cleanup.
  Persistent release failure blocks attachment; runtime restart is a retry,
  not guaranteed recovery. Direct construction of WiFiWakeLock is rejected.
  `wifi.status().radio.wakeLocks` reports outstanding native references, while
  `wakeLockError` records the latest native wake operation error (or null).
- `wifi.start(options?)`
  Start configured interfaces and return `WiFiStatus`, without connecting
  Station. Options are mode (`station`, `ap`, `apsta`) and storage (`ram`,
  `flash`). Omitted values preserve the initialized driver's configuration;
  a cold start defaults to Station/RAM. Undefined and an empty object use these
  defaults. Unknown options, NUL-suffixed enums and non-string values fail
  before native access. AP/APSTA require SoftAP support and a valid stored AP
  configuration: configure it first with start:false when needed.
  A stopped start shares configure's helper preparation and START barrier.
  Stored AP configuration and its regulatory channel are validated before
  broadcasting; start does not rewrite AP credentials. Storage selects the SDK
  policy for later writes; it does not promise to copy the current config to NVS.
  Already-running mode/storage must match; changing them requires explicit
  stop or configure. Matching starts preserve existing owners/netifs; if another
  feature started Station first, a Station lease anchors that Radio during
  helper allocation. Late attachment joins the native START barrier and starts
  its IP interface before publishing readiness, without restarting shared Radio.
  Pending Wi-Fi Futures/native drains or cleanup reject start.
  Failed setup/START preserves the central cleanup token for stop/runtime cleanup.
  start-ap-allocate/start-ap-config identify AP snapshot allocation/validation
  failures. Result allocation can fail after successful start; inspect status
  before retrying. Detailed behavior and pending validation are recorded in the
  [start options implementation](../investigations/2026-09-08-w02-start-options.md).
- `wifi.stop(options?)`
  Stop an idle radio and return `WiFiStatus`. Disconnect first, consume or cancel
  pending Futures and close CSI/ESP-NOW sessions. A connected Station, native
  operation, pending result or another feature owner causes `WIFI_STOP_FAILED`
  before cleanup. Native disconnect/IP draining must also finish; retry after
  that terminal barrier. `timeoutMs` is an integer from 1 to 60000 (default 1000),
  shared across this call's timer, disconnect, Radio event, callback and AP/STA
  netif waits. Elapsed time in earlier steps consumes the same budget; a retry
  starts a new budget for the retained unfinished suffix. Expiration does not
  cancel queued native cleanup or release storage still used by callbacks.
  This bounds cooperative waiting, not wall-clock return time: synchronous SDK
  calls, mutex acquisition and result construction cannot be preempted. Busy
  owner admission still fails immediately instead of waiting for owners.
  Unknown options and invalid timeout values fail before native work.
  A healthy AP is included in
  the stop, disconnecting its clients; a connected Station must still disconnect
  first. Normal AP/APSTA stop uses the central three-owner cleanup and retains
  initialized driver storage. APSTA startup is available through configure; `stopAP` removes only its AP interface.
  Once admitted, it excludes new owners through timer/handler cleanup, driver
  stop and netif/queue release. Failed cleanup keeps `radio.lifecycleActive:true`
  and rejects new work until an explicit stop or runtime cleanup retries the
  unfinished suffix. `WIFI_START_FAILED` / `WIFI_STOP_FAILED` include the raw
  ESP-IDF code; status retains native fault/cleanup diagnostics. If allocating
  the returned status fails, accepted driver effects remain visible to `status()`.
  Stop retains initialized driver storage; a later start can reuse it.
- `wifi.DEFAULT_TIMEOUT_MS`
  Default station connect timeout in milliseconds.
- `wifi.status()`
  Return `{ accessPoint, initialized, started, connected, scanning, scanDraining, scanCleanupError, connectTimerError, connectDraining, disconnectCleanupError, ssid, ssidBytes, lastDisconnectReason, lastDisconnectReasonName, droppedDriverEvents, cleanupStage, cleanupError, radio, watch }`.
  `accessPoint` is null when the AP helper has no live/pending resources, or
  SoftAP is not compiled. Otherwise it reports `{started, cleanupPending,
  cleanupStage, queryError, queryStage, ssid, ssidBytes, hidden, channel,
  authMode, maxConnections, clientCount, mac}`. `started` reflects received
  AP_START/AP_STOP events; it does not imply client or DHCP readiness. `channel`
  is the current SDK reading, including the Station's home channel in APSTA.
  Config, MAC, channel and client count are separate observations, not an atomic
  RF snapshot. SSID text is null for invalid UTF-8; `ssidBytes` preserves all
  bytes, including embedded zeroes. Query failure leaves started/cleanup
  diagnostics available and sets the sampled fields to null, with raw SDK
  `queryError` and the failed `queryStage`. It does not change persistent cleanup
  diagnostics. Closing helpers are not queried through SDK. No query initializes
  Wi-Fi or returns passwords. JS allocation failure still throws normally.
  `droppedDriverEvents` counts bounded internal Wi-Fi event publications that
  could not be queued without blocking.
  `started` reports the current physical-radio state. `radio` contains `{ generation, driverState, driverOwned, restartRequired,
  faultStage, faultError, cleanupStage, cleanupError, initialized, starting,
  started, requestedMode, mode, activeOperations, wakeLocks, wakeLockError, lifecycleActive, fixedChannelOwners, conflictedChannelOwners,
  promiscuousOwners, promiscuousIdentityExhausted,
  channel, channelGeneration, channelObservationError, maxTxPowerDbm, powerSave, clients }`; client counts distinguish the Wi-Fi station helper
  from the explicit `application`, `wifiAccessPoint`, `wifiMonitor`, `wifiRawTx` and ESP-NOW/CSI leases.
  `wifiMonitor` accounts native Monitor capture owners; its presence does not
  imply that the public Monitor namespace is registered in this build.
  `wifiRawTx` counts native Raw TX leases, including leases retained by unfinished
  sends. `radio.rawTx` reports native completion/quarantine and cleanup diagnostics;
  see the [Raw TX API](wifi-raw-tx.md) for one-shot/Session sending and timeout semantics.
  `promiscuousOwners` counts live enable claims, including failed cleanup retained
  for retry. Each acquisition has an independent identity; exhaustion is permanent
  until device reboot and reported by `promiscuousIdentityExhausted`. Runtime
  restart cannot reset this identity space. This count does not advertise Monitor
  or CSI multi-session capture support.
- `wifi.setCountry(code, options?)`
  Set the SDK country code and return its actual `WiFiCountryStatus`. Call
  `wifi.start()` first; Station must be disconnected, with no native operation,
  pending cleanup or foreign feature owner. This setter does not initialize,
  connect, stop or restart the driver. Active AP/APSTA country changes remain
  part of the later configuration transaction work.
  `code` is exactly two uppercase ASCII letters or `"01"` (world safe mode);
  the SDK rejects unsupported codes. Options are `policy: "auto" | "manual"`
  and `ieee80211d: boolean`. Defaults are auto/true; either field selects the
  policy, and both must agree when present. Unknown fields are rejected.
  Auto permits SDK adoption of a connected AP's country during a later
  connection. The SDK documents country persistence to flash and PHY-data
  changes: this API is not a promise of RAM-only regulatory configuration.
  Station/AP credentials continue to follow their RAM-storage contract.
  The returned country describes the local driver, unlike a scan record's
  country, which describes that AP. Success verifies the requested code and
  policy against SDK readback. Errors use `WIFI_COUNTRY_FAILED` with
  `details.espCode`, `stage`, and `driverAccepted`. The original failure, attempted
  mutation, possible persistent write and verified rollback are recorded in
  `wifi.status().radio.configuration`. An unconfirmed rollback faults Radio and
  retains cleanup. A successful rollback verifies local settings only; it does
  not prove NVS durability. An allocation failure when returning the result can
  follow a successful write; inspect current status before another action.
- `wifi.setChannel(channel, options?)`
  Change an already started idle Station or exclusive SoftAP's channel and
  return `{ channel, secondaryChannel, band, channelGeneration }` from immediate
  native readback. No implicit start or fixed-channel lease is created; later
  scanning/association may change the home channel. Foreign owners, native
  operations, cleanup and APSTA are rejected. A connected Station may request
  its unchanged channel as a read-only operation; changes are rejected.
  `channel` must be a valid target-supported integer. `secondaryChannel` is
  `"none"` (default), `"above"` or `"below"`; 2.4 GHz extension channels must
  also be valid under the current country. On 5 GHz the SDK chooses secondary
  channel automatically, so above/below are rejected rather than ignored.
  AP changes may initiate asynchronous CSA; returned readback can still show
  the previous channel. Poll `wifi.status().radio` for the observed home
  channel; returning does not prove every AP client switched successfully.
  The SDK does not persist this setter's channel across stop/start in NVS.
  Errors use `WIFI_CHANNEL_FAILED` with `details.espCode`, `stage` and
  `driverAccepted`. Both new setters serialize admission/write/readback with
  owner release and lifecycle operations. If a write succeeds but readback or
  JS allocation fails, the setting may already have changed; no rollback is
  implied by an exception.
- `wifi.setPowerSave(mode)`
  Set Station modem sleep to `"none"`, `"minimum"`, or `"maximum"` and return
  the active mode. This is separate from ESP-NOW connectionless wake-window
  control. It allows the Wi-Fi helper and its single application owner, with no feature owners.
- `wifi.setTxPower(dbm)`
  Set the shared radio maximum TX power in 0.25 dBm increments from 2 through
  20. It returns the ESP-IDF-mapped actual dBm; read the same value from
  `wifi.status().radio.maxTxPowerDbm` after the radio is started. It requires
  only the Wi-Fi helper and its single application owner. Both setters reject live
  ESP-NOW/CSI owners and pending Radio cleanup before writing the setting.
  They serialize the write and readback with lease release and driver shutdown.
  If the write succeeds but readback fails, the call throws and the setting may
  already have changed; a subsequent status read reports the actual value.
- `wifi.connect(ssid, options?)`
  Start Station mode through the native Future driver. `ssid` is a required
  string or `ByteSource`; optional `password`, `timeoutMs`, `bssid`, `channel`, `scanMethod`,
  `sortMethod`, `minimumRssi`, `minimumAuthMode`, `pmf`, `listenInterval`,
  `failureRetryCount`, `rmEnabled`, `btmEnabled`, `mboEnabled`, `ftEnabled`,
  `oweEnabled`, `saePwe`, `saeH2eIdentifier`, `rssi5gAdjustment`, and the PHY/security
  options below control association. The optional `driver` object accepts the
  implemented `WiFiStationDriverConfig` fields except `ssid`. Use the positional
  SSID and top-level `timeoutMs`; neither belongs inside `driver`. Defined fields
  may appear at the top level or in `driver`, but not both (even equal values
  conflict). An undefined value is treated as absent. Capture merges the values
  before shared authentication, PHY and dependency validation; nested fields do
  not bypass build gates or add advanced credential-owner support.
  `capabilities().stationOptions.driver` reports this input form.
  Unknown fields are rejected. Returns `WiFiConnectResult`:
  `{ connected:true, ssid, ssidBytes, bssid, channel, aid, elapsedMs, rssi, negotiatedPhy }`.
  `ssidBytes` preserves the association event's exact byte span. `ssid` is its
  UTF-8 text, or null for invalid UTF-8; binary bytes never round-trip through text.
  SSID/BSSID/channel/AID come from this operation's association event, copied
  into its completion at IP readiness. Later disconnect/reconnect cannot rewrite
  these identity fields; `connected:true` records the accepted result, while
  `wifi.status()` describes current state. AID is null if absent. `elapsedMs`
  spans native connect dispatch (after initial Radio start) through completion
  publication, including a previous-link drain but excluding lane wait and JS
  delivery delay; it does not change timeout semantics.
  RSSI is an advisory last-beacon dBm reading and negotiatedPhy is the SDK's
  negotiated mode (`lr`, `11b`, `11g`, `11a`, `ht20`, `ht40`, `he20`, `vht20`).
  Both are sampled at result delivery, with BSSID/channel checked before and
  after the queries. Unavailable/mismatched readings or unknown PHY are null.
  This is not an atomic RF sample or an association-generation guarantee for
  reconnects to the same AP. AP supported-protocol flags are not used as PHY.
  Missing/malformed native identity throws `WIFI_CONNECT_FAILED`; allocation
  failure can also prevent delivery after the connection took effect. Neither
  failure undoes the connection: inspect `wifi.status()` and `net.status()`.
  Read assigned addresses and default route through `net.status()`.
  SSID is 1–32 text or source bytes without NUL. Unlike AP config, the SDK's
  Station config has no separate SSID length, so binary Station input also rejects
  NUL. Source bytes are copied during capture and ByteView read leases are released
  before driver work; closing or changing the source afterward cannot change the
  captured native config. Password is at most 64 bytes without
  NUL; exactly 64 bytes must be an ASCII hexadecimal PSK. Native authentication
  still validates the password requirements of the selected security mode.
  BSSID must be nonzero unicast. Channel is an initial scan hint: 0 for none,
  1–13 for 2.4 GHz, or a standard 5 GHz channel on a supporting target. It does
  not create a fixed-channel claim or bypass the SDK's current country rules.
  `scanMethod` is `"fast"` or `"all-channel"` (default all-channel).
  `pmf` accepts `"disabled"`, `"optional"` (default except pure WPA3/OWE), or
  `"required"`. Pure WPA3/OWE requires required and rejects weaker policy.
  Disabled requires eligible authentication and `disableWpa3CompatibleMode:true`.
  It applies a stopped configuration transaction before registering the Future
  and starting the native connection. Existing Station connectivity may be
  disconnected; AP/APSTA modes and foreign Radio owners reject this transaction
  before mutation. Mode and storage use current defaults; a fresh Radio uses
  Station/RAM. The native connection verifies the accepted configuration without
  writing it again after START. Preparation uses the existing bounded lifecycle
  waits; `timeoutMs` and result elapsed time apply to the subsequent connection.
  A failed preparation can leave a diagnostic cleanup suffix: inspect
  `wifi.status().radio` and cleanup status before retrying. No automatic rollback
  of an established connection or persistent FLASH credentials is promised.
  `listenInterval` is 0–65535 AP beacon intervals, applies to maximum modem
  power saving, and does not change power saving itself; 0 asks the SDK for its
  default of 3. `failureRetryCount` is 0–255 retries before moving to another
  AP; nonzero requires all-channel scanning. It never extends timeoutMs.
  `rssi5gAdjustment` is an integer 0–255 dB (default 0), used by the SDK's
  connection candidate selection to prefer 5 GHz APs within this RSSI difference
  from a 2.4 GHz candidate. It does not force a band, change standalone scan
  results, or bypass country rules. `capabilities().stationOptions.rssi5gAdjustment`
  reports target support. On targets without 5 GHz, any explicit value, including
  zero, fails with `WIFI_CONNECT_UNSUPPORTED` before native mutation.
  RM/BTM require their respective 11k/v build gates; FT requires 11r; MBO
  requires MBO support and also enables RM/BTM. Explicit false dependencies or
  BTM/MBO together with a BSSID/nonzero channel hint are rejected, since the
  SDK can clear those hints while roaming. False capability flags mean enable
  requests fail; an explicit false option remains a valid disabled setting.
  `oweEnabled:true` or `minimumAuthMode:"owe"` selects OWE and requires PMF.
  No password, explicit weaker threshold, explicit optional PMF, or contradictory
  `oweEnabled:false` is accepted. OWE does not silently fall back to open Wi-Fi.
  `saePwe` accepts `"hunting-and-pecking"`, `"hash-to-element"`, or `"both"`
  according to SAE/H2E build support. `saeH2eIdentifier` is a secret 1–32-byte
  UTF-8 string without NUL; it requires H2E, conflicts with hunting-only, and
  chooses both methods when saePwe is omitted. Explicit SAE derivation/identifier options require
  a 1–63-byte password and cannot be combined with OWE. The identifier is
  copied into native capture, never returned/logged, and wiped with the whole
  config on failed capture or Future destruction.
  `saePkMode` is `"automatic"`, `"only"`, or `"disabled"`. The native default is
  automatic when PK support is built. Explicit automatic/only requires
  `CONFIG_ESP_WIFI_ENABLE_SAE_PK`; disabled is accepted without it. Only mode
  requires a 1–63-byte password, selects a WPA3 threshold, required PMF and H2E,
  and rejects explicit weaker thresholds, optional PMF, hunting-only PWE or OWE.
  The native supplicant validates the SAE-PK password checksum and peer during
  authentication; capture success does not prove those checks passed. Only mode
  does not retry the request with weaker settings. Automatic mode permits ordinary
  SAE when PK is unavailable at the peer. Available modes are listed in
  `capabilities().stationOptions.saePkModes`.
  `transitionDisable` enables native handling of the peer's transition-disable
  indication; it does not strengthen the initial authentication threshold.
  `disableWpa3CompatibleMode` explicitly disables RSN override compatibility.
  The SDK can then select WPA2 unless the authentication threshold excludes it;
  this option alone is not a security-strengthening switch. Both default false,
  report their build gates in stationOptions and reject true when unavailable.
  Explicit false remains accepted when that security feature is not built.

  HE controls are `heDcmSet`, `heDcmMaxConstellationTx`,
  `heDcmMaxConstellationRx`, `heMcs9Enabled`, `heSuBeamformeeDisabled`,
  `heTrigSuBeamformingFeedbackDisabled`,
  `heTrigMuBeamformingPartialFeedbackDisabled`, and `heTrigCqiFeedbackDisabled`.
  All except the constellation fields are booleans, default false. Each
  constellation is integer 0 (unsupported), 1 (BPSK), 2 (QPSK), or 3 (16-QAM).
  An explicit constellation requires `heDcmSet:true`; then omitted TX/RX values
  default to 3. `heMcs9Enabled` enables HE MCS 8 and 9.
  VHT booleans are `vhtSuBeamformeeDisabled`, `vhtMuBeamformeeDisabled`, and
  `vhtMcs8Enabled`, all default false. In the pinned SDK VHT configuration is
  available on its 5 GHz target. `stationOptions.he` / `stationOptions.vht`
  report these groups. Any explicit PHY option on an unsupported target,
  including false, fails before native mutation. These options do not switch
  configured protocol/band or guarantee a negotiated rate or peer feature.
  Unsupported build options/thresholds throw `WIFI_CONNECT_UNSUPPORTED` with
  `{ option, espCode, espName }`; malformed/conflicting options fail before
  Radio/driver effects. Nested `driver` fields use the same validation and
  duplicate-field rules as the top-level options described above.
- `wifi.disconnect(options?)`
  `options.timeoutMs` is an integer from 1 to 2147483647 ms; omission uses
  `wifi.DEFAULT_TIMEOUT_MS` (15000 ms by default, configurable at build time).
  Disconnect through the native Future driver and return only after the
  station state has converged to disconnected. Once submitted to ESP-IDF the
  disconnect side effect cannot be cancelled; a queued call remains
  cancellable before it starts.
- `wifi.scan(options?)`
  Run an event-driven AP scan and return a `WiFiScanResult` object:
  `{ records: WiFiScanRecord[], complete: boolean, timedOut: boolean }`. Options are `channel`, `channels`,
  `ssid`, `bssid`, `showHidden`, `mode`, `activeMinMs`, `activeMaxMs`,
  `passiveMs`, `homeChannelDwellMs`,
  `coexistenceBackgroundScan`, `maxRecords`, and `timeoutMs`; unknown fields
  are rejected. Hidden beacon records have an empty `ssid`.
  Optional `ssid` matches 1–32 UTF-8 bytes without NUL; omit it to scan any
  SSID. Optional `bssid` matches one nonzero unicast MAC in exact
  `xx:xx:xx:xx:xx:xx` form (hex letters may use either case). Both filters may
  be supplied together. The same exact MAC syntax applies to connect's BSSID.
  Filters are copied during Future capture, then copied to native scan storage
  before SDK submission. At the operation deadline, the framework requests a
  synchronous native scan stop and returns the AP records found so far with
  `complete: false, timedOut: true`. No discovered APs (or expiry before native
  start) returns an empty `records` array. A matching terminal event already
  queued at the deadline is processed normally; successful completion returns
  `complete: true, timedOut: false`. `complete` describes scan completion, not
  whether `maxRecords` omitted additional APs.
  Native stop and result cleanup can add latency beyond `timeoutMs`; it is not
  a strict wall-clock return bound. SDK stop/read/cleanup failures and allocation
  errors still throw; they are not disguised as an empty successful scan.
  Explicit Future cancellation still cancels and discards results. An outer
  `Future.timeout()` wrapper retains its own timeout behavior.
  The native filter copy and Radio reservation remain isolated until SCAN_DONE
  and AP-list cleanup, even when partial records have already been returned.
  A new scan can therefore temporarily report `WIFI_SCAN_BUSY`; inspect
  `wifi.status().scanDraining` and `scanCleanupError` for pending retirement.
  Cleanup failures occurring after a partial result was returned are reported
  through that status; they cannot retroactively reject the completed Future.
  See the [partial-scan implementation record](../investigations/2026-09-13-wifi-scan-partial-results.md)
  for callback-race tests and remaining hardware validation.
  `homeChannelDwellMs` is an integer from 30 through 150, default 30: time on
  the home channel between scan channels. `coexistenceBackgroundScan` is a
  boolean, default false, passed to the SDK's return-home-under-coexistence
  flag; it does not guarantee RF coexistence or bypass fixed-channel owner
  admission. `maxRecords` is an integer from 1 through 32, default 32. It caps
  returned records and the framework's result allocation, not the driver's
  internal scan list or scan duration. Retrieving a capped result still releases
  the SDK list; conversion failure follows the same cleanup path.
  `mode` is `"active"` (default) or `"passive"`. Active mode accepts
  `activeMinMs` (integer 0–1500, default 0) and `activeMaxMs` (integer 1–1500,
  default 120), with minimum no greater than maximum. Passive mode accepts
  `passiveMs` (integer 1–1500, default 360). Supplying timing fields for the
  other mode is rejected, even if their values equal the defaults. These are
  per-channel times; `timeoutMs` remains the whole-operation deadline.
  `channel: "all"` is equivalent to omitting the channel. Explicit time defaults
  follow the fixed SDK constants; this call does not change global scan defaults.
  The sole v1 timing contract uses these fields; `passive`/`dwellMs` are removed
  and rejected as unknown options.
  `channels: { ghz2?: number[], ghz5?: number[] }` selects explicit channels;
  at least one band must contain a nonempty array. Omitted bands are skipped.
  Duplicate/noninteger/invalid channel numbers and unknown fields are rejected.
  `ghz2` accepts channels 1–14; `ghz5` accepts the SDK's 28 standard channels
  36–177 and is rejected on targets without 5 GHz. A numeric `channel` and
  `channels` are mutually exclusive; `channel: "all"` may accompany a list.
  Under the native SCAN reservation, explicit selections are checked against
  the current country and band mode before scan submission. A disallowed
  channel returns `WIFI_SCAN_FAILED` with `ESP_ERR_NOT_ALLOWED`. Explicit
  5 GHz selections require a manual, nonzero country mask; the SDK does not
  expose its implicit regulatory table. When that prerequisite cannot be
  established, the call reports `ESP_ERR_NOT_SUPPORTED` rather than silently
  scanning a smaller set. Unfiltered/all-channel scans still use SDK rules.
  Checks describe admission-time state; the SDK remains responsible for RF
  regulations if its effective country changes during the scan.

  Each record contains `ssid`, `bssid`, `rssi`, `channel`, `authMode`, `hidden`,
  `secondaryChannel`, `band`, `pairwiseCipher`, `groupCipher`, `antenna`,
  `protocols`, `country` and `capabilities`. Secondary channel is none/above/below;
  band is 2.4GHz/5GHz; antenna is 0/1. Unknown enum/channel/antenna values return
  null. Cipher names follow `WiFiCipher`; protocols are reported 11b/11g/11n/
  11a/11ac/11ax/lr flags, not negotiated rates. Capability flags are wps,
  ftmResponder, ftmInitiator, he and vht; false means not reported by this scan,
  not independent RF qualification. SSID decoding reads at most 32 bytes.
  Country is null without a recognizable code; otherwise it contains code,
  environment (indoor/outdoor/X/null), policy (auto/manual/null), startChannel,
  channelCount, maxTxPowerDbm and ghz5ChannelMask (null without target support).
  These describe the AP's SDK record, not the effective local regulatory policy;
  a zero 5 GHz mask is not an unrestricted-channel declaration.
- `wifi.csi`
  Optional bounded Channel State Information capture namespace. It exists only
  when `sys.info.features.wifiCsi` is true; see the Wi-Fi CSI API document for
  `capabilities()`, `open()`, layouts, ownership, and transport format.

Example:

```js
print(JSON.stringify(wifi.status()));
var scan = wifi.scan({ showHidden: true, timeoutMs: 1500 });
print(scan.records.length, scan.complete, scan.timedOut);
wifi.setPowerSave("minimum");
wifi.connect("your-ssid", { password: "your-password" });
print(JSON.stringify(net.status()));
var clock = sys.time.sync({
  servers: ["pool.ntp.org", "time.cloudflare.com"],
  timeoutMs: 10000
});
print(clock.unixTimeMs);
var nextScan = Future.call(wifi.scan, wifi, [{ mode: "passive", passiveMs: 360 }]);
print(nextScan.wait(10000).length);
print(JSON.stringify(wifi.status()));
wifi.disconnect();
```

The application supplies the SNTP server list to `sys.time.sync(...)`. Complete
time synchronization after connecting and before any public HTTPS, TLS, or
secure WebSocket operation so certificate validity dates are checked against a
current clock. Treat SNTP as ordinary wall-clock initialization; applications
that must resist an active network attacker capable of altering both time and
network traffic should establish an authenticated time source.
After synchronization, `Date.now()` and `new Date()` use the same wall clock;
`sys.millis()`, `sys.micros()`, and `performance.now()` remain monotonic uptime
clocks and are not affected by SNTP adjustments.


`radio.driverOwned` records successful native driver initialization even if a later
storage/mode/start step failed. `faultStage` and the original numeric ESP-IDF
`faultError` are null when no Radio fault is pending. `cleanupStage` and
`cleanupError` identify the failed cleanup suffix; `driverState` distinguishes
`uninitialized`, `initializing`, `stopped`, `starting`, `started`, `stopping`,
`faulted`, and `cleanup-pending`. `restartRequired:true` still requires a
**device reboot**, including a failed native init whose SDK cleanup cannot be
proven. A runtime restart does not promise recovery from that fault.
An unsuccessful promiscuous cleanup retains its exact owner until retry succeeds.

Top-level `cleanupStage` / `cleanupError` describe Wi-Fi helper initialization
or runtime cleanup, separately from physical Radio diagnostics. Stages are
`scan-drain`, `connection-drain`, `timer-stop`, `timer-delete`,
`control-unregister`, `ip-unregister`, `scan-unregister`,
`disconnect-unregister`, `start-unregister`, `radio-release`, or `radio-stop`.
They are null when no cleanup is pending. Wi-Fi operations cannot reinitialize
over retained native resources.

Runtime teardown disconnects the helper's established Station connection as
well as cancelling pending operations. Applications must reconnect after a
runtime restart. The dying runtime is detached before waiting for entered
callbacks to exit. Native scan completion/result cleanup and the disconnect/IP
fence must finish before timer, event handlers, queues, lock and netif are
released. Native operation identities remain boot-scoped across helper resets.
A pending native terminal retains these resources without keeping JS roots.
The next runtime attachment explicitly retries unfinished cancellation once
and waits up to one second for native terminals; failure prevents attachment.
Timer callback shutdown has its own one-second wait; entered event callbacks
must exit before their storage can be freed, so these are not an overall
teardown deadline. Cleanup retries only the unfinished suffix.

Helper retirement releases its exact Radio lease and stops the driver only
when no owner remains, serialized against new acquisitions. CSI or ESP-NOW
owners with pending cleanup retain their leases and keep the shared driver
running. A failed final stop retains Radio diagnostics and helper storage;
the lease has already been released and is not decremented again on retry.
Runtime retirement also releases the explicit application owner. Successful
retirement stops but retains Radio driver allocations and its listener. The
internal shutdown/restart path unregisters that listener and waits for entered
callbacks before native deinit; failed unregister reports `channel-unregister`
and retains driver ownership for retry. The mutation mutex uses static storage
and remains boot-lived. These paths do not establish recovery from a native
init failure that still reports `radio.restartRequired:true`. Public
`wifi.driver.restart()` is a Candidate for healthy initialized stopped sources; see
[its current admission and cleanup contract](wifi-driver.md#driver-restart).

An accepted asynchronous scan keeps a native operation until `SCAN_DONE` has
arrived and its driver AP list has been consumed or cleared. Cancelling its
Future or restarting the JavaScript runtime detaches the JS token and requests
`scan_stop`; it does not make the scan slot immediately reusable. `scanning`
continues to report the outstanding native scan, while `scanDraining` indicates
that its public consumer has detached. `scanCleanupError` is the native stop or
AP-list cleanup error, or null. A new scan/connect returns busy while this scan
or its result cleanup remains pending. A successful stop is not resubmitted;
failed AP-list cleanup can be retried by a new operation request or runtime
teardown. The runtime poller does not repeatedly retry failed cleanup.

If the terminal event never arrives, the scan remains quarantined across runtime
restart; this release does not force a shared-radio reset to hide the condition.
Scan configuration storage remains native and independent of the destroyed
Future. A completed but unread scan also releases its AP list when the Future
is destroyed. Connection handoff uses the separate native barrier described below.

Connect timeout notifications carry their operation generation. A late timeout
for an older connection, or a timeout while the registered operation is
`disconnect`, is ignored. Before submitting a new connection's configuration,
the runtime waits up to 1,000 ms for the previous connect timer callback to exit.
If that barrier fails, the connection call fails before changing configuration
or calling `esp_wifi_connect()`. `connectTimerError` reports the native timer
barrier/start error, or null; a later explicit connection attempt can retry the
barrier. Runtime teardown also attempts it and retains the native timer if
it fails. A prepared connect Future cancelled before it starts has no driver
side effects. Cancelling an already started connection retains its existing
disconnect protection. These timer guarantees do not supply an identity cookie
to native DISCONNECTED/GOT_IP events.

Connection cancellation, timeout and runtime teardown preserve a native
`connectDraining` obligation after detaching the public Future. Before a
replacement connect writes its configuration, it requests disconnect and waits
up to that call's `timeoutMs` for all three conditions: native DISCONNECTED,
a marker draining the default event loop's older IP notifications, and return
of the native disconnect call. This handoff wait precedes the new connection's
own timeout and the timer callback barrier has its separate 1,000 ms limit.
GOT_IP cannot complete a disconnect Future or a cancelled connection; during
handoff it cannot mark the replacement connection successful. Scans are also
rejected while the connection is draining.

`wifi.disconnect`, `wifi.stopAP` and `wifi.stop` share `WiFiWaitOptions`:
`{timeoutMs?: number}`. Omission, `undefined`, `{}` and `{timeoutMs:undefined}`
use each operation's default. Explicit lifecycle timeouts must be integers 1..2147483647;
scalars, null, arrays, unknown fields and extra arguments are rejected before
native mutation. The native shared wait additionally checks the converted tick interval before admission.

| Operation | Default wait | Successful result | Deadline / failure consequence |
| --- | --- | --- | --- |
| `disconnect` | `wifi.DEFAULT_TIMEOUT_MS` | `WiFiStatus` after native disconnected state | Throws; a submitted disconnect continues and its native slot may remain quarantined |
| `stopAP` | 1000 ms | `WiFiStatus` after AP retirement; APSTA preserves Station | Throws; retains unfinished cleanup for an explicit retry |
| `stop` | 1000 ms | `WiFiStatus` after admitted whole-Radio shutdown | Throws; retains unfinished cleanup; does not forcibly close foreign owners |

These budgets do not preempt synchronous SDK calls. `stop` and `stopAP` share
their budget across native cleanup waits; `disconnect` remains a native Future
operation. An options object does not make the three operations use identical
scheduling or cancellation. For example:

```js
wifi.disconnect({ timeoutMs: 5000 });
wifi.stopAP({ timeoutMs: 5000 });
wifi.stop({ timeoutMs: 5000 });
```

`wifi.disconnect()` completes when native DISCONNECTED is observed; its success
does not promise that the IP-event fence has finished. Repeating disconnect
after that terminal does not wait for another event. `disconnectCleanupError`
reports a native disconnect, netif-down verification or fence-post error, or
null. A full event queue retains the obligation; a later explicit connect or
disconnect retries only the missing fence, without resubmitting a successful
disconnect. A failed native disconnect can be retried explicitly. An exhausted
native epoch reports invalid state and never wraps; device reboot is required.

If DISCONNECTED never arrives, the native slot remains quarantined across
runtime restart. Native NOT_CONNECT/NOT_STARTED errors alone do not prove that
old events have drained. The barrier relies on the pinned SDK's default STA
handler being registered first and synchronously stopping netif/DHCP before the
FIFO marker is posted. If netif remains up (including unqualified roaming/IP
retention configurations), handoff fails conservatively and keeps its diagnostic.
This is not yet complete physical Radio teardown.

These diagnostics contain no credentials.

The native Radio has at most 16 live leases. Released lease copies cannot alter
another client; exhausted identity space fails without wrapping. Matching primary
and secondary channels can share fixed-channel ownership. Releasing one lease
preserves the others; conflicting channel requests fail before driver mutation.
`fixedChannelOwners` counts those leases. Observed driver channel changes advance
`channelGeneration` and latch `conflictedChannelOwners`; a read never forces the
driver back to an old channel. The shared Radio listens for home-channel,
STA-connected and AP-start events on the default event loop, even when the Wi-Fi
JS module is disabled. The listener has no session/JS pointer, survives stop,
and is removed before driver deinit and registered once on reinitialization. It reads the current SDK channel rather
than replaying delayed event payloads. A revision check prevents an older
concurrent query from overwriting a newer observation. `channelObservationError`
is the raw SDK read error, or null; packet admission fails closed while that
observation is unavailable. A later successful observation clears this read
error, but does not clear a fixed owner's latched conflict.

If a home-channel observation overtakes an explicit channel write, the write
cannot commit a stale fixed-owner claim. The returned error does not promise
rollback of a driver write that already occurred; status reports the next actual
observation. Event-loop creation or listener registration failures report
`event-loop` / `channel-handler` in the Radio fault details; with no owners,
internal shutdown can clear these pre-driver failures before another start.

Scan and connect reserve one shared Radio operation before scan submission or
connection handoff/configuration. Any live fixed-channel lease rejects a new
scan/connect with the existing operation error and native invalid-state reason;
the existing link and fixed session are preserved. This conservative rule also
applies to a scan restricted to the current channel: a same-channel exception
has not been qualified. A connection's channel hint does not constrain its
internal scan to that channel. Following (`"current"`) leases do not acquire a
fixed-channel constraint and are not rejected solely for sharing the Radio.

Conversely, while that reservation is active, new fixed-channel claims, mode
changes and the existing global power-save/TX-power setters are rejected.
`radio.activeOperations` counts those native reservations and Raw TX submissions
retained through native retirement. Raw TX temporarily pins its observed channel
while in flight, so it cannot overlap a scan/connect reservation. This count is
not the number of public Futures or all wireless feature operations.
A reservation retains its exact Radio owner. Scan cancellation retains it until
SCAN_DONE and AP-list cleanup; connect cancellation/timeout retains it through
the disconnect/IP fence. SDK submission must also have returned before release.
An unread scan result or failed cleanup can therefore keep the count at 1.
Callbacks never wait for the Radio mutation mutex: the runtime task releases
completed reservations, including after runtime reattachment. A successful
connected STA releases its reservation and can again share its actual home
channel with a fixed owner. A cancelled connection's handoff keeps its existing
reservation until the replacement operation completes; it does not create a
window in which another owner can claim the channel.

`requestedMode` is the union of current leases. The effective `mode` can retain a
superset until the next safe stopped start. Internal stop/shutdown require all
leases to exit first. Public start/stop and Candidate driver restart are registered;
restart requires the stopped-source admission described in [Driver restart](wifi-driver.md#driver-restart).
Wi-Fi control events update native status and Future results independently of
lossy observation queues. Timer callbacks defer disconnect to the runtime.

#SSID delivery uses the sole v1 `ssid`/`ssidBytes` pair in connect/startAP results,
Station/AP status, scan records and Station association/disconnection watch data.
`ssid` is UTF-8 text or null; `ssidBytes` owns the bytes and can be passed to a later
call. AP status keeps both null if sampling fails. Station status describes the
cached requested SSID (empty string/empty bytes before a request), whereas connect
results use the association event. Scan records preserve the SDK's NUL-terminated
SSID prefix, up to 32 bytes; they cannot recover bytes the SDK did not expose.
Text nullability does not mean disconnected or hidden. Passwords are never included.

## Association status and operation results

`wifi.status()` adds `associated`, `bssid`, `channel`, `aid`, `rssi`, and
`negotiatedPhy`. Association may precede the IP-ready `connected` flag.
BSSID/channel/AID describe the last accepted association and become null when
native disconnect begins; channel is the association channel, while
`status.radio.channel` is the current Radio channel observation. RSSI/PHY use
the same advisory guarded queries as the connection result. Status does not
start the driver. These fields contain no credentials. Handler cleanup exposes
`connected-unregister` if unregistering the association callback needs retry.

### Configuration transaction diagnostics

`wifi.status().radio.configuration` is null until a native configuration
transaction runs; afterwards it describes the latest attempt, including across
stop/shutdown until another transaction replaces it. Fields are `stage`, `error`
(the original ESP error, zero on success), `mutationAttempted`,
`rollbackAttempted`, `rollbackComplete`, `rollbackStage`, `rollbackError`, and
`persistentMutationPossible`. No configuration values or credentials are exposed.

`startAP` uses this stopped-driver transaction for mode and AP config, with
pre-write snapshots and validated readback. Configuration failure attempts RAM
restoration and leaves the lifecycle token/netif for explicit `stopAP` cleanup. A start
failure after a completed config transaction is still reported separately by
Radio fault state; `configuration.stage:"complete"` does not mean start succeeded.
Rollback failure leaves Radio faulted for explicit shutdown; it is not retried
from retained credential buffers. `rollbackComplete` describes verified runtime
fields/mode and accepted storage selection, not the old persistent NVS image.
Flash-mode native writes can leave persistent side effects even after runtime
rollback; `persistentMutationPossible` never asserts that NVS was restored.

The shared option-name and enum validator compares complete string lengths and
contents, so embedded NUL suffixes cannot match a valid prefix. Invalid names
are rejected instead of being silently ignored by later property lookups.

The transaction core and interface executor now back public `wifi.configure`.
Capture, default selection, disconnect admission and per-call errors are
connected; see the configure method below for the supported contract.

The internal stopped transaction also accepts country, per-interface protocol/
bandwidth, and power-save controls, with snapshots and verified restoration.
Country setters may persist even with RAM storage; their failures retain a fault
after successful RAM restoration. TX-power and band-mode setters require a
started driver and are not part of this stopped transaction. Public configure supplies these controls; `startAP` passes no controls. See the
[implementation and pending validation](../investigations/2026-09-08-w02-config-controls.md).

`wifi.status().radio.activation` reports the most recent post-start TX-power
attempt using the same metadata fields, or null if none has run. It is separate
from `configuration`; later stop/shutdown and starts without these controls
preserve the record. Its `rollbackComplete` only verifies restoration of the
power sampled after native START. It does not reverse START or prior configuration.
Failures retain the lifecycle reservation and publish no new owners; failed
power restoration adds `cleanupStage: "activation-rollback"`. Public `configure` supplies these post-start controls. The SDK requires startup
before setting power, so this cannot promise that startup beacons use the new
ceiling. See [implementation and pending tests](../investigations/2026-09-08-w02-start-controls.md).

The AP adapter now uses the common interface executor. One exact lifecycle
reservation spans stop, helper retirement, initialization, helper preparation,
configuration, start events and mode readback. It publishes a new AP lease only
on successful handoff. Preparation failures may still own the original lease;
`stopAP` handles either that lease or a retained native lifecycle token. Failed
start/readback publishes no usable AP owner, and `radio.lifecycleActive` remains
true until explicit cleanup. Internal handoff supports separate Application,
Station and AP identities; the native executor can prepare either or both helpers,
and public configure can start APSTA. Advanced credential-owner inputs remain pending.

Failure after admission retains the central cleanup reservation. `stopAP` may
finish a pending AP-only configuration/stop (preserving the accepted stop-only
choice); it cannot finish a mixed-interface or
runtime cleanup transaction. `wifi.stop` dispatches pending configuration cleanup
to the central executor, which retires AP and Station helpers before shutdown.
Retrying cleanup never replays configuration using the caller's released input
buffers. AP inputs are checked against the current country before configuration
writes (`radio.configuration.stage: "ap-regulatory"` on that failure).
Normal AP/APSTA `wifi.stop` now uses this same retirement sequence with
`configuration-finish-stop` as its final stage, retaining initialized driver
storage. The stop-only choice survives helper reset and explicit/runtime retries;
failed configuration cleanup continues to request shutdown. See the
[implementation and pending tests](../investigations/2026-09-08-w02-unified-stop.md).
The internal executor also has an opt-in Station disconnect step. Exact owner
admission precedes disconnect; an outstanding Future/native operation still
blocks it. It retains the token through native DISCONNECT, netif down and the
matching IP-event fence, with a bounded 1000 ms native wait. Failure is recorded
as `configuration-disconnect`; central cleanup retries that obligation before
retiring helper storage. Accepted disconnect calls are not resubmitted while
waiting for their terminal/fence. Public configure exposes this as
`allowDisconnect`. Selection admission requires explicit permission
for any running AP, even when a client snapshot would be empty. See the
[implementation and remaining contract work](../investigations/2026-09-08-w02-config-disconnect.md).
Internal configuration defaults and exact lifecycle admission are resolved under
one Radio mutex before releasing helper owners. A healthy configured driver
preserves mode/storage/started; a cold request infers mode from interface inputs
(default Station), with RAM storage and start enabled. Starting AP currently
requires a complete AP configuration. The captured-input executor uses the same
path as `startAP`; Station readback requires semantic equality of all supported
fields. This now backs public `configure`. See the
[implementation and deferred tests](../investigations/2026-09-08-w02-config-selection.md).
Configuration rollback does not recreate previous helpers or restore the prior
running lifecycle. The executor requires explicit cleanup after a failed attempt.

AP errors combine native Radio reboot requirements with netif detach failures
in `details.restartRequired`; false does not promise that runtime restart can
undo configuration or persistent side effects. The original ESP error and
stage remain available, with pending cleanup visible in Radio status.

### Station helper setup and cleanup failures

Station netif creation uses explicit `esp_netif_new`, attach and default-handler
steps. Allocation/attach/handler failure returns an error instead of entering
the SDK convenience constructor's assert/abort path. The Radio lease is reserved
before attaching a netif, so failed owner admission does not replace SDK netif
state. `status().setupStage`/`setupError` retain the last helper setup failure
through successful unwind and clear after the next successful setup.

`stationNetifCleanupError` is null or the native detach error;
`stationNetifRestartRequired` reports a quarantined Station netif requiring a
device reboot. The fixed SDK destroys the interface driver even when clearing
its configuration fails, so that error retains the netif/helper storage and
never retries the possibly dangling driver handle. `cleanupStage:"netif-detach"`
identifies the retirement step; it can also be pending on event-queue admission
or a callback timeout, which does not set the permanent detach-error fields.
Common Wi-Fi operation errors include setupStage,
cleanupStage and restartRequired; that flag combines native Radio and Station
netif requirements. A runtime restart is not proof of recovery.

Internal Station/AP prepare/retire hooks now work under an exact stopped
lifecycle token without acquiring owners or releasing the coordinator's token.
Retirement now serializes the SDK netif STOP action and detach on the default
event-loop task, then drains already-enqueued derived events through a second
marker before freeing the netif. A timeout retains native storage; retries resume
the unfinished suffix. This marker does not terminate future event producers.
The reviewed SDK build also cancels a netif's delayed IP-lost timer on stop and
before destruction, so its pointer cannot later refer to a reused netif address.
This targeted fix uses a build-local SDK source copy without disabling delayed
IP notifications for live interfaces. SDK source changes require review before
rebuilding. Runtime regression coverage is pending.
Runtime cleanup now drains Station operations before taking a shared
application/Station/AP lifecycle reservation. It retains that reservation through
AP retirement, Station helper retirement and driver shutdown; a new runtime
retries this central suffix before attaching the AP helper. Failures use
`cleanupStage` values `configuration-admission`, `configuration-stop`,
`configuration-ap-retire` or `configuration-shutdown`, with the original native
error in `cleanupError`. Internal helper failures retain their existing stages.

`accessPointNetifCleanupError` and `accessPointNetifRestartRequired` expose a
permanent AP detach failure, including one reached through central cleanup.
They are null/false without SoftAP support. Common Wi-Fi errors include this AP
requirement in `details.restartRequired` alongside Radio and Station failures.
The interface executor additionally reports `configuration-initialize`,
`configuration-station-prepare`, `configuration-ap-prepare`,
`configuration-ap-slot`, `configuration-commit` and `configuration-resume` in
`cleanupStage` after failure. The underlying configuration result and Radio fault
remain separate diagnostics. Public configure also includes per-call execution
progress in its error details; advanced credential-owner inputs remain pending.

APSTA `stopAP` uses a separate `eventPhase: "ap-stop"` boundary, requiring
AP_STOP and a marker while Station remains live. Its native retry state preserves
accepted mode changes and consumed event fences; failures use `ap-stop-*` stages.
The AP helper retires its netif through the event-task detach/IP fence before
releasing its lease. An explicit whole stop or runtime teardown can adopt the
same token after Station/native operations drain. Admission failure during
runtime takeover reports `cleanupStage: "ap-stop-admission"`. See the
[public handoff and pending verification](../investigations/2026-09-08-w02-ap-stop-public.md).

### Radio start/stop event completion

Radio now waits for the expected native interface START/STOP events and a
subsequent default-loop marker before publishing successful lifecycle completion.
This applies to the existing Station start and exclusive SoftAP start paths, and
to internal restart/configuration handoff. A successful SDK call alone does not
complete this wait. Native control updates remain independent of lossy watch
queues. This does not establish IP readiness, RF qualification or AP client health.

Startup event waiting is bounded at 5000 ms; each stopped-event retry waits up
to 1000 ms, in tick granularity. SDK calls retain their own blocking semantics.
Waits use the existing native-wait/cooperate hooks to preserve watchdog and
runtime interruption handling. Interrupts retain native ownership and cleanup
obligations. No runtime or JS pointer is stored in marker events.

An accepted stop is submitted once. Missing STOP or a full marker queue retains
`stop-events` cleanup state; a subsequent cleanup attempt waits or re-posts only
the missing marker. New START/STOP observations invalidate older marker revisions,
and markers from earlier lifecycle identities cannot acknowledge a new phase.
Startup wait failure records `start-events`; completing a later stop does not
clear that startup fault. Explicit driver shutdown/reinitialization is required.
Exhausted marker identities never wrap and set `restartRequired`.

`status().radio` adds `eventPhase`, `eventIdentity`, `eventExpectedMask`,
`eventSeenMask`, `eventStoppedMask`, `eventLiveMask`, and `eventFencePending`. Mask bit 1 means Station,
bit 2 means AP, and bit 4 means NAN-Sync. Expected/seen describe the latest transition (also retained when
phase is idle); live is the latest observed interface state. `started` can be true
while a startup fault is pending because the SDK accepted start; inspect
`driverState`, `faultStage` and `eventPhase` for completed lifecycle state.
Mesh-capable builds also report `clients.wifiMesh`. The internal Mesh binding
retains an exclusive Radio lifecycle through native shutdown, physical STOP,
netif retirement and restoration of the previous configuration. Restoration
ends stopped; an original AP mode requires `allowApRestart` because acceptance
temporarily starts that AP. These owner diagnostics do not add public Mesh
Session methods. Root DHCP/IP readiness is distinct from ToDS/Internet reachability.

NAN-enabled builds also report `clients.wifiNan`; `mode` and `requestedMode`
can report `nan`. The internal NAN lifecycle reserves a stopped, owner-free
Radio through START, STOP, SDK cleanup, netif retirement and restoration of
the previous stopped mode/storage. A failed cleanup retains that owner and
its `nan-*` diagnostic stage. The public `wifi.nan` API has separate build gates
and Session contracts. Discovery readiness includes the default-loop fence and an
up netif; usable datapath/IPv6 readiness remains a separate operation.
Internal band preparation uses `eventPhase: "restart"` while waiting for the
SDK's own STOP/START cycle. `eventStoppedMask` shows STOP observations in the
current stop/restart phase. In restart, `eventSeenMask` counts START only after
that interface's STOP; another STOP clears its START evidence. The queue marker
must also match the latest revision before the outer STOP can begin.
Internal AP restoration starts AP-only, uses the SDK's supported CSA path to
restore the saved actual channel, and waits for matching band/current/home
observations under the native wait budget. It retains the original AP configuration.
For APSTA it then enables Station; `eventPhase:"sta-start"` requires STA_START,
both interfaces live and a matching queue marker. Owner publication follows the
remaining configuration/power checks and storage-policy commit. This internal
path is shared with the public `wifi.driver.restart()` coordinator.
The marker identity belongs to the framework and is not a native SDK callback
cookie. Native terminal events and their queue ordering are still required.

The Station adapter always validates its exact Radio lease and fault state, even
when an earlier callback set its local started bit. Public configure/APSTA uses
this event barrier. Short C5 lifecycle tests do not establish all-target RF or
long-duration qualification.


### `wifi.configure(options)`

Synchronously apply a validated configuration, then return `WiFiStatus`. Available
with the Wi-Fi feature; AP/APSTA require SoftAP support. This method does not
connect Station: use `wifi.connect` separately. `radio.storage` reports RAM/Flash
or null before initialization. Capability `configure` reports registration;
`apsta` reports that a combined start is available. `stopAP` also has an APSTA
removal path; Station continuity has short C5 test evidence and remains subject
to target and RF qualification.

```js
var configured = wifi.configure({
    mode: "apsta",
    storage: "ram",
    start: true,
    accessPoint: { ssid: "esp32-config", password: "change-this-pass", channel: 6 }
});
print(configured.radio.mode);
// After disconnecting Station and closing other Radio users:
wifi.stop();
```

`WiFiConfigureOptions` accepts mode, storage, start, allowDisconnect, station,
accessPoint, country, protocols, bandwidths, txPowerDbm and powerSave. Unknown
fields and invalid types are rejected. A supplied interface object is a full
replacement with constructor defaults; omitted interface objects are left alone.
Starting AP requires a complete accessPoint object even if an old config exists.
A healthy initialized driver preserves omitted mode/storage/start. A cold request
infers mode from supplied interfaces (otherwise Station), uses RAM and starts.
`mode: "apsta"` explicitly adds both interfaces to an existing driver.

Station/AP raw configs use the fields listed in the source types, including
`pmf: "disabled" | "optional" | "required"`, and AP `beaconIntervalTu` instead of
milliseconds. The former development boolean `pmfRequired` is removed, without an alias.
Disabled invokes the dedicated SDK operation inside the stopped transaction before
START; it requires eligible authentication and Station `disableWpa3CompatibleMode:true`.
WPA3/OWE require PMF, so an explicit weaker policy is rejected. Direct `wifi.connect()` uses the same stopped transaction for disabled PMF. Beacon interval is 100..60000 TU in multiples of 100. SSID accepts
text or `ByteSource`, copied before native work: 1..32 bytes; binary AP SSID may
contain NUL, Station SSID and text inputs may not. Enterprise and DPP credentials
use their dedicated profile/Session APIs; these generic configuration options
do not accept credential-owner descriptors from the design inventory.
Existing security/PHY target gates and conflict checks apply.

Country accepts a two-character uppercase code/01 or details with code/policy.
When supplying range/environment/5 GHz mask, include both startChannel and
channelCount. Protocol/bandwidth objects use station and access-point keys, each
with ghz2/ghz5 protocol arrays or ghz2MHz/ghz5MHz widths. Arrays are complete,
nonempty, distinct protocol sets. Bandwidth is 20 or 40 MHz; the pinned SDK does
not support HT40 with 11AC/AX. Only currently active bands may be changed.
TX power uses 2..20 dBm in 0.25 dBm steps, requires a started result, and is applied
after START; initial beacons are not guaranteed to use the requested ceiling.

`allowDisconnect` defaults to false. An established Station or any running AP
requires true for this stop/configure/start transaction. A zero-client snapshot
does not exempt a running AP because a peer could associate before STOP. Pending
Futures/native operations, wake locks and ESP-NOW/CSI/foreign owners still block
admission. Previous Station connectivity and AP client sessions are not restored.
`stopAP` removes AP while retaining Station resources. To stop both interfaces,
use `wifi.stop` after Station disconnect and other operations/owners have ended.

Native failures use `WIFI_CONFIG_FAILED` or `WIFI_CONFIG_UNSUPPORTED`, operation
`wifi.configure`, and details with stage, option, espCode/espName,
lifecycleAdmitted, stopAttempted, cleanupPending, restartRequired, configuration
and activation. Configuration/activation metadata is included only when reached
by this failed call. Capture errors do not query or mutate the driver. Type/range
and JS-allocation errors preserve their ordinary exception types; native memory
failures retain the ESP error code in configuration error details.

After admission, failure retains cleanup obligations: use `wifi.stop()` to finish
cleanup before another attempt. Rollback covers runtime configuration, not prior
connectivity or persistent NVS history. Country setters can persist even with RAM
storage; inspect persistentMutationPossible. A result-allocation error can occur
after successful configuration: inspect `wifi.status()` before retrying. There
is no automatic replay or rollback caused only by failure to allocate the return
object. No credentials appear in configuration errors or return metadata.

Implementation, deferred tests and remaining work are recorded in
[the public configuration handoff](../investigations/2026-09-08-w02-public-configure.md).

`wifi.monitor` now provides bounded single-frame capture and retained byte access; see
[Wi-Fi Monitor](wifi-monitor.md) for options, lifecycle, ownership and remaining capabilities.

`wifi.vendorIe` provides Candidate `set`, `clear`, and `status` for copied management-frame
Vendor IEs on configured interfaces, including pre-start capture. See [Wi-Fi Vendor IE](wifi-vendor-ie.md) for slot
ownership, startup handoff, cleanup and pending reception support.

`wifi.action.send(options)`、`wifi.action.status()` 与 `wifi.action.capabilities()`
已接入 Candidate，详见 [Action TX/ROC](wifi-action.md)。`wifi.action.remainOnChannel(options)`
及 `WiFiRocSession.status()/wait()/close()` 已接入；完整故障恢复仍待完成。

Enterprise credential configuration and native Future controls are documented in
[Wi-Fi enterprise](wifi-enterprise.md); check `wifi.enterprise` presence.

C5 HE builds provide Candidate TWT probe and individual Agreement setup/status/close/suspend/resume.
See [TWT](wifi-twt.md) for parameters, independent timeouts, per-agreement Radio
ownership and retained cleanup. Broadcast Agreement remains
under implementation.

With Enterprise configured, `wifi.stop()` on an idle disconnected Station retires
SDK credentials under the same lifecycle before releasing Station/AP helpers.
It retains the configured profile; after starting again, explicitly enable
Enterprise before connecting. Cleanup failure keeps the transaction for a later
stop or runtime-exit retry. See [Enterprise lifecycle](wifi-enterprise.md).

Station WPS enrolment is available through [`wifi.wps`](wifi-wps.md) with BSD TCP/IP and IPv4. Its PIN/credential Session is Candidate; registrar-enabled builds also expose AP WPS Sessions. Runtime and RF qualification remain pending.
