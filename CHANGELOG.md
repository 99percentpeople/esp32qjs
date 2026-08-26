# Changelog

All notable framework changes are documented here. ESP32QJS follows Semantic
Versioning; the native Host API uses a separate integer compatibility version.

## 0.1.0 - Unreleased

This release remains under development. Public framework APIs, manifests, and
persisted formats use the sole v1 contract and may still change directly before
the first release.

### Runtime and lifecycle

- Integrate the vendored MQuickJS engine into a single trusted JavaScript task
  with bounded evaluation, callback deadlines, cooperative native waits, and
  optional task-watchdog supervision.
- Add reusable runtime create/start/stop/destroy APIs, JavaScript-only runtime
  restart, full-device reboot receipts, retained runtime logs, startup failure
  tracking, and an application-owned persistent safe-mode latch.
- Replace the former aggregate `sys.info()` function with lazy `sys.info` and
  `sys.status` trees; add immutable profile constants, bounded FreeRTOS task
  snapshots, detailed internal/DMA/PSRAM memory views, and transport-neutral
  `sys.time` synchronization.
- Seed `sys.randomHex()` from true hardware entropy and expose the stable eFuse
  Base MAC as `sys.info.hardware.hardwareId`.

### Concurrency and memory ownership

- Add scheduler-driven `Future` composition, native Future drivers, timeout and
  cancellation semantics, resource-keyed FIFO lanes, and automatic collection
  of terminal Futures without capacity batching.
- Add bounded `EventQueue` delivery with queue statistics, sequence/timestamp
  metadata, overflow policy, and payload draining that does not allocate in a
  finalizer.
- Use C11 release/acquire publication or FreeRTOS synchronization for worker,
  callback, and runtime-task ownership instead of `volatile` completion flags.
- Add owned `ByteView`, retained `ByteSpanSource`, common `Stream`, and framework
  memory classes for bounded binary I/O without large JavaScript number arrays.
- Prefer PSRAM for movable large allocations, TLS, media payloads, and worker
  stacks when the measured hardware profile supports it while preserving
  internal/DMA memory for drivers.

### Storage and loading

- Add immutable `FsVolume` receivers, exact mounted-root validation, secondary
  LittleFS support, bounded whole-file helpers, atomic text replacement,
  Future-backed VFS/Stream operations, and ordered filesystem watch events.
- Keep `load()` relative to the active application volume and add
  `framework.load()` for read-only system libraries below `/_sys`.
- Add a bounded NVS string module with explicit encryption status and purge
  behavior; applications retain ownership of namespace and secret policy.
- Add allowlisted startup-script bundling and exact-engine 32-bit MQuickJS
  precompilation.

### Peripherals, media, and graphics

- Add feature-gated GPIO, LEDC, ADC, DAC, I2C, SPI, UART, RMT, I2S, camera, and
  Bitmap APIs with explicit handle lifecycles and target capability checks.
- Replace singleton I2C/SPI operations with bus/device handles; serialize native
  operations per physical resource while allowing independent controllers or
  I2S directions to progress independently.
- Add UART readiness/error EventQueues, RMT symbol buffers, standard/PDM I2S,
  leased camera frames, Bitmap conversion/blit operations, display command
  buffers, and generic span-based output for DMA-friendly transfers.
- Add layered JavaScript display libraries with explicit transport, panel,
  surface, font, batching, presentation metrics, and close ownership.
- Replace development-board presets with intrinsic ESP32-C3/ESP32-S3 MCU
  profiles and generated Flash, PSRAM, partition, and immutable wiring inputs.

### Networking and security

- Add transport-neutral ESP-NETIF status/watch APIs, Wi-Fi station control, and
  asynchronous DNS that does not block the JavaScript runtime task.
- Add object-based TCP, UDP, and listener handles with bounded send/receive,
  native byte-source support, TLS handshake state, cancellation, and stale
  generation checks.
- Add bounded HTTP client Futures, EventQueue-based HTTP servers, static-file
  handling, and a text/binary WebSocket client whose blocking ESP-IDF cleanup
  runs outside the JavaScript task.
- Make TLS an independently selectable capability. Public TLS always validates
  the full CA chain, hostname, and certificate dates; public HTTPS, raw TLS, and
  WSS share the same certificate-bundle wrapper and structured error classes.
- Keep standard TLS record sizes, prefer external mbedTLS allocation on measured
  PSRAM profiles, and remove the CA bundle and secure paths when TLS is disabled.

### Device integration

- Add mutually exclusive headless USB Serial/JTAG and interactive REPL
  frontends with bounded transport lifecycles.
- Add the generic, application-configured `esp32qjs.rpc/1` codec and decoder:
  deterministic CBOR, COBS-delimited records, CRC-32, bounded segmentation,
  streamed byte sources, and atomic file adoption. Firmware owns no Agent
  opcode, authentication, workspace, or product policy.
- Add bounded native runtime-log capture for headless products and preserve log
  ordering across JavaScript runtime generations.

### Build, documentation, and verification

- Separate selectable application profiles from intrinsic MCU profiles and
  shared JavaScript resources; support external application directories without
  copying them into the framework repository.
- Generate Flash-sized partition layouts and hardware overlays from measured
  capacity rather than guessing development-board properties.
- Add exact-engine JavaScript syntax validation, startup precompile checks,
  generated native API manifests, TypeScript declarations, Python architecture
  tests, host C tests, and device-backed JS module tests.
- Add a version-checked ESP32 camera 2.1.7 workaround for the upstream OV3660
  JPEG DMA-tail issue used by the pinned dependency.
- Establish Apache-2.0 licensing and the initial framework, API, lifecycle,
  stability, and production backlog documentation.
