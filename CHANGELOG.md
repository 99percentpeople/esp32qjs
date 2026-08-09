# Changelog

All notable framework changes are documented here. ESP32QJS follows Semantic
Versioning; the native Host API uses a separate integer compatibility version.

## 0.1.0 - Unreleased

- Separate shared JavaScript system libraries from selectable applications.
- Add `minimal` and `demo` application profiles with `--app` tooling,
  application sdkconfig overlays, board-specific partitions, and resource overlays.
- Add the reusable `esp32qjs_runtime` lifecycle component with cooperative
  stop/destroy, native callback cleanup, and guarded in-flight HTTP teardown.
- Apply deadlines to every native-to-JavaScript callback path.
- Add optional runtime-task watchdog integration.
- Make timer handles generation-checked, clamp tight intervals, and cancel
  repeating timers whose callbacks fail.
- Bound HTTP response capture, add cancellable asynchronous requests, and add
  explicit server/route release methods.
- Add an exact-engine MQuickJS syntax preflight for flashed sources and runnable
  API examples, plus checked JavaScript declarations for apps/shared libraries.
- Replace inherited display-driver surfaces with a layered Display, Surface,
  PanelDriver, and Transport architecture; add explicit bus ownership and move
  WLK1501SPI8P wiring into a profile.
- Align display/UI declarations with runtime return values, optional colors,
  batching, capabilities, presentation statistics, and explicit close behavior.
- Add mutually exclusive headless USB serial framing and optional REPL builds,
  plus an outbound WebSocket text client with bounded callbacks and cleanup.
- Publish Host API version `1` through `esp32.info()`.
- Establish Apache-2.0 licensing and initial framework documentation.
