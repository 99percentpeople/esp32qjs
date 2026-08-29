# Framework Backlog

Status: open work only. Implemented runtime, display-driver, async-I/O, startup
guard, and safe-mode behavior is documented in the API references and is not
repeated here.

API-shape and freeze work is tracked separately in
[API Stability Plan](api-stability-plan.md).

## Firmware Delivery and Recovery

- Add signed firmware and resource artifacts.
- Add A/B OTA updates with rollback.
- Qualify the existing startup guard and application-owned safe mode across
  interrupted startup, corrupt required workspace filesystems, and repeated
  watchdog or panic resets.
- Test recovery across power loss during flash, workspace, NVS, and future OTA
  writes.

## Production Security

- Implement and qualify the target-specific profiles, provisioning flow, and
  update pipeline defined in
  [Production Security and Update Plan](production-security-plan.md).
- Add device-secret provisioning and rotation.
- Keep model/provider credentials on the host and document the production trust
  boundary for device pairing and artifact delivery.

## Extensibility

- Allocate stable public native class IDs for third-party C modules.
- Define compatibility rules for externally developed native modules before the
  Host API is frozen.

## DMA and SPI Qualification

The reusable DMA staging core, memory accounting, SPI TX/RX/full-duplex paths,
timeouts, fault state, public types, and API documentation are implemented.
Remaining work is verification rather than another SPI API redesign:

- Host fault injection now drives the production completion queue, cancellation
  and ownership gates, TX/RX length checks, and no-progress rule. It covers
  ordered and out-of-order completion, cancel-before-start and cancel-in-flight,
  close-pending retention, RX overflow, TX underflow, and staging allocation
  failure at each of the four buffer allocations.
- Repeat those cases on a stable SPI fixture so ESP-IDF callback/queue behavior,
  physical CS continuity, faulted-device rejection, and close/reopen are backed
  by hardware evidence. The current no-PSRAM and SPI-disabled build profiles
  have been re-run after the DMA-core changes; build checks remain separate
  from device E2E tests.
- Keep the M5Stack StickS3 display default at 40 MHz. Before any future default
  change, compare explicit 2/4/8/16 KiB staging configurations and exercise
  display, Wi-Fi, Agent, and I2S concurrency. No long-duration run is required
  for the current 40 MHz default.
- Reuse the shared primitives from I2S, RMT, or Camera only when a concrete
  adapter needs them; do not migrate those drivers merely for API symmetry.
- Add a dedicated peripheral worker only if instrumentation demonstrates that
  an ESP-IDF SPI call blocks the runtime task.

## Local Wireless Qualification

The BLE and ESP-NOW v1 native APIs, lifecycle handling, declarations, and
documentation are implemented. XIAO ESP32-S3 to Raspberry Pi 5 BLE central and
peripheral E2E is complete. The antenna-corrected C5 run also covers BLE-only
discovery from the S3 and Raspberry Pi 5, encrypted bidirectional S3/C5
ESP-NOW, RX-pool sequence-gap accounting, BLE/ESP-NOW coexistence, and both
initialization orders. Remaining work is:

- Add public per-peer rate configuration only with automatic-rate restoration
  across peer update, removal, close, and timeout-driven native rebuild.
- Treat ESP32-C3 hardware coverage, multiple BLE connections, and broader peer
  matrices as later qualification. Do not require a 24-hour soak for the
  current development milestone.

## Verification Infrastructure

- Commit and publish the checked-in host and C3/C5/S3 workflows, then record the
  first green GitHub Actions run. The remote currently has no workflow or run
  because these files exist only in the local worktree.
- Enable required checks on `main` after branch protection is available. The
  current private repository plan rejects the branch-protection API with HTTP
  403; making the repository public or upgrading the GitHub plan is an external
  prerequisite.
- Provision and maintain the self-hosted runner, fixtures, and secrets required
  by the scheduled hardware-lab workflow.
- Add sanitizers and host-side fuzzing where the native adapters can run without
  hardware.
