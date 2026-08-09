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
- Publish Host API version `1` through `esp32.info()`.
- Establish Apache-2.0 licensing and initial framework documentation.
