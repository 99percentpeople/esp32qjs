# Repository Guidelines

## Project Structure
This is an ESP-IDF framework with hardware defaults under [`configs/boards/`](configs/boards), application behavior/partition/resource profiles under [`apps/`](apps), and shared LittleFS libraries under [`shared/flash_data/`](shared/flash_data). The reusable lifecycle component lives in [`components/esp32qjs_runtime/`](components/esp32qjs_runtime), while [`main/`](main) is only the default entry point. The QuickJS adapter and built-in modules live in [`components/esp32_mquickjs/`](components/esp32_mquickjs) with core glue in [`src/core/`](components/esp32_mquickjs/src/core) and feature modules in [`src/modules/`](components/esp32_mquickjs/src/modules), and the serial interaction shell lives in [`components/esp32qjs_interactive/`](components/esp32qjs_interactive). Host C tests are in [`tests/c/`](tests/c) and board-backed JS test assets are in [`tests/js/`](tests/js). Upstream `mquickjs` stays in [`components/esp32_mquickjs/vendor/mquickjs`](components/esp32_mquickjs/vendor/mquickjs), while generated headers are emitted into the active build directory under `build*/esp-idf/esp32_mquickjs/generated/`.

## Build, Flash, and Debug Commands
Use the repo scripts for normal development; do not default to direct `idf.py` workflows.

- `uv sync` installs the repo-local Python tooling.
- `cp .env.example .env` seeds the local tool config; set `BOARD`, `APP`, and `IDF_PATH` as needed.
- `python scripts/remote.py boards` lists available board profiles.
- `python scripts/remote.py apps` lists available application profiles.
- `python scripts/remote.py show-config` prints the merged board/app/sdkconfig/partition/resource/tool configuration.
- `python scripts/remote.py check-js` parses first-party JS and runnable API examples with the exact vendored MQuickJS engine.
- `python scripts/remote.py --assume y build` builds for the active board.
- `python scripts/remote.py flash` builds and flashes the active board.
- `python scripts/remote.py flash-fs` refreshes only the LittleFS `storage` partition for JS-only changes.
- `python scripts/remote.py flash --erase-workspace` initializes an optional workspace partition; ordinary flashes preserve it.
- `python scripts/remote.py flash-workspace` explicitly erases only the optional workspace partition.
- `python scripts/remote.py monitor` opens the serial monitor.
- `python scripts/remote.py test` runs the default automated test baseline and flashes the latest code with dedicated JS test sdkconfig defaults before board-backed JS tests.
- `python -m unittest discover -s tests/python` runs host tests for repository tooling and profile resolution.
- `python scripts/remote.py test --scope c` runs only host C tests.
- `python scripts/remote.py test --scope js --module fs --module stream --no-flash-firmware --no-flash-fs` reruns selected JS modules against an already flashed test image.
- `python scripts/remote.py test --scope js --module wifi --module http --network` enables network-required cases when `TEST_WIFI_SSID`, `TEST_WIFI_PASSWORD`, and `TEST_HTTP_URL` are configured.
- `python scripts/remote.py test --scope js --module spi --loopback` enables physical SPI loopback cases using the board defaults.
- `TEST_JS_CONFIG='{"spiLoopback":{"sclk":1,"mosi":2,"miso":2}}' python scripts/remote.py test --scope js --module spi --loopback` overrides the default SPI loopback pins.
- `python scripts/remote.py --board esp32c3_supermini build` switches boards without editing `.env`.
- `python scripts/remote.py --app demo build` builds the display demo while the default `minimal` app remains board-neutral.
- `python scripts/remote.py --app ../agent/device build` builds a sibling external application profile without copying it into `apps/`.

Keep `idf.py menuconfig` for configuration work and `cmake --build build --target update_mquickjs_headers` as a manual fallback for generated headers, but they are not the primary day-to-day workflow.

Local serial and RFC2217 targets are configured through `scripts/remote.py` and
the repository `.env` file.

## Coding Style
Use 4-space indentation and standard ESP-IDF C style. Prefer `snake_case` for functions and locals, `UPPER_SNAKE_CASE` for macros, and keep ESP32-specific code in the adapter layer instead of editing the submodule directly. Match existing logging and error-handling patterns with `ESP_LOG*`, `ESP_ERROR_CHECK`, and thin adapter helpers around third-party code.

## Testing
The default validation target is `python scripts/remote.py test`. It runs host C tests plus the JS modules enabled by the active board's runtime feature set, runs the MQuickJS syntax preflight before JS scope, and flashes the latest code with the dedicated JS test sdkconfig defaults before board-backed JS tests so C-side changes are not left stale on the device. Use `--scope c`, `--scope js`, `--module ...`, `--network`, and `--loopback` to narrow or extend coverage when needed. Hardware-specific JS tests can read optional values from `TEST_JS_CONFIG`; SPI loopback cases require `--loopback`, use `spi.DEFAULT_*` by default, and `testConfig.spiLoopback` only overrides selected fields. Use `--no-flash-firmware` and `--no-flash-fs` only when you intentionally want to reuse what is already on the board.

MQuickJS is a constrained ES5-like engine. Do not use `node --check` as the syntax authority and do not introduce `const`, `let`, classes, arrow functions, template literals, or other modern syntax directly into flashed sources. If a modern authoring source is ever added, its checked artifact must be explicitly transpiled before the MQuickJS preflight.

For firmware changes, prefer `python scripts/remote.py --assume y build` and `python scripts/remote.py test` over manual `idf.py` checks. The offline JS baseline already covers the built-in runtime modules such as `core`, `sys`, `gpio`, `ledc`, `adc`, `dac`, `i2c`, `spi`, `timers`, `fs`, `stream`, `load`, `displayBuffer`, `display`, `wifi`, `socket`, `http`, `http_server`, and `websocket`; modules disabled by `sys.info().features` are auto-skipped, opt-in cases such as network and physical loopback are skipped unless their flag is passed, and explicitly requested disabled modules fail fast.

Only add extra manual verification when the change is inherently interactive or outside the automated harness, for example terminal UX, REPL multiline editing, or app-level LittleFS helpers such as `display` and `ui`. The canonical API reference lives in [docs/api.md](docs/api.md), [docs/c-api.md](docs/c-api.md), and [docs/js-api.md](docs/js-api.md). When adding features, prefer extending `tests/c` or `tests/js` rather than mixing test logic into `app_main()`.

## Commits and Pull Requests
Use short imperative commit messages, for example `Add remote RFC2217 flash helper` or `Split LED init from app_main`. Keep pull requests narrowly scoped, describe the hardware used for validation, include the exact flash or monitor commands you ran, and attach boot logs for behavior changes.
