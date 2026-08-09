# Local And Remote Flashing

This project supports both direct local serial development and remote RFC2217 development through the same [scripts/remote.py](../scripts/remote.py) entrypoint. Use one `TARGET` value everywhere: if it is an `rfc2217://...` URL, the helper talks to a remote board; otherwise it treats the value as a local serial device path. The RFC2217 path still relies on `esp_rfc2217_server`, and the Windows server must override the default reset sequence. Without this override, the server uses `ClassicReset`, which does not reliably put `USB-Serial/JTAG` boards into download mode.

## Local Serial Usage
For a board connected to the current machine, set `TARGET=/dev/ttyACM0` or pass a one-off override:

```bash
python scripts/remote.py --target /dev/ttyACM0 chip-id
python scripts/remote.py --target /dev/ttyACM0 flash-fs
python scripts/remote.py --target /dev/ttyACM0 monitor
```

## Server Setup on Windows
Run `uv sync` once in the repository root to provision the repo-local Python tools. Then copy [.env.example](../.env.example) to `/.env`, choose board and application profiles with `BOARD=...` and `APP=...`, and set `TARGET` to the local serial device on the machine that owns the board. For `rfc2217://...` URLs, the helper auto-adds `ign_set_control` and `timeout=10` when they are missing. With `SERVER_PYTHON_EXE=auto`, the helper prefers the uv-managed `esptool` install automatically, and `build` / `monitor` derive `idf.py` and `export.sh` from `IDF_PATH`. Then run [scripts/remote.py](../scripts/remote.py) on the machine that owns the board:

```bash
python scripts/remote.py server --force-restart
```

The script writes `~/esptool.cfg` with the required reset overrides:

```ini
[esptool]
custom_reset_sequence = R0|D0|W0.1|D1|R0|W0.1|R1|D0|R1|W0.1|D0|R0
custom_hard_reset_sequence = R1|W0.2|R0
```

## Client Usage
From the development machine, use the same [scripts/remote.py](../scripts/remote.py) entrypoint:

```bash
uv sync
python scripts/remote.py boards
python scripts/remote.py apps
python scripts/remote.py show-config
python scripts/remote.py chip-id
python scripts/remote.py flash
python scripts/remote.py flash-fs
python scripts/remote.py monitor
python scripts/remote.py test
python scripts/remote.py --assume n build
```

Board profiles live under [`configs/boards/<board>/`](../configs/boards) and carry hardware target, pin, feature, and memory defaults. Bundled application profiles live under `apps/<app>/`; external applications may be selected by passing their directory or `app.env` path to `--app`, or by setting `APP_FILE`. Each application carries `app.env`, behavior `sdkconfig.defaults`, board-specific `partitions/<board>.csv`, and `flash_data/`. Its optional `APP_ID` provides a stable build identifier and is restricted to letters, digits, `.`, `_`, and `-`. Board defaults are applied before application defaults, while shared `_sys` libraries come from `shared/flash_data` and application files overlay them. For example:

```bash
python scripts/remote.py --board xiao_esp32s3 --app minimal flash-monitor
python scripts/remote.py --board xiao_esp32s3 --app demo build
python scripts/remote.py --board xiao_esp32s3 --app ../agent/device build
python scripts/remote.py --board esp32c3_supermini build
python scripts/remote.py --board esp32c3_supermini chip-id
python scripts/remote.py --board esp32c3_supermini test --scope js --module fs --module stream
```

When a one-off remote override is needed, pass a full RFC2217 URL as `--target`:

```bash
python scripts/remote.py --target "rfc2217://192.168.68.54:4000?ign_set_control&timeout=10" flash
python scripts/remote.py --target "rfc2217://192.168.68.54:4000?ign_set_control&timeout=10" monitor
python scripts/remote.py --board esp32c3_supermini --build-dir build-c3 monitor
```

