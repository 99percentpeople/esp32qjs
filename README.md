# ESP32QJS

ESP32QJS is a reusable ESP-IDF framework for running trusted JavaScript on
ESP32 microcontrollers with
[mquickjs](https://github.com/99percentpeople/mquickjs). It provides a bounded,
feature-gated native API while keeping development-board wiring, JavaScript
libraries, product protocols, and application policy outside the framework.

| | Current development baseline |
| --- | --- |
| Framework | `0.1.0` |
| Native Host API | `v1` |
| ESP-IDF | `6.1` |
| Targets | ESP32-S3, ESP32-C5, and ESP32-C3 |
| JavaScript engine | vendored mquickjs submodule |

## Framework capabilities

- One trusted JavaScript runtime with deadlines, cooperative native waits,
  `Future`, timers, and `EventQueue`.
- Explicit create/start/stop/destroy lifecycle and JavaScript-only restart.
- Native modules selected at build time instead of a monolithic image.
- Immutable build-time constants, measured Flash/PSRAM configuration, and an
  externally resolved partition table.
- Native byte ownership through `ByteView`, `ByteSpanSource`, and `Stream`.
- Exact-engine syntax checks, host tests, and device-backed module tests.

Feature-gated modules cover LittleFS and NVS; GPIO, LEDC, ADC, DAC, I2C, SPI,
UART and RMT; I2S and camera; native bitmap/display primitives; ESP-NETIF,
Wi-Fi, BLE, ESP-NOW, sockets, HTTP client/server, WebSocket and TLS; USB
Serial/JTAG, generic binary RPC, and runtime logs.
Read the compiled selection through `sys.info.features`.

## Framework boundary

This repository deliberately does not contain:

- development-board pin maps or vendor-specific Board wrappers;
- product JavaScript libraries or startup policy;
- Agent opcodes, authentication, workspace schema, or connection policy;
- model/provider orchestration, an online IDE, or a browser control plane.

An external resolver owns Boards and JavaScript Libraries, then produces one
immutable Build Context. The framework consumes that context and does not parse
the source manifests. JavaScript is trusted application code; ESP32QJS is not
an untrusted-code sandbox.

## Build Context v1

Every build requires a complete directory:

```text
<context>/
  manifest.json
  sdkconfig.defaults
  partitions.csv
  profile-constants.inc
  precompile.json
  flash_data/
```

`manifest.json` identifies the resolved context and target hardware. All other
files are immutable resolved inputs. Missing or inconsistent entries fail the
build; there is no application-profile, hardware-template, or board-pack
fallback. The repository includes
`tests/build-contexts/esp32s3/` only as a framework test fixture.

Configuration is applied in this order:

1. intrinsic MCU defaults from `configs/mcus/<target>/`;
2. the selected Build Context's sdkconfig defaults;
3. an optional explicit developer sdkconfig layer.

MCU defaults never guess development-board pins. Product hardware probes and
the external resolver must provide Flash, PSRAM, partitions, constants, native
features, JavaScript files, and precompile entries.

## Prerequisites and first build

- Python 3.11+
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

Set `IDF_PATH` in `.env`, then inspect and build the repository fixture:

```bash
uv run python scripts/remote.py mcus
uv run python scripts/remote.py show-config
uv run python scripts/remote.py check-js
uv run python scripts/remote.py --assume y build
```

To build a context produced elsewhere:

```bash
uv run python scripts/remote.py \
  --build-context /absolute/path/to/build-context \
  --assume y build
```

Generated output stays under `build/<mcu>/`. Direct `idf.py` remains available
for low-level work but bypasses Build Context validation.

## Feature selection

The Build Context enables only the native modules needed by its resolved Board
and Libraries. Typical entries include:

<!-- BEGIN GENERATED FEATURE CATALOG -->
| Feature | Capability | Targets | Requires |
| --- | --- | --- | --- |
| `fs` | Filesystem | esp32c3, esp32c5, esp32s3 | none |
| `nvs` | NVS | esp32c3, esp32c5, esp32s3 | none |
| `gpio` | GPIO | esp32c3, esp32c5, esp32s3 | none |
| `ledc` | LEDC | esp32c3, esp32c5, esp32s3 | none |
| `adc` | ADC | esp32c3, esp32c5, esp32s3 | none |
| `dac` | DAC | none | none |
| `i2c` | I2C | esp32c3, esp32c5, esp32s3 | none |
| `spi` | SPI | esp32c3, esp32c5, esp32s3 | none |
| `uart` | UART | esp32c3, esp32c5, esp32s3 | none |
| `rmt` | RMT | esp32c3, esp32c5, esp32s3 | none |
| `i2s` | I2S / PDM | esp32c3, esp32c5, esp32s3 | none |
| `camera` | Camera | esp32s3 | none |
| `net` | Network interfaces | esp32c3, esp32c5, esp32s3 | none |
| `wifi` | Wi-Fi | esp32c3, esp32c5, esp32s3 | `net` |
| `espnow` | ESP-NOW | esp32c3, esp32c5, esp32s3 | none |
| `ble` | Bluetooth LE | esp32c3, esp32c5, esp32s3 | none |
| `tls` | TLS | esp32c3, esp32c5, esp32s3 | `net` |
| `socket` | Socket | esp32c3, esp32c5, esp32s3 | `net` |
| `rpc` | Binary RPC | esp32c3, esp32c5, esp32s3 | none |
| `http_client` | HTTP Client | esp32c3, esp32c5, esp32s3 | `net` |
| `http_server` | HTTP Server | esp32c3, esp32c5, esp32s3 | `net` |
| `usb_serial` | USB Serial | esp32c3, esp32c5, esp32s3 | none |
| `websocket_client` | WebSocket | esp32c3, esp32c5, esp32s3 | `net`, `tls` |
| `bitmap` | Bitmap | esp32c3, esp32c5, esp32s3 | none |
| `runtime_logs` | Runtime logs | esp32c3, esp32c5, esp32s3 | none |
<!-- END GENERATED FEATURE CATALOG -->

```text
CONFIG_ESP32_MQUICKJS_FEATURE_FS=y
CONFIG_ESP32_MQUICKJS_FEATURE_NET=y
CONFIG_ESP32_MQUICKJS_FEATURE_WIFI=y
CONFIG_ESP32_MQUICKJS_FEATURE_SOCKET=y
CONFIG_ESP32_MQUICKJS_FEATURE_HTTP=y
CONFIG_ESP32_MQUICKJS_FEATURE_TLS=y
```

`net` owns shared ESP-NETIF initialization and is independent of Wi-Fi.
Ethernet, PPP, or a custom netif can support HTTP, sockets, WebSocket, TLS, and
network time without compiling the Wi-Fi module. TLS is separately optional.
Disabling it retains plaintext HTTP/TCP/UDP while removing the public CA bundle
and secure transports. WebSocket currently depends on TLS because ESP-IDF
packages WS and WSS in one component.

USB Serial/JTAG frames and the interactive REPL are mutually exclusive because
both consume the same serial stream.

## Memory and secure networking

PSRAM contexts prefer external memory for the JavaScript heap, managed large
buffers, worker stacks, media payloads, and mbedTLS. Internal/DMA memory remains
reserved for Wi-Fi and peripheral drivers. Diagnose pressure with
`sys.status.memory.internal`, `.dma`, and `.psram`, especially
`largestFreeBlockBytes` and `minimumFreeBytes`; aggregate free heap is not a
fragmentation metric.

TLS retains standard 16 KiB RX and 4 KiB TX records. Public CA, hostname, and
certificate dates are always verified; there is no insecure mode. After a
network interface receives an address, synchronize time before public TLS:

```js
sys.time.sync({
    servers: ["pool.ntp.org", "time.cloudflare.com"],
    timeoutMs: 15000
});
var response = http.fetch("https://example.com", { timeoutMs: 15000 });
print(response.status);
```

`Date.now()` uses synchronized wall time. `performance.now()`, `sys.millis()`,
and `sys.micros()` remain monotonic. SNTP supports ordinary certificate-date
validation but does not authenticate time against an active network attacker.

## JavaScript and documentation

The external Build Context may include ordinary JavaScript libraries under
`flash_data/`. `framework.load(path)` resolves immutable system library files;
`load(path)` uses the active application filesystem. Sources must use the
vendored MQuickJS ES5-like dialect, not modern Node syntax.

The complete native reference is under `docs/`. Compact framework facts for AI
consumers are ordinary Markdown files under `docs/ai/`, indexed by
`docs/ai/docs.json`. External Libraries and Boards can add their own ordinary
documentation. A host may snapshot all of them into Artifact-bound `doc://`
resources. Skills are a separate host concept for problem-solving workflows;
they are not the framework API or a hardware fact database.

## Flash, monitor, and test

```bash
uv run python scripts/remote.py flash
uv run python scripts/remote.py flash-fs
uv run python scripts/remote.py monitor
```

Ordinary `flash` and `flash-fs` preserve an optional workspace partition.
`flash --erase-workspace` and `flash-workspace` explicitly initialize it and
are destructive.

Host checks do not require a board:

```bash
uv run python scripts/generate_api_manifest.py --check
uv run python scripts/generate_feature_docs.py --check
uv run python -m unittest discover -s tests/python
uv run python scripts/remote.py check-js
uv run python scripts/remote.py test --scope c
```

GitHub Actions runs these checks and representative C3/C5/S3 Build Contexts on
pull requests. The separate hardware-lab workflow requires a configured
self-hosted runner and physical fixtures.

The default hardware test rebuilds and flashes dedicated test inputs before
running enabled JS modules:

```bash
uv run python scripts/remote.py test
uv run python scripts/remote.py test --scope js --module socket
uv run python scripts/remote.py test --scope js --module wifi --module http --network
uv run python scripts/remote.py test --scope js --module spi --module uart --loopback
uv run python scripts/remote.py test --scope js --module camera-bitmap --media-hardware
uv run python scripts/remote.py test --scope js --module espnow --wireless-hardware
```

Network tests require `TEST_WIFI_SSID`, `TEST_WIFI_PASSWORD`, and
`TEST_HTTP_URL`. Loopback and media tests require the corresponding hardware.
Use `--no-flash-firmware` and `--no-flash-fs` only when intentionally reusing a
compatible test image.

## Native runtime integration

Custom ESP-IDF applications can embed `components/esp32qjs_runtime` rather than
using `main/`:

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

See [Native runtime integration](docs/runtime-api.md) for lifecycle ownership.

## Repository layout

```text
configs/mcus/               intrinsic target defaults
components/esp32_mquickjs/  MQuickJS adapter and native modules
components/esp32qjs_runtime reusable runtime lifecycle
components/esp32qjs_interactive optional serial shell
main/                       default ESP-IDF entry point
docs/                       framework documentation
tests/build-contexts/       complete test-only Build Context fixtures
tests/c/                    host-native C tests
tests/js/                   device-backed JavaScript tests
tests/python/               repository tooling tests
scripts/remote.py           build, flash, monitor, and test helper
```

Start with [API index](docs/api.md), [Native Host API](docs/c-api.md),
[JavaScript Library boundary](docs/js-api.md), and
[framework backlog](docs/backlog.md).

ESP32QJS remains in development. Project-owned APIs and manifests use one v1
contract; breaking changes replace that contract directly. Production use
still requires an explicit review of signed artifacts, secure boot, Flash/NVS
encryption, provisioning, OTA rollback, and recovery.

Licensed under the [Apache License 2.0](LICENSE).
