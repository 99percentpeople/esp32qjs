# Remote RFC2217 Flashing

This project supports direct flashing to a remote `XIAO ESP32-S3` through `esp_rfc2217_server`, but the Windows server must override the default reset sequence. Without this override, the server uses `ClassicReset`, which does not reliably put the board into download mode over `USB-Serial/JTAG`.

## Server Setup on Windows
Run `uv sync` once in the repository root to provision the repo-local Python tools. Then copy [.env.example](/home/zach/esp32qjs/.env.example) to `/.env` and set the local tool paths plus the remote board address once. `REMOTE_URL` is preferred; the script also accepts the legacy `ESPPORT` variable. For `rfc2217://...` URLs, the helper auto-adds `ign_set_control` and `timeout=10` when they are missing. With `ESPTOOL_BIN=auto` and `SERVER_PYTHON_EXE=auto`, the helper prefers the uv-managed `esptool` install automatically. Then run [scripts/remote.py](/home/zach/esp32qjs/scripts/remote.py) on the machine that owns `COM3`:

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
python scripts/remote.py chip-id
python scripts/remote.py flash
python scripts/remote.py monitor
```

When a one-off override is needed, pass either `--remote-host/--remote-port` or a full `--remote-url`:

```bash
python scripts/remote.py flash --remote-host 192.168.68.54 --remote-port 4000
python scripts/remote.py monitor --remote-url "rfc2217://192.168.68.54:4000?ign_set_control&timeout=10"
```

The script flashes:

- `build/bootloader/bootloader.bin` at `0x0`
- `build/partition_table/partition-table.bin` at `0x8000`
- `build/esp32qjs.bin` at `0x10000`

## Known Behavior
- `?ign_set_control&timeout=10` is required for this setup.
- `chip-id` and full `write-flash` were verified against `ESP32-S3 USB-Serial/JTAG`.
- If the server is reachable but the board stays in the app, confirm `~/esptool.cfg` exists on the server host and restart the RFC2217 server.
- If post-flash reset is flaky, rerun `chip-id` first; if that works, the reset override is active and the link is healthy.