- Host behavior tests now cover SPI staging allocation rollback, public Future
  capacity versus the internal reserve, resource-lane saturation/FIFO,
  runtime-stop retention of pending drivers, ISR EventQueue saturation, and
  interrupted atomic file replacement. EventQueue creation and Future runtime
  creation inject every scratch/mutex/queue or state/slot/queue allocation
  failure with reverse rollback. HTTP operation creation likewise injects its
  storage and cancellation-mutex failures through the production owner. RMT
  symbol-buffer creation injects owner and internal/DMA storage failures. RMT
  channel opening likewise injects channel, TX encoder, and callback-registration
  failures; cleanup retains the exact enabled/encoder/channel suffix when
  disable or deletion fails so close/open/runtime-init can retry. Wi-Fi runtime
  creation injects lock, event-group, and both queue failures;
  later netif, radio, handler, or timer failures share one reverse cleanup.
  I2S channel creation injects partial TX/RX handles; the production owner
  tracks each enabled direction, rolls a new TX enable back when RX start
  fails, and retains the exact enabled/handle suffix when stop or deletion
  fails so close/open/runtime-init can retry. Duplex timeout-timer creation also
  injects RX and TX failures and deletes a partial pair in reverse order.
  Camera driver teardown now retains the initialized driver and all camera,
  SCCB, and LEDC leases when `esp_camera_deinit()` fails; explicit close reports
  the failure without detaching its JS handle so close/open/runtime-init can
  retry. ADC
  unit opening now gives the unit handle and every per-channel calibration
  handle to one owner; partial unit creation is rolled back, while calibration
  or unit deletion failures retain the exact resource suffix so
  configure/close/open/runtime-init can retry. I2C bus opening
  now gives the peripheral lease and native bus handle to one owner; bus-create
  failures release the lease, while bus-delete failures retain both resources
  for a later cleanup retry. UART opening injects watcher-lock and
  driver/event-queue creation failures through one owner; UART and socket Future
  pollers share a timer owner that deletes a created timer when start fails and
  stops/deletes successful timers exactly once. The same owner now supports
  create-without-start plus explicit start/stop for rearmable one-shot timers;
  SPI DMA progress and USB Serial TX stall paths use it for create/start failure
  handling and exactly-once teardown. SPI native teardown now removes every
  child device before freeing the bus; a device-remove or bus-free failure
  retains the exact remaining device/bus/staging suffix so explicit close,
  open, finalizer cleanup, and runtime init can retry. GPIO runtime teardown no
  longer erases a failed ISR-handler removal, so later init can retry it. LEDC
  channel/timer teardown records successful stop/pause stages and retains the
  configured resource plus peripheral lease when a later deconfigure fails;
  explicit deconfigure and runtime init share the retry path. HTTP Server stop
  now retains its native handle, listening state, and route-registration state
  when `httpd_stop()` fails; explicit stop/close, finalizer cleanup, orphaned
  open, and runtime init only clear dependent state after a successful retry.
  BLE scan/notification callbacks use a shared
  production pooled-event publisher whose saturation test proves rejected pool
  slots return exactly once. WebSocket callback messages use the tested
  task-context DROP_NEW primitive, with rejected heap payloads returned to the
  producer for exactly-once release. USB Serial's production lifecycle core covers
  pending-send stop/destroy ownership and acquisition by a new generation. The
  device Agent and Bun request channel cover pending-operation ownership across
  disconnect/reconnect. UART driver delete, NimBLE host stop/deinit, HTTP client
  cleanup, and WebSocket stop/unregister/destroy now each retain the exact
  native-resource suffix on failure and retry before clearing dependent handles,
  queues, pools, or JS generations. Runtime destroy also propagates retained
  I2C bus-delete and SPI device/bus cleanup failures instead of freeing the
  JavaScript context underneath them. Vendored LittleFS host tests cover partial
  metadata programming, corrupt-media mount rejection, and explicit reformat.
  Continue sustained overflow through the remaining concrete ISR/task producers, and repeat
  filesystem, driver-stop, and transport recovery against ESP flash/VFS and
  physical hardware.
- Extend the completed HTTP/HTTPS/WebSocket lifecycle checks into long-duration,
  mixed TLS/media pressure on PSRAM and no-PSRAM profiles, including reliable
  USB Serial/JTAG re-enumeration after host reset operations.
- Complete repeated hardware lifecycle and memory-regression qualification for
  SSD1306, ST7789, shared buses, and supported ESP32-S3/ESP32-C3 boards.
- Re-run the S3 device JavaScript suite on a stable USB path. The current XIAO
  ESP32-S3 fixture boots the PSRAM test image and reaches the test-ready marker,
  but its native USB Serial/JTAG device repeatedly disconnects before the host
  can drive a case. Dual-device wireless qualification also requires a second
  responsive fixture.
