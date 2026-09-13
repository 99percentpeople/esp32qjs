# C5 / S3 flash and short Wi-Fi acceptance

## Target and workspace evidence

This run uses firmware `c90047b` plus the current uncommitted Wi-Fi changes.
Evidence is under `build/wifi-two-targets/`. No workspace erase, frontend build,
commit, push, or parent gitlink update was performed.

| Target | Current evidence | Flash status |
| --- | --- | --- |
| XIAO ESP32-C5, `10:bd:a3:c8:54:e8` | ROM MCU/MAC, 8 MiB Flash; running Agent confirms 8 MiB Quad PSRAM | Hash verified; Agent reconnected |
| XIAO ESP32-S3 Sense, `e8:f6:0a:8b:bd:c0` | User confirmed Board; ROM MCU/MAC re-probed, 8 MiB Flash, embedded 8 MiB Octal PSRAM; partition table read from Flash | Hash verified; Agent reconnected |

The current esptool C5 `get_chip_features()` does not report PSRAM. Its ROM
profile consequently says **unknown**, which is not evidence of absent PSRAM.
The Agent probe restored the C5 hardware profile to Quad / 8,388,608 bytes.
The build explicitly includes this PSRAM configuration.

C5 Build job: `a5a121d6a0a2a8cbb0c5f4b2`.
Artifact: `84bd29351d10f68c0c7e989b4cafe458d2b1ffb765541a928f79d64390b3c61e`.
Flash job: `e108ddf7e1e82eb06c53515c`.
Application: 3,134,256 / 3,145,728 bytes (11,472 bytes remaining).
Both targets have workspace offset `0x390000`, size 4,653,056 bytes. The C5
flash used `workspaceAction: preserve`. Its 2,752-byte `index.js` is unchanged,
SHA-256 `7ebfb188f0fd0c8e7d53e9774aaff2d08d14ea84613cf66d3a804854e1b0e1d4`.
After flash: boot ID `95a8ef7b9cbfb95b`, workspace mounted, safe mode false,
startup failure count zero, PSRAM size 8,388,608 bytes.

## C5 public API results

Programs were generated from current `tests/js/flash_data/modules` sources,
executed through the authenticated device executor, and removed afterward.
Credentials were injected only for network cases and omitted from recorded
write requests. Each run records its source hash, boot ID, and runtime generation.

The following 12 cases passed:

- `wifi/offline`, `wifi/scan-deadline-hardware`, `wifi/native-policy-hardware`.
- `wifi_csi/offline`, `wifi_csi/packet-hardware`.
- `wifi/monitor-configure-hardware`, `wifi/monitor-retained-hardware`,
  `wifi/monitor-batch-hardware`.
- `wifi/raw-tx-hardware`, `wifi/driver-lifecycle-hardware`.
- `wifi/network`, `wifi/ap-lifecycle-hardware`.

The 1 ms scan returned `{complete:false,timedOut:true,records:[]}`; native
retirement completed and the next scan completed normally with three records.
The policy case accepts all 64 numeric frame pairs and zero/unlimited rate,
preserves caller-selected minimum power save across Monitor/CSI open, roundtrips
CSI requested configuration, and accepts INT32_MAX generic timeouts for idle
Raw TX open and disconnect. It does not wait for an INT32_MAX deadline.

Raw TX completed one-shot, bounded batch and finite periodic probe requests.
This is not RF reception proof for every management subtype. Monitor saturation
observed 58 queue drops and 21 pool drops; retained View/Source data survived
Session/Batch close and GC, and the last owner returned its pool.

After one warmup, three driver reconstruction cycles returned all Radio owners
and active operations. Comparable stopped-state memory:

| Region | Free before / after | Largest block before / after |
| --- | --- | --- |
| Internal | 92,099 / 92,063 | 51,200 / 51,200 |
| PSRAM | 3,971,508 / 3,971,508 | 3,932,160 / 3,932,160 |

The initial new policy test incorrectly expected CSI's `status.requested` field
on Monitor. That test assertion was corrected to the documented Monitor state
contract; the rerun passed. No production firmware defect was established by
that failure. The two new JS cases pass the vendored MQuickJS syntax checker.

