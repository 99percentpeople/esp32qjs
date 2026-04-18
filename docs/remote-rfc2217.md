# Remote RFC2217 Flashing

This project supports direct flashing to a remote `XIAO ESP32-S3` through `esp_rfc2217_server`, but the Windows server must override the default reset sequence. Without this override, the server uses `ClassicReset`, which does not reliably put the board into download mode over `USB-Serial/JTAG`.

## Server Setup on Windows
Run [scripts/remote_flash.py](/home/zach/esp32qjs/scripts/remote_flash.py) on the machine that owns `COM3`:

```bash
python scripts/remote_flash.py server --com-port COM3 --listen-port 4000 --force-restart
```

The script writes `~/esptool.cfg` with the required reset overrides:

```ini
[esptool]
custom_reset_sequence = R0|D0|W0.1|D1|R0|W0.1|R1|D0|R1|W0.1|D0|R0
custom_hard_reset_sequence = R1|W0.2|R0
```

## Client Usage
From the development machine, use the same [scripts/remote_flash.py](/home/zach/esp32qjs/scripts/remote_flash.py) entrypoint:

```bash
python scripts/remote_flash.py chip-id
python scripts/remote_flash.py flash
```

Override the target when needed:

```bash
python scripts/remote_flash.py flash --remote-host 192.168.68.54 --remote-port 4000
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
