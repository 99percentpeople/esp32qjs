# API Stability Plan

Status: active review for the unreleased Host API v1. No ESP32QJS Host API has
been declared frozen yet.

This document records stability policy and the remaining evidence needed before
the current v1 surface can be frozen. It does not duplicate every method or
option. The normative current surface is maintained by:

- [C API Reference](c-api.md) for built-in JavaScript APIs;
- [JavaScript API Reference](js-api.md) for LittleFS libraries;
- [`types/esp32qjs-c-api.d.ts`](../types/esp32qjs-c-api.d.ts) and
  [`types/esp32qjs-js-api.d.ts`](../types/esp32qjs-js-api.d.ts) for declarations;
- [`api-manifest.json`](../api-manifest.json) for the generated native callable
  inventory;
- [System Management API v1](sys-management-api.md) for `sys` lifecycle and
  diagnostic semantics.

`scripts/generate_api_manifest.py --check` verifies native registration,
feature ownership, Future-driver registration, declarations, and C API
documentation together. A design note or an older example never overrides
those sources.

## Versioning policy

- The framework is still on the sole development Host API `v1` contract.
- Breaking changes replace v1 directly; do not add legacy aliases, compatibility
  readers, or v2 namespaces during development.
- A public surface becomes frozen only after its declaration, implementation,
  lifecycle tests, disabled-feature tests, and relevant hardware evidence all
  agree.
- Additive changes are preferred after freeze, but no current module should be
  described as stable merely because its method names look complete.

## Stable design rules

- Native modules stay low-level and board-independent. Pins, product protocols,
  reconnect policy, drivers, widgets, and application state machines belong to
  applications or host-supplied JavaScript.
- Optional native capabilities are selected with
  `CONFIG_ESP32_MQUICKJS_FEATURE_*`; unsupported MCU capabilities are forced
  off by the MCU profile.
- `sys.info.features` is the authoritative runtime view of the compiled feature
  set. Applications should not infer capabilities from board names.
- JavaScript remains single-threaded. Direct Future-backed calls wait
  cooperatively; explicit concurrency uses `Future.call()` and native Future
  drivers.
- Repeated external input uses bounded `EventQueue` handles. Event delivery,
  cancellation, close, queue overflow, and dropped-payload cleanup must have
  explicit semantics.
- Binary payloads use `ByteView`, `ByteSpanSource`, or `Stream`; large buffers
  must not be converted into JavaScript number arrays.
- Native handles have explicit, idempotent `close()` operations. Finalizers are
  fallback cleanup, not the primary lifecycle API.
- Cross-task state publication uses C11 atomics, a FreeRTOS synchronization
  primitive, or one lock. `volatile` is not a synchronization contract.
- Recoverable native operational errors use the sole v1
  `{ code, operation, details }` shape. Module metadata stays under `details`;
  validation errors remain ordinary `TypeError` or `RangeError` failures.
- BLE and ESP-NOW callback ownership and teardown invariants are recorded in
  [Wireless Concurrency and Teardown](wireless-concurrency.md).
- Public TLS always verifies the public CA chain, hostname, and certificate
  dates. TLS remains an independently selectable build capability and never
  exposes an insecure bypass.

## Current implementation inventory

The following are implemented, feature-gated where applicable, and documented
in the current C API reference:

- Core runtime: `Future`, `EventQueue`, timers, `ByteView`,
  `ByteSpanSource`, `Stream`, `load()`, `framework.load()`, and `gc()`.
- Cross-module contracts: strict native plain-options parsing, bounded integer
  parsing, and the shared `NativeError` operational-failure shape.
- Storage and lifecycle: immutable `FsVolume`, filesystem watch queues, NVS,
  lazy `sys.info`/`sys.status`, wall-clock synchronization, runtime restart,
  reboot, startup guarding, and application-owned safe mode.
- Peripherals: GPIO, LEDC, ADC, DAC, I2C bus/device handles, SPI bus/device
  handles, UART ports, and RMT channels.
- Media and graphics: I2S channels, camera frames, Bitmap operations, display
  command buffers, and generic byte-span output.
