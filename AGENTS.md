# Repository Guidelines

## Project Structure
This is an ESP-IDF firmware project targeting `esp32s3`. Application logic lives in [`main/`](</home/zach/esp32qjs/main>): [`main/app_main.c`](/home/zach/esp32qjs/main/app_main.c) boots the runtime, [`main/repl.c`](/home/zach/esp32qjs/main/repl.c) owns REPL flow, and [`main/repl_input.c`](/home/zach/esp32qjs/main/repl_input.c) handles USB Serial/JTAG line editing. App-level resource tuning lives in [`main/Kconfig.projbuild`](/home/zach/esp32qjs/main/Kconfig.projbuild). The ESP32 adapter for the JS engine lives in [`components/esp32_mquickjs/`](</home/zach/esp32qjs/components/esp32_mquickjs>): [`src/esp32_mquickjs.c`](/home/zach/esp32qjs/components/esp32_mquickjs/src/esp32_mquickjs.c) owns runtime setup and global registration, while [`src/esp32_mquickjs_fs.c`](/home/zach/esp32qjs/components/esp32_mquickjs/src/esp32_mquickjs_fs.c), [`src/esp32_mquickjs_gpio.c`](/home/zach/esp32qjs/components/esp32_mquickjs/src/esp32_mquickjs_gpio.c), [`src/esp32_mquickjs_esp32.c`](/home/zach/esp32qjs/components/esp32_mquickjs/src/esp32_mquickjs_esp32.c), and [`src/esp32_mquickjs_wifi.c`](/home/zach/esp32qjs/components/esp32_mquickjs/src/esp32_mquickjs_wifi.c) hold module-specific host APIs. Component defaults for board pins, timeouts, timers, and HTTP capture limits live in [`components/esp32_mquickjs/Kconfig.projbuild`](/home/zach/esp32qjs/components/esp32_mquickjs/Kconfig.projbuild). Upstream `mquickjs` source stays in the submodule [`vendor/mquickjs`](</home/zach/esp32qjs/vendor/mquickjs>) and is wrapped as a local IDF-managed component under [`vendor/idf_components/mquickjs/`](</home/zach/esp32qjs/vendor/idf_components/mquickjs>). Generated headers required by the runtime are committed under `vendor/idf_components/mquickjs/generated/`. Top-level [CMakeLists.txt](/home/zach/esp32qjs/CMakeLists.txt) owns project setup, [`sdkconfig`](/home/zach/esp32qjs/sdkconfig) is the committed target configuration, and `build/` is generated output.

## Build, Flash, and Debug Commands
Run from the repository root after exporting ESP-IDF:

- `git submodule update --init --recursive` fetches the upstream engine source.
- `idf.py build` builds the firmware.
- `idf.py menuconfig` adjusts app/runtime defaults such as JS heap size and placement, REPL width, LittleFS mount/autorun behavior, LED pin, and HTTP response cap.
- `idf.py flash monitor` is the standard local edit-build-run loop.
- `idf.py -p COM3 flash` flashes a directly attached device.
- `idf.py -p COM3 monitor` opens the `USB Serial/JTAG` REPL locally at `115200`.
- `uv sync` provisions the repo-local Python tooling, including `esptool`.
- `cp .env.example .env` seeds the per-repo remote tooling defaults; set `ESP_IDF_PATH` to the local ESP-IDF install root.
- `python scripts/remote.py chip-id` verifies the remote RFC2217 link.
- `python scripts/remote.py flash` builds and flashes the remote `XIAO ESP32-S3`.
- `python scripts/remote.py flash-fs` rebuilds and flashes only the LittleFS `storage` partition for JavaScript-only changes.
- `python scripts/remote.py monitor` opens the remote serial monitor without retyping host and tool paths.
- `idf.py build` now regenerates and syncs the `mquickjs` generated headers automatically when upstream sources change or the generated files are missing.
- `cmake --build build --target update_mquickjs_headers` is still available as an explicit manual refresh target.

Remote flashing setup is documented in [docs/remote-rfc2217.md](/home/zach/esp32qjs/docs/remote-rfc2217.md).

## Coding Style
Use 4-space indentation and standard ESP-IDF C style. Prefer `snake_case` for functions and locals, `UPPER_SNAKE_CASE` for macros, and keep ESP32-specific code in the adapter layer instead of editing the submodule directly. Match existing logging and error-handling patterns with `ESP_LOG*`, `ESP_ERROR_CHECK`, and thin adapter helpers around third-party code.

## Testing
Every code change should pass `idf.py build`. JS REPL changes should also be verified interactively over serial, for example `help()`, `sleep(20)`, `esp32.info()`, `gpio.led(true)`, `JSON.stringify(wifi.status())`, `wifi.scan()`, `wifi.scan(function(results) { print(results.length); })`, `fetch("http://example.com")`, `fetch("https://example.com", function(error, response) { print(error === null, response.status); })`, `setTimeout(function() { print("once"); }, 200)`, `setInterval(function() { print("tick"); }, 150)`, and core file operations such as `fs.writeText("tmp.txt", "ok")`, `fs.readText("tmp.txt")`, and `fs.remove("tmp.txt")`. If `/littlefs/index.js` exists, also validate that it auto-runs before the first prompt. If Wi-Fi credentials are available, also validate `wifi.connect("ssid", "password")` and `wifi.disconnect()`. The detailed API reference lives in [docs/repl-api.md](/home/zach/esp32qjs/docs/repl-api.md). For editor changes, validate multiline continuation as well: typing `(1 +` then Enter should switch to the `... ` prompt, and pressing Enter before a closing `)` should insert a newline instead of submitting. When adding more features, introduce component-level tests or a `test/` directory rather than mixing test code into `app_main()`.

## Commits and Pull Requests
Use short imperative commit messages, for example `Add remote RFC2217 flash helper` or `Split LED init from app_main`. Keep pull requests narrowly scoped, describe the hardware used for validation, include the exact flash or monitor commands you ran, and attach boot logs for behavior changes.
