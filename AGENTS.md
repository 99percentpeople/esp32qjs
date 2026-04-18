# Repository Guidelines

## Project Structure
This is an ESP-IDF firmware project targeting `esp32s3`. Application logic lives in [`main/`](</home/zach/esp32qjs/main>): [`main/app_main.c`](/home/zach/esp32qjs/main/app_main.c) boots the runtime, [`main/repl.c`](/home/zach/esp32qjs/main/repl.c) owns REPL flow, and [`main/repl_input.c`](/home/zach/esp32qjs/main/repl_input.c) handles USB Serial/JTAG line editing. The ESP32 adapter for the JS engine lives in [`components/mquickjs/`](</home/zach/esp32qjs/components/mquickjs>), while upstream `mquickjs` source is tracked as the submodule [`vender/mquickjs`](</home/zach/esp32qjs/vender/mquickjs>). Generated headers required by the adapter are committed under `components/mquickjs/generated/`. Top-level [CMakeLists.txt](/home/zach/esp32qjs/CMakeLists.txt) owns project setup, [`sdkconfig`](/home/zach/esp32qjs/sdkconfig) is the committed target configuration, and `build/` is generated output.

## Build, Flash, and Debug Commands
Run from the repository root after exporting ESP-IDF:

- `git submodule update --init --recursive` fetches the upstream engine source.
- `idf.py build` builds the firmware.
- `idf.py flash monitor` is the standard local edit-build-run loop.
- `idf.py -p COM3 flash` flashes a directly attached device.
- `idf.py -p COM3 monitor` opens the `USB Serial/JTAG` REPL locally at `115200`.
- `uv sync` provisions the repo-local Python tooling, including `esptool`.
- `cp .env.example .env` seeds the per-repo remote tooling defaults; set `ESP_IDF_PATH` to the local ESP-IDF install root.
- `python scripts/remote.py chip-id` verifies the remote RFC2217 link.
- `python scripts/remote.py flash` builds and flashes the remote `XIAO ESP32-S3`.
- `python scripts/remote.py monitor` opens the remote serial monitor without retyping host and tool paths.
- `python scripts/update_mquickjs_headers.py` regenerates `mquickjs_atom.h` and `mqjs_stdlib.h` after updating the submodule.

Remote flashing setup is documented in [docs/remote-rfc2217.md](/home/zach/esp32qjs/docs/remote-rfc2217.md).

## Coding Style
Use 4-space indentation and standard ESP-IDF C style. Prefer `snake_case` for functions and locals, `UPPER_SNAKE_CASE` for macros, and keep ESP32-specific code in the adapter layer instead of editing the submodule directly. Match existing logging and error-handling patterns with `ESP_LOG*`, `ESP_ERROR_CHECK`, and thin adapter helpers around third-party code.

## Testing
Every code change should pass `idf.py build`. JS REPL changes should also be verified interactively over serial, for example `help()`, `help(esp32)`, `mem()`, `gc()`, `esp32.info()`, `esp32.led(true)`, `setTimeout(function() { print("once"); }, 200)`, and `setInterval(function() { print("tick"); }, 150)`. For editor changes, validate multiline continuation as well: typing `(1 +` then Enter should switch to the `... ` prompt, and pressing Enter before a closing `)` should insert a newline instead of submitting. When adding more features, introduce component-level tests or a `test/` directory rather than mixing test code into `app_main()`.

## Commits and Pull Requests
Use short imperative commit messages, for example `Add remote RFC2217 flash helper` or `Split LED init from app_main`. Keep pull requests narrowly scoped, describe the hardware used for validation, include the exact flash or monitor commands you ran, and attach boot logs for behavior changes.
