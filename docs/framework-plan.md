# ESP32QJS Framework Plan

## Purpose

This document tracks the work required to turn ESP32QJS from a repository-local
firmware application into a reusable framework for multiple applications.

The target remains a trusted JavaScript application runtime for ESP-IDF. It is
not a sandbox for untrusted third-party scripts.

## Decisions

- License: Apache License 2.0.
- Framework version: `0.1.0`.
- Native Host API version: `1`, versioned independently from the framework.
- Default application: `minimal`.
- Application layout: shared system files plus application overlays.
- Board profiles and application profiles are selected independently.

## Target Layout

```text
apps/
  minimal/
    app.env
    sdkconfig.defaults
    partitions/<board>.csv
    flash_data/index.js
  demo/
    app.env
    sdkconfig.defaults
    partitions/<board>.csv
    flash_data/index.js
    flash_data/demo/
shared/
  flash_data/_sys/
configs/
  boards/<board>/
components/
  esp32_mquickjs/
  esp32qjs_interactive/
  esp32qjs_runtime/
```

The LittleFS image is assembled by copying `shared/flash_data` first and the
selected application's `flash_data` second. Application files therefore may
override shared files intentionally.

## Phase 0: Documentation and Application Separation

Status: complete for `0.1.0`.

Deliverables:

- Remove completed implementation-plan documents once their durable API
  documentation is already present.
- Replace machine-local documentation links with repository-relative links.
- Move reusable `_sys` JavaScript libraries under `shared/flash_data`.
- Move the current display UI demo under `apps/demo`.
- Add a board-neutral `apps/minimal` application and make it the default.
- Preserve complete-flash-data overrides, while allowing the board-backed JS
  test image to overlay its test assets onto shared framework resources.

Acceptance criteria:

- A default build boots without requiring display hardware.
- `--app demo` builds the existing display demo.
- `--app minimal` and `--app demo` can use the same board profile without
  copying board configuration.
- JavaScript-only flashing uses the selected application's merged image.

## Phase 1: Reusable Runtime Component

Status: complete for `0.1.0`.

Move application bootstrap policy out of `main/app_main.c` and into a public
`esp32qjs_runtime` component.

Initial public lifecycle:

```c
esp32qjs_runtime_default_config(...)
esp32qjs_runtime_create(...)
esp32qjs_runtime_start(...)
esp32qjs_runtime_request_stop(...)
esp32qjs_runtime_stop(...)
esp32qjs_runtime_destroy(...)
```

The configuration must cover heap selection, startup script, LittleFS policy,
runtime task settings, REPL enablement, and an application hook that can install
additional globals after the built-in Host API is initialized.

Constraints for version `0.1.0`:

- Only one active mquickjs runtime is supported. The API reports this explicitly
  instead of relying on hidden global state.
- Destruction is allowed only after the runtime task has stopped.
- Active native resources must be closed or rejected clearly during shutdown;
  HTTP servers and registered callbacks are closed automatically, while an
  in-flight outgoing HTTP worker makes `destroy()` return
  `ESP_ERR_INVALID_STATE` until its bounded request completes.
- Unsafe forced task deletion is not part of the normal lifecycle.

Acceptance criteria:

- `main/app_main.c` contains only default configuration, create, and start.
- Headless and REPL builds use the same runtime component.
- Initialization failures release the JS context, native runtime state, and heap.
- A focused lifecycle test covers create failure and clean stop where possible.

## Phase 2: Callback Deadlines and Watchdog

Status: complete for `0.1.0`.

Every transition from native code into JavaScript must use one deadline-aware
call helper. This includes timers, GPIO interrupts, Wi-Fi callbacks, HTTP client
callbacks, HTTP server routes, and request/response helper callbacks.

Rules:

- Nested calls may shorten an existing deadline but never extend it.
- The previous deadline is restored after the callback returns.
- Callback timeout errors are reported through the existing exception path.
- The runtime task optionally subscribes to the ESP-IDF task watchdog as a final
  recovery layer; normal timeout handling should fire first.

Acceptance criteria:

- No direct module-level `JS_Call(...)` remains outside the core call helper.
- Timer callback timeout is covered by a board-backed regression test.
- Runtime stop wakes the task and does not wait forever on its idle notification.

## Phase 3: Independent Application Profiles

Status: complete for `0.1.0`.

Extend `scripts/remote.py` with:

```text
--app <name-or-path>
--app-sdkconfig-defaults <path>
--partition-table <path>
--flash-data-dir <complete-image-override>
apps
show-config
```

An application profile defines its label, flash-data directory, application
`sdkconfig.defaults`, and a board-specific partition table. Paths may use the
`{board}` and `{idf_target}` placeholders. Effective sdkconfig defaults are
ordered board first and application second; a generated final defaults file
selects the application partition CSV.

Configuration precedence:

1. command line
2. process environment / repository `.env`
3. application profile
4. framework default (`minimal`)

Acceptance criteria:

- `show-config` prints both board and application inputs.
- The selected app is passed to CMake explicitly, avoiding stale cache reuse.
- Existing test code can still supply a complete LittleFS source directory.
- Unknown or incomplete application profiles fail before a build starts.
- `show-config` exposes board defaults, application defaults, the generated
  combination-specific sdkconfig, and the selected partition table.
- Switching board/application combinations cannot reuse a stale generated
  sdkconfig from another combination.

## Phase 4: Release and Compatibility Baseline

Status: complete for the `0.1.0` baseline.

Deliverables:

- Root `README.md` with minimal-app and demo workflows.
- Root Apache-2.0 `LICENSE`.
- `version.txt` containing framework SemVer.
- `ESP32QJS_VERSION` and `ESP32QJS_HOST_API_VERSION` in the public C API.
- `sys.info()` fields `runtimeVersion` and `hostApiVersion`.
- A compatibility statement covering SemVer, Host API changes, JS library
  versions, and deprecation policy.
- Repository-relative documentation links.

Acceptance criteria:

- Firmware reports framework version `0.1.0` and Host API `1`.
- Documentation and TypeScript declarations describe both fields.
- Release metadata is available without Git history at runtime.

## Validation Matrix

Required before this plan is marked complete:

- [x] `python scripts/remote.py test --scope c`
- [x] `python scripts/remote.py --assume y build` with the default minimal app
- [x] `python scripts/remote.py --app demo --assume y build`
- [x] `python scripts/remote.py --board esp32c3_supermini --app minimal --assume y build`
- [x] Default board-backed JS test baseline after flashing the dedicated test image
- [x] `python scripts/remote.py check-js` for shared, application, test, and
  runnable API-documentation JavaScript
- [x] Configuration checks for local serial, application profiles, and RFC2217 normalization

The network and physical loopback suites remain opt-in and must be recorded
separately when executed.

## Follow-up Work

The following production work is intentionally outside phases 0-4:

- A/B OTA, rollback, and signed firmware/resources
- safe-mode boot after repeated startup failures
- secret provisioning and production security profiles
- public native class-ID allocation for third-party C modules
- CI build matrices, sanitizers, fuzzing, and fault-injection coverage
