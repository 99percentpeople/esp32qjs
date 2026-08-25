# ESP32QJS

ESP32QJS is a reusable ESP-IDF framework for running trusted JavaScript
applications on ESP32 microcontrollers with
[mquickjs](https://github.com/99percentpeople/mquickjs). It provides a bounded,
feature-gated native API while keeping board wiring, product protocols, and
application policy outside the framework.

| | Current development baseline |
| --- | --- |
| Framework | `0.1.0` |
| Native Host API | `v1` |
| ESP-IDF | `6.1` |
| Targets | ESP32-S3 and ESP32-C3 |
| JavaScript engine | vendored mquickjs submodule |

## What the framework provides

- A single trusted JavaScript runtime with bounded evaluation deadlines,
  cooperative native waits, `Future`, timers, and `EventQueue`.
- Explicit runtime create/start/stop/destroy lifecycle, JavaScript-only restart,
  startup guarding, and application-owned safe-mode policy.
- Optional native modules selected at build time instead of one monolithic
  firmware image.
- Generated hardware profiles for measured Flash and PSRAM capacity, partition
  geometry, and immutable application or driver constants.
- Native byte ownership through `ByteView`, `ByteSpanSource`, and `Stream` so
  large media and transport payloads do not need to become JavaScript arrays.
- Host tests, exact-engine JavaScript syntax checks, and device-backed module
  tests driven by one repository-local command.

The optional modules cover:

| Area | Capabilities |
| --- | --- |
| Storage | LittleFS, secondary LittleFS, bounded NVS strings |
| Peripherals | GPIO, LEDC, ADC, DAC, I2C, SPI, UART, RMT |
| Media | standard I2S RX/TX/duplex, PDM RX, ESP32-S3 still camera |
| Graphics | native mono1/gray8/RGB565/RGB888 bitmaps and display transports |
| Networking | Wi-Fi station, TCP/UDP sockets, HTTP client/server, WebSocket client |
| Security | optional public-CA TLS with hostname and certificate-date validation |
| Integration | USB Serial/JTAG frames and application-configured binary RPC codec |

Exact availability is target- and profile-dependent. Application code can read
the compiled selection through `sys.info.features`.

## Framework boundary

This repository is the generic firmware layer. It deliberately does **not**
contain:

- development-board pin maps or vendor-specific board wrappers;
- an Agent business protocol, authentication policy, or workspace schema;
- model/provider orchestration or credentials;
- an online IDE, device control plane, or browser UI.

Those concerns belong to the host product or application profile. A board pack
may supply generated constants and JavaScript display setup at build time, but
the native modules remain reusable across boards and products.

JavaScript runs as trusted application code. ESP32QJS is not an untrusted-code
sandbox.

## Prerequisites

- Python 3.11 or newer
- [uv](https://docs.astral.sh/uv/)
- ESP-IDF 6.1
- CMake and a host C compiler
- a recursive Git checkout

```bash
git clone --recurse-submodules https://github.com/99percentpeople/esp32qjs.git
cd esp32qjs
uv sync
cp .env.example .env
```

Set `IDF_PATH` in `.env`. The default profile uses ESP32-S3, the `minimal`
application, an 8 MiB Flash layout, and no assumed PSRAM. Set `TARGET` only when
flashing, monitoring, or running device-backed tests; it accepts a local serial
path such as `/dev/ttyACM0` or `COM3`, or an RFC2217 URL.

Inspect the resolved configuration before the first build:

```bash
uv run python scripts/remote.py mcus
uv run python scripts/remote.py apps
uv run python scripts/remote.py show-config
uv run python scripts/remote.py --assume y build
```

Use `scripts/remote.py` for normal development. Direct `idf.py` commands remain
available for low-level ESP-IDF work, but they bypass the framework's profile
resolution and validation.

## Configuration model

Build configuration is applied in this order:

1. MCU capability defaults from `configs/mcus/<target>/`.
2. Application behavior and feature defaults from `apps/<application>/` or an
   external application profile.
3. A generated hardware overlay containing measured Flash/PSRAM values and
   validated constants.
4. An optional host-generated module selection for product builds.

Partition tables are generated from the selected Flash capacity and application
layout. MCU profiles never guess development-board pins. Unknown PSRAM must use
the safe `none` profile; pass `--psram-mode` and `--psram-size` only with values
measured from the target hardware.

Useful overrides include:

```bash
# Safe no-PSRAM ESP32-C3 build.
uv run python scripts/remote.py \
  --mcu esp32c3 \
  --flash-size-mb 4 \
  --psram-mode none \
  --assume y build

# Measured 8 MiB octal-PSRAM ESP32-S3 build.
uv run python scripts/remote.py \
  --mcu esp32s3 \
  --flash-size-mb 8 \
  --psram-mode octal \
  --psram-size 8388608 \
  --assume y build
```

Generated output stays under `build/<mcu>/`. Additional hardware and test
profiles use subdirectories under the selected MCU. A relative `--build-dir`
also stays under `build/`; use an absolute build directory only for an
intentional temporary build.

## Feature selection

Native modules are feature-gated. Applications and host products enable only
what they need; MCU and hardware overlays may force unsupported modules off or
select a capability required by that profile. Application defaults can include:

```text
CONFIG_ESP32_MQUICKJS_FEATURE_FS=y
CONFIG_ESP32_MQUICKJS_FEATURE_WIFI=y
CONFIG_ESP32_MQUICKJS_FEATURE_SOCKET=y
CONFIG_ESP32_MQUICKJS_FEATURE_HTTP=y
CONFIG_ESP32_MQUICKJS_FEATURE_TLS=y
```

TLS is an independent capability. Disabling it keeps plaintext HTTP and
TCP/UDP available while removing the CA bundle and secure transport paths.
WebSocket currently requires TLS because the ESP-IDF WebSocket component packages
WS and WSS in one transport library. USB Serial/JTAG frames and the interactive
REPL are mutually exclusive because both consume the same serial input stream.

Run `show-config` to inspect the final merged selection rather than inferring
features from one defaults file.

## Memory and secure networking

Hardware profiles determine where scarce memory is used:

- PSRAM profiles prefer external RAM for the JavaScript heap, managed large
  buffers, HTTP worker stacks, media payloads, and mbedTLS allocations.
- No-PSRAM profiles keep selected API contracts but use bounded internal
  allocations and omit capabilities that cannot fit that hardware profile.
- Internal/DMA memory remains reserved for Wi-Fi and peripheral drivers. Inspect
  `sys.status.memory.internal`, `.dma`, and `.psram`, especially
  `largestFreeBlockBytes` and `minimumFreeBytes`; total `freeHeap()` alone is not
  a fragmentation metric on PSRAM systems.

TLS retains standard 16 KiB RX and 4 KiB TX records. Public CA, hostname, and
certificate dates are always verified; there is no insecure or
skip-verification mode. After a network interface obtains an address, the
application supplies its own SNTP servers and synchronizes the wall clock before
public TLS:

```js
wifi.connect("network", "password", 15000);
sys.time.sync({
    servers: ["pool.ntp.org", "time.cloudflare.com"],
    timeoutMs: 15000
});
var response = http.fetch("https://example.com", { timeoutMs: 15000 });
print(response.status);
```

`Date.now()` and `new Date()` use the synchronized wall clock.
`performance.now()`, `sys.millis()`, and `sys.micros()` remain monotonic uptime
clocks. SNTP provides ordinary date validation and does not authenticate network
time against an active attacker.

## Create an application

A bundled JavaScript application profile contains:

```text
apps/my_app/
  app.env
  sdkconfig.defaults
  flash_data/
    index.js
```

Minimal `app.env`:

```dotenv
APP_ID=my_app
APP_LABEL=My application
FLASH_DATA_DIR=flash_data
APP_SDKCONFIG_DEFAULTS=sdkconfig.defaults
PARTITION_LAYOUT=storage
```

The application owns autorun/REPL policy and feature defaults. Hardware
capacity and board constants remain generated inputs. Select a bundled profile
with `--app my_app`, or pass an external directory or `app.env` without copying
it into this repository:

```bash
uv run python scripts/remote.py --app ../my-device-app show-config
uv run python scripts/remote.py --app ../my-device-app --assume y build
```

Shared JavaScript libraries are copied from `shared/flash_data/` before the
application files, so applications can intentionally override shared paths.
Board-specific display code belongs in an application or host-supplied board
pack. The framework supplies native bitmap and bus primitives plus generic
JavaScript display drivers.

Flashed sources must use the vendored MQuickJS ES5-like dialect. Do not assume
that Node.js syntax checking accepts the same language. Run `check-js`, and
avoid `const`, `let`, classes, arrow functions, and template literals unless a
separate authoring pipeline explicitly transpiles them first.

## Build, flash, and monitor

```bash
# Build or flash the hardware-neutral minimal application.
uv run python scripts/remote.py --assume y build
uv run python scripts/remote.py flash

# Build and flash the bundled display demo.
uv run python scripts/remote.py --app demo --assume y build
uv run python scripts/remote.py --app demo flash

# Validate or update JavaScript without rebuilding the application image.
uv run python scripts/remote.py check-js
uv run python scripts/remote.py --app demo flash-fs

# Open the serial monitor.
uv run python scripts/remote.py monitor
```

Ordinary `flash` and `flash-fs` preserve an external application's optional
workspace partition. `flash --erase-workspace` initializes that partition while
flashing the firmware; `flash-workspace` explicitly erases and initializes only
the workspace. Treat both operations as destructive.

## Native runtime integration

Custom ESP-IDF applications can embed `components/esp32qjs_runtime` instead of
using the default `main/` entry point:

```c
#include "esp32qjs_runtime.h"

static esp32qjs_runtime_t *runtime;

void app_main(void)
{
    esp32qjs_runtime_config_t config;

    esp32qjs_runtime_default_config(&config);
    ESP_ERROR_CHECK(esp32qjs_runtime_create(&config, &runtime));
    ESP_ERROR_CHECK(esp32qjs_runtime_start(runtime));
}
```

The lifecycle API also provides cooperative stop and destroy operations plus an
application callback for installing trusted native globals. See
[Native runtime integration](docs/runtime-api.md) for ownership and shutdown
requirements.

## Testing

Host-side checks do not require a connected board:

```bash
uv run python -m unittest discover -s tests/python
uv run python scripts/remote.py check-js
uv run python scripts/remote.py test --scope c
```

The default device test command rebuilds and flashes dedicated test firmware and
LittleFS images before running enabled JavaScript modules:

```bash
uv run python scripts/remote.py test
```

Narrow or extend hardware tests explicitly:

```bash
uv run python scripts/remote.py test --scope js --module nvs
uv run python scripts/remote.py test --scope js --module socket
uv run python scripts/remote.py test --scope js --module wifi --module http --network
uv run python scripts/remote.py test --scope js --module spi --module uart --loopback
uv run python scripts/remote.py test --scope js --module camera-bitmap --media-hardware
```

Network tests require `TEST_WIFI_SSID`, `TEST_WIFI_PASSWORD`, and
`TEST_HTTP_URL`. Loopback and media tests require the corresponding physical
wiring or hardware profile. Use `--no-flash-firmware` and `--no-flash-fs` only
when intentionally reusing an already compatible device test image.

## Repository layout

```text
apps/                       bundled application profiles and files
configs/mcus/               intrinsic MCU targets and capability exclusions
shared/flash_data/_sys/     reusable JavaScript libraries and display drivers
components/esp32_mquickjs/  mquickjs adapter and feature-gated Native Host API
components/esp32qjs_runtime reusable runtime lifecycle component
components/esp32qjs_interactive optional serial REPL frontend
main/                       default standalone ESP-IDF entry point
tests/c/                    host-native C tests
tests/js/                   device-backed JavaScript tests
tests/python/               profile and repository tooling tests
scripts/remote.py           supported build, flash, monitor, and test workflow
```

## Documentation

- [API index](docs/api.md)
- [Native Host API](docs/c-api.md)
- [JavaScript libraries](docs/js-api.md)
- [System management API](docs/sys-management-api.md)
- [Native runtime integration](docs/runtime-api.md)
- [API stability plan](docs/api-stability-plan.md)
- [Framework backlog](docs/backlog.md)

ESP32QJS is still in development. Project-owned APIs, protocols, manifests, and
persistence formats remain on the sole `v1` contract; breaking changes replace
that contract directly instead of adding compatibility layers. Framework
release numbers follow Semantic Versioning independently of the Host API
contract.

## Production boundary

The default profiles are development-oriented. Production deployments still
need an explicit review of REPL exposure, signed artifacts, secure boot, Flash
and NVS encryption, secret provisioning/rotation, OTA rollback, and recovery
under interrupted writes. Enabling the optional `nvs` module does not enable NVS
encryption.

See the [framework backlog](docs/backlog.md) for unfinished production and
verification work.

## License

Licensed under the [Apache License 2.0](LICENSE).
