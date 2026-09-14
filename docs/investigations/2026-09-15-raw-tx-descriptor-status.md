# Raw TX descriptor completion status

## Contract

Raw TX uses the shared sole-v1 `completion: {status, native: {domain, code, name}}`
shape. One-shot and Session send results use the same converter.

- `status` continues to come from the SDK's `wifi_tx_status_t` observation, which
  also drives the broker and Session counters.
- When the reviewed descriptor hook observes TX-info status, `native` contains
  `domain: "esp_wifi_tx_descriptor_status"`, the unmodified byte as `code`, and
  its reviewed label as `name`. There are no parallel `macStatus*` result fields.
- Without that observation, `native` contains the original `wifi_tx_status_t`
  namespace, value and SDK symbol.
- An unknown descriptor byte remains in the descriptor namespace with a null
  name. It does not erase a known SDK success/failure or trigger fallback.
- A failed MAC completion remains a normal result, without automatic retry or
  recovery. This change does not alter transmission, address or ACK policy.

Descriptor code 1 means success; SDK enum code 1 means failure. Consumers must
interpret `code` together with `domain`, or use normalized `completion.status`.
The public reference and TypeScript declaration document both domains.

## Evidence and corrected interpretation

The pinned `ieee80211_get_tx_info_from_eb` objects read the descriptor's TX-info
pointer at byte 44 on C3/S3 and byte 56 on C5, then byte 19 of TX-info. The getter
maps 1 to SDK success and every other descriptor value to SDK failure.

`lmacDiscardMSDU` writes its status argument into this TX-info field. The reviewed
C3/S3/C5 `lmacDiscardFrameExchangeSequence` callers supply 2/3 for the
frame-exchange discard family, or 4 for discard. `ppDiscardMPDU` also writes 4.
Known labels are therefore 1=`success`, 2/3=`frame-exchange`, 4=`discarded`.
They are framework observation labels, not official Espressif enum symbols or
proof of a particular RF/ACK cause.

The previous 7/8/9 labels confused two structures. The LMAC state-machine object
also has a byte at offset 19, but it is distinct from descriptor TX-info. The
review's execution of the actual C5 `lmacEndRetryAMPDUFail` object wrote 8 into
that context while a sentinel in TX-info remained unchanged. Those state-machine
writes do not justify naming descriptor values 7/8/9. Their names now remain null.

The build guard already pinned the metadata getter and PP recycle lifetime. It
now also verifies `lmac.o`, so the reviewed descriptor writers cannot change
silently. Native SDK tests reject a changed byte in either `pp.o` or `lmac.o` on
C3, S3 and C5. The shared SDK archives remain unmodified.

## Regression coverage

- Snapshot tests reject unsupported names over all 256 byte values.
- Broker fixtures model actual pointer slots and TX-info storage, with both
  target layouts. They exercise all byte values, snapshot retention after the
  callback buffer changes, descriptor reuse and an unavailable pointer.
- The SDK metadata/cache fixtures now execute the production observation hook,
  the actual C5 metadata getter, and the production snapshot. Each covers all
  256 values across two existing frame fixtures, two sequence modes and two
  prefix layouts: 2,048 modeled completions per fixture, without RF transmission.
- Real MQuickJS tests link the production snapshot/name implementation and verify
  both native domains, known/unknown names, absence of parallel fields, fallback,
  allocation failure and moving GC in ordinary and extended management builds.

The initial new expectations failed against the old implementation. After the
repair, all 16 selected native tests passed with no skips, and all 87 wireless
contract tests passed. API manifest and feature documentation checks passed;
the TypeScript consumer check passed; all 76 API JavaScript examples parsed with
the vendored MQuickJS engine. API documentation remains English.

Artifacts and exact target build commands are under
`build/raw-status-fix-20260915/`. The original review and C5 context-field probe
are under `build/raw-status-review-20260915/`. Complete C5, S3 and C3 builds passed without compiler warnings; exact commands
and results are recorded in `build-results.json`. Those implementation checks
did not include hardware acceptance; the authorized flash follow-up is below.


## Hardware follow-up: 2026-09-15

The user authorized flashing and testing. C5 remained the verified XIAO target
`hw-10bda3c854e8` (8 MiB flash, 8 MiB Quad PSRAM). USB, runtime identity, embedded
Board constant and the flasher's ROM MCU/base-MAC check all matched.

