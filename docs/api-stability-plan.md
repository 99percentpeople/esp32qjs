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
- Public TLS always verifies the public CA chain, hostname, and certificate
  dates. TLS remains an independently selectable build capability and never
  exposes an insecure bypass.

## Current implementation inventory

The following are implemented, feature-gated where applicable, and documented
in the current C API reference:

- Core runtime: `Future`, `EventQueue`, timers, `ByteView`,
  `ByteSpanSource`, `Stream`, `load()`, `framework.load()`, and `gc()`.
- Storage and lifecycle: immutable `FsVolume`, filesystem watch queues, NVS,
  lazy `sys.info`/`sys.status`, wall-clock synchronization, runtime restart,
  reboot, startup guarding, and application-owned safe mode.
- Peripherals: GPIO, LEDC, ADC, DAC, I2C bus/device handles, SPI bus/device
  handles, UART ports, and RMT channels.
- Media and graphics: I2S channels, camera frames, Bitmap operations, display
  command buffers, and generic byte-span output.
- Networking and transport: transport-neutral `net`, Wi-Fi station control,
  asynchronous DNS, TCP/UDP handles, optional TLS, HTTP client/server,
  WebSocket client, and USB Serial/JTAG.
- Integration: application-configured `esp32qjs.rpc/1` codec/decoder handles
  with deterministic CBOR and streamed byte sources.

`Future` and `EventQueue` are current runtime primitives, not planned APIs.
Removed `defer()`, `waitFor()`, callback overloads, `*.async` namespaces,
transport `recv()` aliases, mutable filesystem roots, and the former flat
`sys.info()` function are not part of v1.

## Feature model

Applications and managed builds select exact build features. The current
feature families are:

- storage: `fs`, `nvs`;
- peripheral/media: `gpio`, `ledc`, `adc`, `dac`, `i2c`, `spi`, `uart`,
  `rmt`, `i2s`, `camera`, `bitmap`;
- networking: `net`, `wifi`, `tls`, `socket`, `http`, `httpServer`,
  `websocket`;
- integration and diagnostics: `usbSerial`, `rpc`, `runtimeLogs`, task
  snapshots.

Important dependency rules include:

- Wi-Fi, sockets, TLS, HTTP client/server, and WebSocket require `net`.
- WebSocket currently requires TLS because the ESP-IDF component packages WS
  and WSS in one transport.
- Wi-Fi requires a target with Wi-Fi hardware; DAC and camera remain subject to
  their SoC capability checks.
- Static file handling requires both the HTTP server and filesystem features.
- USB Serial/JTAG and the interactive REPL are mutually exclusive.

The build catalog may call these entries modules for product selection, but
entries such as `net` and `tls` are compile-time capabilities and do not imply a
same-named importable JavaScript object.

## Stability matrix

| Area | Shape status | Evidence still required before freeze |
| --- | --- | --- |
| Core helpers, `Future`, timers | Candidate | Longer repeated cancellation, timeout, GC, and runtime-restart soak |
| `EventQueue` | Candidate | Allocation-failure and sustained producer-overflow qualification across all event sources |
| `FsVolume`, file `Stream`, NVS | Candidate | Interrupted-write and corrupted-filesystem fault injection; encrypted-NVS hardware coverage |
| `sys` and runtime lifecycle | Candidate | Repeated ESP32-C3/ESP32-S3 restart, reboot, startup-failure, and required-workspace recovery soak |
| GPIO and LEDC | Candidate | Board-backed lifecycle checks across active-high/active-low wiring and LEDC status transitions |
| ADC | Candidate | Cross-target calibration and attenuation validation |
| DAC | Hardware pending | A supported ESP32/ESP32-S2 target is required; current C3/S3 profiles disable it |
| I2C, SPI, UART, RMT | Candidate | External loopback/peripheral fixtures and cancellation/close stress |
| I2S and camera | Candidate | Long-duration DMA, frame lease, close/reopen, and mixed media/TLS pressure |
| Bitmap and JS display layer | Candidate | More board/display combinations and long repeated source/presentation lifecycle testing |
| `net`, Wi-Fi, DNS | Candidate | Interface replacement, reconnect, and non-Wi-Fi ESP-NETIF coverage |
| TLS, socket, HTTP, WebSocket | Candidate | Long-duration mixed connection/failure pressure on PSRAM and no-PSRAM profiles |
| USB Serial/JTAG and RPC | Candidate | Re-enumeration, transport-loss, streamed-transfer, and runtime-restart soak on supported targets |

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
uv run python scripts/remote.py check-js
uv run python -m unittest discover -s tests/python
uv run python scripts/remote.py test --scope c
uv run python scripts/remote.py --assume y build
```

Device-backed validation must name its selected modules and physical fixture;
do not report a host check or a successful build as hardware proof.
