# ESP32QJS

ESP32QJS is an ESP-IDF framework for running trusted JavaScript applications on
ESP32 MCUs with mquickjs. It provides feature-gated native APIs for GPIO,
I2C, SPI, UART, USB serial frames, TCP lines, Wi-Fi, HTTP, WebSocket, LittleFS, bounded NVS strings, timers, and display buffers, plus
versioned JavaScript display and immediate-mode UI libraries.

Framework version: **0.1.0**

Native Host API version: **1**

## Repository Layout

```text
apps/                       application profiles, behavior, partitions, and files
  minimal/                  hardware-neutral default application
  demo/                     display and immediate-mode UI demos
shared/flash_data/_sys/     JavaScript libraries shared by applications
configs/mcus/               MCU targets and intrinsic feature defaults
components/esp32_mquickjs/  mquickjs adapter and native Host API
components/esp32qjs_runtime reusable runtime lifecycle component
components/esp32qjs_interactive optional serial REPL frontend
main/                       minimal firmware entry point
tests/                      host C and device-backed JavaScript tests
```

Configuration is layered as MCU defaults, application defaults, then a generated
hardware overlay for Flash, PSRAM, and optional wiring. Partition tables are generated
from the selected Flash capacity and application layout. The LittleFS image is built by
copying shared files first and the selected application's files second;
applications may intentionally override shared paths.

## Prerequisites

- Python 3.11 or newer
- `uv`
- ESP-IDF 6.1
- CMake and a host C compiler
- a recursive checkout containing the `mquickjs` submodule

```bash
git clone --recurse-submodules <repository-url>
cd esp32qjs
uv sync
cp .env.example .env
```

Set `IDF_PATH`, `MCU`, `APP`, and `TARGET` in `.env`. `TARGET` may be a local
serial device such as `/dev/ttyACM0` or an RFC2217 URL.

## Build and Flash

The default application is `minimal`, which safely boots into the REPL without
requiring display hardware.

```bash
python scripts/remote.py mcus
python scripts/remote.py apps
python scripts/remote.py show-config
python scripts/remote.py --assume y build
python scripts/remote.py flash
```

All generated output stays inside this standalone firmware repository's
`build/` directory. Normal profiles use `build/<mcu>`; additional hardware and
test profiles use subdirectories below that MCU. A relative `--build-dir` is
treated as a subdirectory of `build/`; use an absolute path only for an
intentional temporary build outside the repository.

Build or flash the display demo without changing the MCU profile:

```bash
python scripts/remote.py --app demo --assume y build
python scripts/remote.py --app demo flash
```

For JavaScript-only changes:

```bash
python scripts/remote.py check-js
python scripts/remote.py --app demo flash-fs
```

`check-js` builds a cached host parser from the vendored MQuickJS sources and
validates bundled applications, the selected external application, shared/test
sources, and runnable API-documentation snippets without executing them. The JS
test scope runs the same check automatically.

A complete LittleFS source directory can be supplied for compatibility or test
workflows with `--flash-data-dir PATH`. One-off application inputs can be
selected with `--app-sdkconfig-defaults PATH` and `--partition-table PATH`.
Use `--flash-size-mb`, `--psram-mode`, `--psram-size`, and `--wiring-config`
only with measured hardware values. Unknown PSRAM must use `--psram-mode none`.

## Create an Application

Copy the minimal profile and edit its entry point:

```text
apps/my_app/app.env
apps/my_app/sdkconfig.defaults
apps/my_app/flash_data/index.js
```

Example `app.env`:

```dotenv
APP_ID=my_app
APP_LABEL=My application
FLASH_DATA_DIR=flash_data
APP_SDKCONFIG_DEFAULTS=sdkconfig.defaults
PARTITION_LAYOUT=storage
```

Application defaults control behavior such as REPL/autorun policy, while MCU
defaults retain only target and intrinsic feature settings. Flash/PSRAM settings and
optional pin defaults are generated per build. `APP_ID` is the
stable build identifier and must contain only letters, digits, `.`, `_`, or
`-`. Select a bundled app with `--app my_app` or `APP=my_app` in `.env`.

An application may also live outside this repository. Pass its directory or
`app.env` path relative to the framework root (or as an absolute path):

```bash
python scripts/remote.py --app ../agent/device show-config
python scripts/remote.py --app ../agent/device --assume y build
python scripts/remote.py --app ../agent/device flash --erase-workspace
```

The destructive flag initializes an external profile's optional workspace
partition. Subsequent `flash` and `flash-fs` calls preserve it; use
`flash-workspace` only when an explicit workspace reset is intended.

External profiles own the same `app.env`, `sdkconfig.defaults`, partition layout,
and `flash_data/` structure as bundled profiles. Set `APP_FILE`
in `.env` for a persistent direct path. Shared libraries remain available
under `_sys/`, for example:

```js
load("_sys/display/wlk1501spi8p.js");
load("_sys/ui.js");
```

## Native Runtime Integration

Applications that need a custom C entry point can consume
`components/esp32qjs_runtime` and use:

```c
esp32qjs_runtime_default_config(&config);
esp32qjs_runtime_create(&config, &runtime);
esp32qjs_runtime_start(runtime);
```

The lifecycle also provides stop and destroy operations. A configuration hook
can install application-specific globals after the built-in Host API is ready.
Version `0.1.0` intentionally supports one active runtime.

## Testing

```bash
python -m unittest discover -s tests/python
python scripts/remote.py check-js
python scripts/remote.py test --scope c
python scripts/remote.py test
```

The default device-backed baseline builds and flashes the latest code with the
dedicated JS test sdkconfig defaults, then flashes a dedicated test LittleFS image. Network and physical loopback cases are opt-in:

```bash
python scripts/remote.py test --scope js --module nvs
python scripts/remote.py test --scope js --module tcp
python scripts/remote.py test --scope js --module wifi --module http --network
python scripts/remote.py test --scope js --module spi --module uart --loopback
```

## API and Pending Work

- [API index](docs/api.md)
- [Native Host API](docs/c-api.md)
- [JavaScript libraries](docs/js-api.md)
- [API stability plan](docs/api-stability-plan.md)
- [Native runtime integration](docs/runtime-api.md)
- [Framework backlog](docs/backlog.md)

The native Host API uses an integer compatibility version. Additive changes keep
the current version; incompatible changes increment it. Framework releases use
Semantic Versioning. JavaScript libraries retain their own version fields.

## Production Notes

The default profiles are development-oriented. Before product deployment,
review REPL enablement, LittleFS formatting, secure boot, flash encryption,
encrypted NVS/key provisioning, OTA/rollback, resource signing, and secret
provisioning. Enabling the optional `nvs` binding does not itself enable NVS
encryption. JavaScript code is trusted application code and is not
security-sandboxed.

## License

Licensed under the [Apache License 2.0](LICENSE).