The S3 on USB had changed from `hw-e8f60a8bbdc0` to `hw-14c19fd5d648`, while the
backend still showed the previous S3's transport mapping as unreachable. That
initial follow-up did not reset, rebind, flash or test the replacement S3. The
user subsequently authorized installing the S3 firmware; its separate evidence
is recorded below. The C5-only results do not establish peer receipt or ACK.

### C5 build and preserve flash

- Board: `seeed-xiao-esp32c5`.
- Backend build job: `f61d43618cd219c71aeca86e`.
- Artifact: `45d9e6c582ab8f0534bab25dd1e5096ee812d294df13771aa516449c4ef6ac5c`.
- Application SHA-256: `71bbc5e8454d9b18271de533cab78bdd9b6d8786a6ff40165d1be2ad4cb14f56`.
- Backend flash job: `975b9bc16a38f838932085e1`.
- Flash mode: full, workspace action: preserve. All four image hashes verified;
  the partition-table image hash and workspace offset/size matched the old layout.
  The existing workspace at `0x390000`, size 4,653,056 bytes, was excluded from
  the write plan. Its empty directory listing remained unchanged.
- Agent reconnected. Boot ID changed from `c900f69d5ac8625d` to
  `f3f2b8a29b7f685d`; startup became healthy, with safe mode false and zero failures.
- The current Artifact's Raw TX documentation includes the new native domain.
- The full Board build fits its 3 MiB application partition with 1,920 bytes free;
  this small remaining build margin is distinct from runtime heap availability.

The first build request used the backend's named `compatible` strategy, which
means the historical fixed 2 MiB application layout, not the current device's
layout. It was rejected before building. The subsequent `auto` request resolved
to the existing 3 MiB application / 512 KiB storage layout; preview and flash
checks confirmed workspace preservation. No partition policy was changed.

### C5 result and cleanup checks

A pre-flash broadcast Action frame completed with the old `rawStatus: 0` shape.
After flashing, 24 bounded Action-frame cases covered one-shot and Session sends,
driver/application sequence control, and 33/300/1000-byte frames. All frames used
the actual station source MAC. Twelve were broadcast and twelve addressed the
previous, currently unconnected test board's MAC.

| Cases | SDK-normalized status | Native domain | Code | Name |
| --- | --- | --- | --- | --- |
| 12 broadcasts | success | `esp_wifi_tx_descriptor_status` | 1 | `success` |
| 12 unicasts to the previous test board | failed | `esp_wifi_tx_descriptor_status` | 2 | `frame-exchange` |

All 24 returned the new shape without legacy or parallel status fields. Failed
completions returned normally. Session flush counters matched the results with
pending zero. After each group, Raw TX had no operation, lane, quarantine,
correlation fault or pending cleanup, and no live Session or Raw TX radio client.
Boot ID stayed stable throughout the tests; all radio clients were zero at the
end and the driver was stopped. The post-flash log snapshot contained no
exception/panic chunks.

A first read-only status request called `wifi.getMac()` before the newly flashed
Wi-Fi driver was initialized and received `WIFI_MAC_FAILED`; the inspection was
corrected to respect initialization. This was not a send or startup failure.

This confirms success/failure mapping and cleanup on C5. It does not establish
peer receipt or the precise ACK cause behind code 2. Other named/unknown values
and the descriptor-unavailable fallback retain the host/SDK fixture evidence;
they were not artificially injected into the live device.

Exact backend requests, build/flash logs, before/after snapshots and executable
host regression scripts are saved under `build/raw-status-hardware-20260915/`.
The final summary is `verification.json`.

### S3 identity, existing firmware and backup

The subsequent S3 authorization applies to `hw-14c19fd5d648`, the previously
registered M5Stack StickS3. Its stable USB serial, fresh ROM base MAC
`14:c1:9f:d5:d6:48`, ESP32-S3-PICO-1 revision 0.2, 8 MiB flash and 8 MiB Octal
PSRAM matched. The ROM enrollment probe rebound `/dev/ttyACM1` from the previous
S3 identity to this device. C5 was excluded from the flash operation.

The stored Board documentation identified StickS3, but its historical Artifact
binding and old cached ESP-NOW logs did not describe the installed firmware.
A read of the actual partition table found `factory` at `0x10000` (4,992 KiB),
`spiffs` at `0x4f0000` (3 MiB), and `coredump` at `0x7f0000` (64 KiB), with no
ESP32QJS workspace partition. The app descriptor identified
`arduino-lib-builder`, IDF `v5.5.4-dirty`, built July 15, 2026. This was not an
ESP32QJS Agent reconnect failure after an ESP32QJS boot.