For non-interactive runs, `--assume y` or `--assume n` answers all future yes/no prompts automatically. That is mainly useful when a stale build directory needs a confirmed delete-and-retry:

```bash
python scripts/remote.py --assume n build
python scripts/remote.py --assume y --board esp32c3_supermini build
```

The script flashes the files listed in the selected build directory's `flasher_args.json`, typically:

- `build/bootloader/bootloader.bin` at `0x0`
- `build/partition_table/partition-table.bin` at `0x8000`
- `build/esp32qjs.bin` at `0x10000`
- `build/storage.bin` at the selected partition table's `storage` offset (currently `0x210000` for the bundled boards)

`show-config` prints the exact board/application profile directory, defaults, combination-specific generated sdkconfig, partition table, and resource paths. Relative direct app references are resolved from the framework repository root. App-owned paths may use `{board}` and `{idf_target}` placeholders; missing inputs fail before ESP-IDF starts. One-off overrides are available through `--app-sdkconfig-defaults`, `--partition-table`, and `--flash-data-dir`.

For JavaScript-only changes under `shared/flash_data` or `apps/<app>/flash_data`, use the faster filesystem-only path:

```bash
python scripts/remote.py --app minimal build-fs
python scripts/remote.py --app demo flash-fs
```

For automated validation, use the unified test entrypoint:

```bash
python scripts/remote.py test
python scripts/remote.py test --scope c
python scripts/remote.py test --scope js --module fs --module stream --no-flash-firmware --no-flash-fs
python scripts/remote.py test --scope js --module wifi --module http --network
python scripts/remote.py test --scope js --module spi --loopback
TEST_JS_CONFIG='{"spiLoopback":{"sclk":1,"mosi":2,"miso":2}}' python scripts/remote.py test --scope js --module spi --loopback
```

By default `python scripts/remote.py test` runs host C tests plus the JS modules enabled by the active board's runtime feature set, and it reflashes the latest firmware before board-backed JS tests so C-side changes are not left stale on the device. During JS tests the firmware build and flash skip the normal `storage` image because the dedicated JS test LittleFS image is flashed next. The default JS baseline covers the built-in offline runtime APIs across `core`, `esp32`, `gpio`, `ledc`, `adc`, `dac`, `i2c`, `spi`, `uart`, `timers`, `fs`, `stream`, `load`, `wifi`, `http`, and `http_server`, but modules disabled by `esp32.info().features` are auto-skipped. Passing one or more `--module` flags narrows the run to only those modules; if an explicitly requested module is disabled on the active board, the JS stage fails fast with a clear error instead of silently skipping it. The `wifi` and `http` modules still run by default when enabled, but their network-required cases stay disabled unless `--network` is passed. SPI and UART loopback cases are also opt-in and stay disabled unless `--loopback` is passed. Use `--no-flash-firmware` and `--no-flash-fs` only when you intentionally want to reuse what is already on the board. Configure `TEST_WIFI_SSID`, `TEST_WIFI_PASSWORD`, and `TEST_HTTP_URL` in `/.env` or the selected board profile before enabling network cases. Hardware-specific JS tests can read optional values from `TEST_JS_CONFIG`; SPI loopback uses `spi.DEFAULT_*` by default and `testConfig.spiLoopback` only overrides selected fields, while UART loopback uses `uart.DEFAULT_*` and `testConfig.uartLoopback`. App-level LittleFS helpers such as `display` and `ui` are still outside the dedicated test image and should be validated separately when they change.

## Known Behavior
- `?ign_set_control&timeout=10` is required for this setup.
- `chip-id` and full `write-flash` were verified against both `ESP32-S3 USB-Serial/JTAG` and `ESP32-C3 USB-Serial/JTAG`.
- If the server is reachable but the board stays in the app, confirm `~/esptool.cfg` exists on the server host and restart the RFC2217 server.
- If post-flash reset is flaky, rerun `chip-id` first; if that works, the reset override is active and the link is healthy.
