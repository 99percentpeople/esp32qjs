# Repository Guidelines

## Project Structure
This is an ESP-IDF firmware project with board-specific profiles under [`configs/boards/`](</home/zach/esp32qjs/configs/boards>). App bootstrap lives in [`main/`](</home/zach/esp32qjs/main>), the QuickJS adapter and built-in modules live in [`components/esp32_mquickjs/`](</home/zach/esp32qjs/components/esp32_mquickjs>) with core glue in [`src/core/`](</home/zach/esp32qjs/components/esp32_mquickjs/src/core>) and feature modules in [`src/modules/`](</home/zach/esp32qjs/components/esp32_mquickjs/src/modules>), and the serial interaction shell lives in [`components/esp32qjs_interactive/`](</home/zach/esp32qjs/components/esp32qjs_interactive>). Host C tests are in [`tests/c/`](</home/zach/esp32qjs/tests/c>) and board-backed JS test assets are in [`tests/js/`](</home/zach/esp32qjs/tests/js>). Upstream `mquickjs` stays in [`components/esp32_mquickjs/vendor/mquickjs`](</home/zach/esp32qjs/components/esp32_mquickjs/vendor/mquickjs>), while generated headers are emitted into the active build directory under `build*/esp-idf/esp32_mquickjs/generated/`.

## Build, Flash, and Debug Commands
Use the repo scripts for normal development; do not default to direct `idf.py` workflows.

- `uv sync` installs the repo-local Python tooling.
- `cp .env.example .env` seeds the local remote-tool config; set `BOARD` and `IDF_PATH` as needed.
- `python scripts/remote.py boards` lists available board profiles.
- `python scripts/remote.py show-config` prints the merged board/tool configuration.
- `python scripts/remote.py --assume y build` builds for the active board.
- `python scripts/remote.py flash` builds and flashes the active board.
- `python scripts/remote.py flash-fs` refreshes only the LittleFS `storage` partition for JS-only changes.
- `python scripts/remote.py monitor` opens the serial monitor.
- `python scripts/remote.py test` runs the default automated test baseline and reflashes the latest firmware before board-backed JS tests.
- `python scripts/remote.py test --scope c` runs only host C tests.
- `python scripts/remote.py test --scope js --module fs --module stream --no-flash-firmware --no-flash-fs` reruns selected JS modules against an already flashed test image.
- `python scripts/remote.py test --scope js --module wifi --module http --network` enables network-required cases when `TEST_WIFI_SSID`, `TEST_WIFI_PASSWORD`, and `TEST_HTTP_URL` are configured.
- `python scripts/remote.py test --scope js --module spi --loopback` enables physical SPI loopback cases using the board defaults.
- `TEST_JS_CONFIG='{"spiLoopback":{"sclk":1,"mosi":2,"miso":2}}' python scripts/remote.py test --scope js --module spi --loopback` overrides the default SPI loopback pins.
- `python scripts/remote.py --board esp32c3_supermini build` switches boards without editing `.env`.

Keep `idf.py menuconfig` for configuration work and `cmake --build build --target update_mquickjs_headers` as a manual fallback for generated headers, but they are not the primary day-to-day workflow.

Remote flashing setup is documented in [docs/remote-rfc2217.md](/home/zach/esp32qjs/docs/remote-rfc2217.md).

## Coding Style
Use 4-space indentation and standard ESP-IDF C style. Prefer `snake_case` for functions and locals, `UPPER_SNAKE_CASE` for macros, and keep ESP32-specific code in the adapter layer instead of editing the submodule directly. Match existing logging and error-handling patterns with `ESP_LOG*`, `ESP_ERROR_CHECK`, and thin adapter helpers around third-party code.

## Testing
The default validation target is `python scripts/remote.py test`. It runs host C tests plus the JS modules enabled by the active board's runtime feature set, and it reflashes the latest firmware before board-backed JS tests so C-side changes are not left stale on the device. Use `--scope c`, `--scope js`, `--module ...`, `--network`, and `--loopback` to narrow or extend coverage when needed. Hardware-specific JS tests can read optional values from `TEST_JS_CONFIG`; SPI loopback cases require `--loopback`, use `spi.DEFAULT_*` by default, and `testConfig.spiLoopback` only overrides selected fields. Use `--no-flash-firmware` and `--no-flash-fs` only when you intentionally want to reuse what is already on the board.

For firmware changes, prefer `python scripts/remote.py --assume y build` and `python scripts/remote.py test` over manual `idf.py` checks. The offline JS baseline already covers the built-in runtime modules such as `core`, `esp32`, `gpio`, `ledc`, `adc`, `dac`, `i2c`, `spi`, `timers`, `fs`, `stream`, `load`, `wifi`, `http`, and `http_server`; modules disabled by `esp32.info().features` are auto-skipped, opt-in cases such as network and physical loopback are skipped unless their flag is passed, and explicitly requested disabled modules fail fast.

Only add extra manual verification when the change is inherently interactive or outside the automated harness, for example terminal UX, REPL multiline editing, or app-level LittleFS helpers such as `display` and `ui`. The canonical API reference lives in [docs/api.md](/home/zach/esp32qjs/docs/api.md), [docs/c-api.md](/home/zach/esp32qjs/docs/c-api.md), and [docs/js-api.md](/home/zach/esp32qjs/docs/js-api.md). When adding features, prefer extending `tests/c` or `tests/js` rather than mixing test logic into `app_main()`.

## Commits and Pull Requests
Use short imperative commit messages, for example `Add remote RFC2217 flash helper` or `Split LED init from app_main`. Keep pull requests narrowly scoped, describe the hardware used for validation, include the exact flash or monitor commands you ran, and attach boot logs for behavior changes.
