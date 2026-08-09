# ESP32QJS

ESP32QJS is an ESP-IDF framework for running trusted JavaScript applications on
ESP32 boards with mquickjs. It provides feature-gated native APIs for GPIO,
I2C, SPI, UART, Wi-Fi, HTTP, LittleFS, timers, and display buffers, plus
versioned JavaScript display and immediate-mode UI libraries.

Framework version: **0.1.0**

Native Host API version: **1**

## Repository Layout

```text
apps/                       application profiles, behavior, partitions, and files
  minimal/                  board-neutral default application
  demo/                     display and immediate-mode UI demos
shared/flash_data/_sys/     JavaScript libraries shared by applications
configs/boards/             board targets, pins, features, and memory defaults
components/esp32_mquickjs/  mquickjs adapter and native Host API
components/esp32qjs_runtime reusable runtime lifecycle component
components/esp32qjs_interactive serial REPL frontend
main/                       minimal firmware entry point
tests/                      host C and board-backed JavaScript tests
```

Configuration is layered as board defaults, then application defaults, then the
application's board-specific partition table. The LittleFS image is built by
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

Set `IDF_PATH`, `BOARD`, `APP`, and `TARGET` in `.env`. `TARGET` may be a local
serial device such as `/dev/ttyACM0` or an RFC2217 URL.

## Build and Flash

The default application is `minimal`, which safely boots into the REPL without
requiring display hardware.

```bash
python scripts/remote.py boards
python scripts/remote.py apps
python scripts/remote.py show-config
python scripts/remote.py --assume y build
python scripts/remote.py flash
```

Build or flash the display demo without changing the board profile:

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
validates application, shared, test, and runnable API-documentation snippets
without executing them. The JS test scope runs the same check automatically.

A complete LittleFS source directory can be supplied for compatibility or test
workflows with `--flash-data-dir PATH`. One-off application inputs can be
selected with `--app-sdkconfig-defaults PATH` and `--partition-table PATH`.

## Create an Application

Copy the minimal profile and edit its entry point:

```text
apps/my_app/app.env
apps/my_app/sdkconfig.defaults
apps/my_app/partitions/xiao_esp32s3.csv
apps/my_app/partitions/esp32c3_supermini.csv
apps/my_app/flash_data/index.js
```

Example `app.env`:

```dotenv
APP_LABEL=My application
FLASH_DATA_DIR=flash_data
APP_SDKCONFIG_DEFAULTS=sdkconfig.defaults
PARTITION_TABLE=partitions/{board}.csv
```

`{board}` and `{idf_target}` are expanded from the selected board profile.
Application defaults control behavior such as REPL/autorun policy, while board
defaults retain target, pin, feature, and memory settings. Select the app with
`--app my_app` or `APP=my_app` in `.env`. Shared libraries remain
available under `_sys/`, for example:

```js
load("_sys/display.js");
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

The default board-backed baseline flashes the latest firmware and a dedicated
test LittleFS image. Network and physical loopback cases are opt-in:

```bash
python scripts/remote.py test --scope js --module wifi --module http --network
python scripts/remote.py test --scope js --module spi --module uart --loopback
```

## API and Plans

- [API index](docs/api.md)
- [Native Host API](docs/c-api.md)
- [JavaScript libraries](docs/js-api.md)
- [Display/driver redesign proposal](docs/display-driver-redesign.md)
- [API stability plan](docs/api-stability-plan.md)
- [Native runtime integration](docs/runtime-api.md)
- [Framework plan](docs/framework-plan.md)
- [Local and RFC2217 workflows](docs/remote-rfc2217.md)

The native Host API uses an integer compatibility version. Additive changes keep
the current version; incompatible changes increment it. Framework releases use
Semantic Versioning. JavaScript libraries retain their own version fields.

## Production Notes

The default profiles are development-oriented. Before product deployment,
review REPL enablement, LittleFS formatting, secure boot, flash encryption,
OTA/rollback, resource signing, and secret provisioning. JavaScript code is
trusted application code and is not security-sandboxed.

## License

Licensed under the [Apache License 2.0](LICENSE).