- Networking and transport: transport-neutral `net`, Wi-Fi station control,
  ESP-NOW, BLE central/peripheral roles, asynchronous DNS, TCP/UDP handles,
  optional TLS, HTTP client/server, WebSocket client, and USB Serial/JTAG.
- Integration: application-configured `esp32qjs.rpc/1` codec/decoder handles
  with deterministic CBOR and streamed byte sources.

`Future` and `EventQueue` are current runtime primitives, not planned APIs.
Removed `defer()`, `waitFor()`, callback overloads, `*.async` namespaces,
transport `recv()` aliases, mutable filesystem roots, and the former flat
`sys.info()` function are not part of v1.

## Feature model

Applications and managed builds select exact build features. The generated
catalog in README is sourced from `runtime-features.json`; this document adds
stability evidence without maintaining a second feature inventory.

Important dependency rules include:

- Wi-Fi, sockets, TLS, HTTP client/server, and WebSocket require `net`.
- WebSocket currently requires TLS because the ESP-IDF component packages WS
  and WSS in one transport.
- Wi-Fi requires a target with Wi-Fi hardware; DAC and camera remain subject to
  their SoC capability checks.
- ESP-NOW shares the station-interface radio runtime with Wi-Fi; BLE uses the
  independent NimBLE host while coexistence is enabled when both radios exist.
- Static file handling requires both the HTTP server and filesystem features.
- USB Serial/JTAG and the interactive REPL are mutually exclusive.

The build catalog may call these entries modules for product selection, but
entries such as `net` and `tls` are compile-time capabilities and do not imply a
same-named importable JavaScript object.

## Stability matrix

| Area | Shape status | Evidence still required before freeze |
| --- | --- | --- |
| Core helpers, `Future`, timers | Candidate | Device-backed repeated cancellation, timeout, GC, pending-driver stop, and runtime-restart soak beyond host capacity/lane and four-stage runtime-allocation fault injection plus USB Serial concrete lifecycle coverage |
| `EventQueue` | Candidate | Sustained ISR/task producer-overflow qualification across the remaining concrete event sources beyond completed generic host saturation, BLE scan/notification pooled-slot recovery, and creation-time allocation rollback |
| `FsVolume`, file `Stream`, NVS | Candidate | ESP flash/VFS power-cut and required-workspace recovery beyond host atomic-replacement and vendored LittleFS image fault injection; encrypted-NVS hardware coverage |
| `sys` and runtime lifecycle | Candidate | Repeated ESP32-C3/ESP32-C5/ESP32-S3 restart, reboot, startup-failure, and required-workspace recovery soak |
<!-- BEGIN GENERATED FEATURE STABILITY -->
| `fs`, `nvs` | Candidate | ESP flash/VFS power-cut and required-workspace recovery beyond host atomic-replacement and vendored LittleFS image fault injection; encrypted-NVS hardware coverage |
| `gpio`, `ledc` | Candidate | Board-backed lifecycle checks across active-high/active-low wiring and LEDC status transitions beyond completed GPIO ISR-removal retry retention and LEDC channel/timer stop, pause, and deconfigure retry retention |
| `adc` | Candidate | Stable hardware lifecycle plus cross-target calibration and attenuation validation beyond completed host unit/calibration construction rollback and deletion-retry retention |
| `dac` | Hardware pending | A supported ESP32/ESP32-S2 target is required; current C3/C5/S3 profiles disable it |
| `i2c`, `spi`, `uart`, `rmt` | Candidate | External loopback/peripheral fixtures and cancellation/close stress beyond completed host I2C lease/native-bus construction rollback and delete-retry retention, SPI queue, ownership, bounds, timeout, allocation fault injection, rearmable progress-timer ownership, and device/bus teardown-retry retention, RMT symbol-buffer allocation plus channel/encoder/callback rollback and teardown-retry retention, and UART watcher-lock/driver construction plus driver-delete retry retention |
| `i2s`, `camera` | Candidate | Long-duration DMA, frame lease, close/reopen, and mixed media/TLS pressure beyond completed I2S TX/RX channel construction, directional start/stop/delete retry retention, timeout-timer allocation rollback, and Camera driver-deinit failure retention with explicit close retry |
| `bitmap` and JS display layer | Candidate | More board/display combinations and repeated source/presentation lifecycle testing |
| `net`, `wifi`, DNS | Candidate | Interface replacement, reconnect, and non-Wi-Fi ESP-NETIF coverage beyond completed Wi-Fi lock/event-group/queue allocation rollback and later init-stage cleanup |
| `wifiCsi` | Hardware pending | C3/S3 legacy and C5 HE RF capture, PHY metadata, 2.4/5 GHz, coexistence, transport throughput, 500-cycle close/reopen, and long-duration memory qualification are required beyond host ownership tests and target build coverage |
| `espNow` | Candidate | ESP32-C3 and broader peer-matrix qualification beyond completed encrypted S3/C5 E2E |
| `ble` | Candidate | Multiple-connection, ESP32-C3, peer-loss, and extended coexistence qualification beyond completed S3/Pi and S3/C5 E2E, host scan/notification queue-saturation ownership coverage, and NimBLE host stop/deinit exact-suffix retry retention |
| `tls`, `socket`, `http`, `httpServer`, `websocketClient` | Candidate | Long-duration mixed connection/failure pressure on PSRAM and no-PSRAM profiles beyond completed HTTP operation storage/mutex and client-cleanup retry retention, HTTP Server native stop/close retry retention, shared socket/UART Future-timer create/start rollback, and WebSocket callback-queue ownership plus stop/unregister/destroy exact-suffix retry retention |
| `usbSerial`, `rpc` | Candidate | Physical re-enumeration, streamed-transfer, and runtime-restart soak beyond completed Agent/Host disconnect ownership, USB Serial pending-send stop/destroy/new-generation host tests, and rearmable TX-stall timer ownership |
| `runtimeLogs` | Candidate | Overflow, restart, and sustained producer qualification |
<!-- END GENERATED FEATURE STABILITY -->