Before any flash writes, an exclusive serial debug lease protected a complete
8,388,608-byte Flash backup. Esptool checked the read's MD5 digest, and the
partition-table bytes matched the independent first read. The backup SHA-256 is
`61f00ebb8e02cd8c1440847adab9586aac9cee705958196a7663ee6520d9fc8c`.
The lease was explicitly released before the backend flash operation.

The full image is `build/raw-status-hardware-20260915/s3-original-flash-8mb.bin`;
`s3-backup-verification.json` records identity, hashes and the original app
descriptor. The old SPIFFS image is also saved separately. Installing ESP32QJS
requires the new LittleFS workspace initializer; this is a new filesystem,
not preservation or migration of the Arduino SPIFFS files on the device.

The StickS3 backend build job `f9cae8a80824d5b857d4e004` succeeded and produced
Artifact `d516b97e1298b6f8a970f69d9a474fe307881891f3b7c203b6a2ec6473712c9b`.
The application SHA-256 is
`2222cd25b657dd84134940511b58378283d1d2f5fd7d1efd7bfabebc9843eb1f`.
It is 2,917,360 bytes, leaving 162,832 bytes in the 3,080,192-byte app partition.
Storage begins at `0x300000` (512 KiB); the new workspace begins at `0x380000`
and occupies 4,718,592 bytes.

### S3 flash, startup and completion checks

Backend flash job `497ebcea4a2628ea930251d2` succeeded with `mode: "full"` and
`workspaceAction: "erase"`, after the original Flash backup was verified.
The flasher rechecked the ROM MCU/base MAC and verified all five images,
including the new workspace initializer. `flashVerified` and `agentConfirmed`
were true. `artifactIdentityConfirmed` was false; this acceptance relies on
the image checks and runtime observations, not that separate identity flag.

The new Agent connected with boot ID `e0eb7897ac8bfec7`. Read-only exec confirmed
the StickS3 Board constant, target, 8 MiB Flash, enabled 8 MiB Octal PSRAM and an
empty mounted workspace. Startup progressed from `stabilizing` to `healthy`,
with safe mode false and failure count zero. The device documentation binding
now matches the new Artifact and includes the descriptor status domain.

S3 passed the same 24-case completion matrix as C5: one-shot/Session,
driver/application sequence control, 33/300/1000-byte Action frames, and
broadcast/previous-offline-test-board destinations. All frames used the actual
interface source MAC. Twelve broadcasts returned normalized success with native
code 1, name `success`; twelve offline-peer unicasts returned normalized failure
with code 2, name `frame-exchange`. Failures completed normally. All results used
the sole `completion` shape, with no legacy or parallel status fields. Session
flush counts and resource cleanup matched the results.

### C5 and S3 peer reception

The two connected boards then exchanged bounded unicast Action frames on channel
6. Each direction sent twelve frames, covering both sequence policies and all
three sizes. Both boards used their own interface MAC as source. Monitor filters
selected the test source/destination and Action subtype; captured payload markers
matched each transmitted frame exactly once.

| Direction | Matched peer frames | Completion | Observed latency |
| --- | --- | --- | --- |
| C5 to StickS3 | 12 / 12 | success, native 1 / `success` | 1.379–21.253 ms |
| StickS3 to C5 | 12 / 12 | success, native 1 / `success` | 3.537–27.661 ms |

Boot IDs remained stable; the empty workspaces were unchanged by the tests.
Both boards had zero radio clients and active operations, no occupied Raw TX
lane, quarantine, correlation fault, pending cleanup or live Session, and zero
invalid/mismatched/orphan/duplicate callbacks. Monitor close left the receiving
driver started despite releasing all clients. The final C5 idle-state check
therefore required a bounded `wifi.stop()` after confirming zero owners; both
drivers were stopped at handoff. No monitor lifecycle source change was made.

The final available log snapshots had no exception/panic matches in 230 current-
boot S3 chunks and 370 current-boot C5 chunks. The snapshots reported dropped log
records (474 and 742 respectively), so this is not complete log coverage.
The tests establish peer reception and the observed completion mapping, not a
separate over-the-air ACK capture or proof of a specific cause for code 2.
Descriptor values 3/4, unknown values and unavailable-descriptor fallback retain
the earlier host/SDK fixture evidence only.

`s3_test.py`, `s3-verification.json`, `paired_test.py`,
`paired-verification.json`, final device/log snapshots and the flash job log are
under `build/raw-status-hardware-20260915/`. The combined `verification.json`
supersedes the initial C5-only summary's pending-S3 status.
