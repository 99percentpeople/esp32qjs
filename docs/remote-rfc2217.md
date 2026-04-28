# Remote RFC2217 Flashing

This project supports direct flashing to remote ESP32 boards through `esp_rfc2217_server`. The Windows server must override the default reset sequence. Without this override, the server uses `ClassicReset`, which does not reliably put `USB-Serial/JTAG` boards into download mode.

## Server Setup on Windows
Run `uv sync` once in the repository root to provision the repo-local Python tools. Then copy [.env.example](/home/zach/esp32qjs/.env.example) to `/.env`, choose a board profile with `BOARD=...`, and set the remote board address plus `IDF_PATH` once. `REMOTE_URL` is preferred; For `rfc2217://...` URLs, the helper auto-adds `ign_set_control` and `timeout=10` when they are missing. With `SERVER_PYTHON_EXE=auto`, the helper prefers the uv-managed `esptool` install automatically, and `build` / `monitor` derive `idf.py` and `export.sh` from `IDF_PATH`. Then run [scripts/remote.py](/home/zach/esp32qjs/scripts/remote.py) on the machine that owns the board:

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
From the development machine, use the same [scripts/remote.py](/home/zach/esp32qjs/scripts/remote.py) entrypoint:

```bash
uv sync
python scripts/remote.py boards
python scripts/remote.py show-config
python scripts/remote.py chip-id
python scripts/remote.py flash
python scripts/remote.py flash-fs
python scripts/remote.py monitor
python scripts/remote.py test
python scripts/remote.py --assume n build
```

Board profiles live under [`configs/boards/<board>/`](</home/zach/esp32qjs/configs/boards>). Each board directory carries its own `.env`, `sdkconfig.defaults`, and `partitions.csv`. For example:

```bash
python scripts/remote.py --board xiao_esp32s3 flash-monitor
python scripts/remote.py --board esp32c3_supermini build
python scripts/remote.py --board esp32c3_supermini chip-id
python scripts/remote.py --board esp32c3_supermini test --scope js --module fs --module stream
```

When a one-off override is needed, pass the board and connection overrides as top-level options before the subcommand, using either `--remote-host/--remote-port` or a full `--remote-url`:

```bash
python scripts/remote.py --remote-host 192.168.68.54 --remote-port 4000 flash
python scripts/remote.py --remote-url "rfc2217://192.168.68.54:4000?ign_set_control&timeout=10" monitor
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
- `build/storage.bin` at `0x150000`

For JavaScript-only changes under `flash_data/`, use the faster filesystem-only path:

```bash
python scripts/remote.py build-fs
python scripts/remote.py flash-fs
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
