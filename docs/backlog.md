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

- Define production profiles for secure boot, flash encryption, and encrypted
  NVS.
- Add device-secret provisioning and rotation.
- Keep model/provider credentials on the host and document the production trust
  boundary for device pairing and artifact delivery.

## Extensibility

- Allocate stable public native class IDs for third-party C modules.
- Define compatibility rules for externally developed native modules before the
  Host API is frozen.

## Verification Infrastructure

- Add CI build matrices for supported ESP-IDF targets and representative Build Contexts.
- Add sanitizers and host-side fuzzing where the native adapters can run without
  hardware.
- Add fault-injection coverage for allocation failure, queue saturation,
  transport loss, storage corruption, and interrupted writes.
- Extend the completed HTTP/HTTPS/WebSocket lifecycle checks into long-duration,
  mixed TLS/media pressure on PSRAM and no-PSRAM profiles, including reliable
  USB Serial/JTAG re-enumeration after host reset operations.
- Complete repeated hardware lifecycle and memory-regression qualification for
  SSD1306, ST7789, shared buses, and supported ESP32-S3/ESP32-C3 boards.