The completed StickS3 network, TLS, HTTP, WebSocket, filesystem, GPIO, Future,
RPC, and resource-baseline runs are useful evidence, but one board and one
memory profile are not enough to freeze a reusable framework API.

## Freeze review checklist

Before marking one area frozen:

1. Confirm the C registration, TypeScript declarations, generated API manifest,
   C API reference, and server Skills use the same names and semantics.
2. Build with the feature enabled and disabled on every supported MCU where the
   capability can exist.
3. Exercise success, timeout, cancellation, explicit close, finalizer fallback,
   runtime restart, and allocation-failure paths relevant to the resource.
4. Verify internal, DMA, and PSRAM free bytes, minimum free bytes, and largest
   free blocks do not decline monotonically during repeated lifecycle tests.
5. Record the exact firmware commit, hardware profile, test command, and any
   physical fixture used.
6. Remove superseded examples and migration-only wording instead of preserving
   compatibility documentation for development-only APIs.

## Remaining API decisions

- Decide whether LEDC low-speed mode is intentionally the only v1 speed mode
  after cross-target hardware validation.
- Decide whether any additional ADC/DAC modes are justified by measured use
  cases; continuous sampling and waveform generation should be separate,
  explicit surfaces rather than overloads of the one-shot APIs.
- Add new network-interface drivers without moving link policy into `net`.
- Keep compressed-image decoders, richer display policy, sensor drivers, and
  board helpers in JavaScript unless profiling proves a native primitive is
  required.
- Define reviewed extension rules and stable native class IDs before allowing
  independently developed C modules to participate in the public ABI.

## Validation commands

Run from the standalone firmware repository:

```bash
uv run python scripts/generate_api_manifest.py --check
uv run python scripts/generate_feature_docs.py --check
uv run python scripts/remote.py check-js
uv run python -m unittest discover -s tests/python
uv run python scripts/remote.py test --scope c
uv run python scripts/remote.py --assume y build
```

Device-backed validation must name its selected modules and physical fixture;
do not report a host check or a successful build as hardware proof.