## Runtime teardown and remaining acceptance

Two runtime restarts passed: shared CSI/ESP-NOW pending receives, then Monitor
with retained View/Source and a pending receive. Runtime generations advanced
1 → 2 → 3 under the same MCU boot ID. After each normal startup quiet interval,
all Radio owners and active operations returned to zero, CSI reserved slots
returned to zero, no Monitor session remained, and no lifecycle/fault state
remained. Evidence: `hardware-runs/runtime-restart-quiet-f1314346/`.
The workspace entry file remained unchanged after the complete test run.

## S3 Sense flash and API acceptance

S3 Build job: `d1c01f34693676579f2af9aa`.
Artifact: `a876f209bea10f5bd9a192f02f6adf9f80ab090aa2d0c1310ea874ecead181a2`.
Flash job: `f2a06a09a5026a3aad66b57d`.
Application: 2,952,416 / 3,145,728 bytes (193,312 bytes remaining).
Flash completed with verified hashes and Agent reconnection. Boot ID:
`482305715d33a7e8`; workspace mounted, safe mode false, startup failure count
zero, Octal PSRAM size 8,388,608 bytes.

Before flashing, the complete 4,653,056-byte workspace was backed up through an
exclusive serial lease, SHA-256
`7ca33c3bd1c1f2a765a072ecbfd58b4fb9111159e74000a626859f6c89f000ad`.
A host LittleFS reader opened that backup without write callbacks and extracted
the 19,170-byte `index.js`, SHA-256
`28ab84a380fa04263b84aca8b982588c9147d52b0d744ccf77c01f7b483b48ea`.
This workspace is a camera ESP-NOW sender. Tests pause it through its existing
`application.stop()` and restore startup afterward; no source rewrite is needed.
Reading the whole entry file through the new Agent confirmed an identical
19,170-byte file after flashing. Before pausing, the OV3660 sender reported 870
frames and 1,740 successful packets, with zero capture/send failures. Those
counters establish sender operation, not peer reception.

The same 12 functional cases listed for C5 passed on S3, including its
`wifi-csi-legacy/1` adapter. One initial `wifi/network` attempt timed out at
15,000 ms. Inspection afterward found Wi-Fi connected, no Radio fault, and
the clock unsynchronized. The immediate complete rerun passed, including time
sync and reconnect after a failed association. The evidence supports a transient
network/time-sync observation; the exact timeout trigger was not established,
and no production repair is claimed. The failed attempt remains recorded at
`hardware-runs/wifi-network-185434a2/`; all attempts are indexed by
`s3-functional-results.json`.

Comparable stopped-state memory after one warmup and three driver rebuilds:

| Region | Free before / after | Largest block before / after |
| --- | --- | --- |
| Internal | 124,163 / 124,143 | 51,200 / 51,200 |
| PSRAM | 3,970,464 / 3,970,460 | 3,866,624 / 3,866,624 |

Both S3 runtime-restart checks passed: shared CSI/ESP-NOW pending receives,
then retained Monitor View/Source plus a pending receive. Runtime generations
advanced 1 → 2 → 3 under the same boot ID. After each startup settled and the
workspace sender was paused, Radio owners/operations and CSI reserved slots
returned to zero, no Monitor sessions remained, and no lifecycle/fault state
remained. Evidence: `hardware-runs/runtime-restart-quiet-0714a741/`.

The final restoration restart reached runtime generation 4 under the same MCU
boot ID. Startup became `healthy`, safe mode stayed false, and failure count
was zero. The original sender resumed: frames advanced from 873 to 939 across
two samples, with zero capture/send failures. The complete workspace entry file
still matches its pre-flash SHA-256. Evidence: `s3-restored-samples.json`,
`s3-final.json`, and `s3-workspace-index-final.js`.

## Deferred hardware acceptance

BLE/GATT, exhaustive
management-frame RF reception, dual-device ESP-NOW, 500 complete lifecycle
cycles, long soak and broad coexistence qualification are not run. These short
checks do not promote feature stability or constitute full F-HARDWARE acceptance.
