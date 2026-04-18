#!/usr/bin/env python3
"""Unified helper for starting RFC2217 forwarding, flashing, and monitoring a remote ESP32-S3."""

from __future__ import annotations

import argparse
import platform
import shlex
import subprocess
import sys
import time
from pathlib import Path


ROOT_DIR = Path(__file__).resolve().parent.parent

ESPTOOL_CONFIG_TEXT = """[esptool]
custom_reset_sequence = R0|D0|W0.1|D1|R0|W0.1|R1|D0|R1|W0.1|D0|R0
custom_hard_reset_sequence = R1|W0.2|R0
"""

SETUP_CFG_TEXT = """[esp-idf-monitor]
custom_reset_sequence = R0|D0|W0.1|D1|R0|W0.1|R1|D0|R1|W0.1|D0|R0
custom_hard_reset_sequence = R1|W0.2|R0

[esptool]
custom_reset_sequence = R0|D0|W0.1|D1|R0|W0.1|R1|D0|R1|W0.1|D0|R0
custom_hard_reset_sequence = R1|W0.2|R0
"""


def run(
    cmd: list[str],
    cwd: Path | None = None,
    check: bool = True,
    interactive: bool = False,
) -> subprocess.CompletedProcess[str]:
    if interactive:
        return subprocess.run(cmd, cwd=cwd, check=check, text=True)
    return subprocess.run(cmd, cwd=cwd, check=check, text=True, capture_output=False)


def which(name: str) -> str | None:
    from shutil import which as _which

    return _which(name)


def esptool_config_path() -> Path:
    """Return the per-user esptool config path used by esptool / esp_rfc2217_server."""
    return Path.home() / "esptool.cfg"


def monitor_config_path() -> Path:
    """Return the project-local config path used by esp-idf-monitor."""
    return ROOT_DIR / "setup.cfg"


def write_esptool_config() -> Path:
    path = esptool_config_path()
    path.write_text(ESPTOOL_CONFIG_TEXT, encoding="ascii")
    return path


def write_monitor_config() -> Path:
    path = monitor_config_path()
    path.write_text(SETUP_CFG_TEXT, encoding="ascii")
    return path


def write_all_configs() -> tuple[Path, Path]:
    return write_esptool_config(), write_monitor_config()


def list_server_processes() -> list[str]:
    """Return running esp_rfc2217_server processes for the current platform."""
    target = "esp_rfc2217_server"

    if platform.system() == "Windows":
        result = subprocess.run(
            [
                "powershell",
                "-NoProfile",
                "-Command",
                (
                    "Get-CimInstance Win32_Process | "
                    "Where-Object { $_.Name -eq 'python.exe' -and "
                    "$_.CommandLine -like '*esp_rfc2217_server*' } | "
                    "ForEach-Object { '{0}`t{1}' -f $_.ProcessId, $_.CommandLine }"
                ),
            ],
            capture_output=True,
            text=True,
            check=False,
        )
        return [line.strip() for line in result.stdout.splitlines() if line.strip()]

    result = subprocess.run(
        ["ps", "-ax", "-o", "pid=,command="],
        capture_output=True,
        text=True,
        check=False,
    )
    return [line.strip() for line in result.stdout.splitlines() if target in line]


def stop_server_processes() -> None:
    """Stop existing RFC2217 server processes before restarting them."""
    if platform.system() == "Windows":
        subprocess.run(
            [
                "powershell",
                "-NoProfile",
                "-Command",
                (
                    "Get-CimInstance Win32_Process | "
                    "Where-Object { $_.Name -eq 'python.exe' -and "
                    "$_.CommandLine -like '*esp_rfc2217_server*' } | "
                    "ForEach-Object { Stop-Process -Id $_.ProcessId -Force }"
                ),
            ],
            check=False,
        )
        return

    subprocess.run(["pkill", "-f", "esp_rfc2217_server"], check=False)


def start_server(python_exe: str, listen_port: int, com_port: str, force_restart: bool) -> int:
    """Start esp_rfc2217_server after ensuring reset overrides exist."""
    esptool_cfg, setup_cfg = write_all_configs()

    if force_restart:
        stop_server_processes()
        time.sleep(1)

    existing = list_server_processes()
    if existing:
        print("esp_rfc2217_server is already running:")
        for item in existing:
            print(item)
        print(f"Using esptool config: {esptool_cfg}")
        print(f"Using monitor config: {setup_cfg}")
        return 0

    cmd = [python_exe, "-m", "esp_rfc2217_server", "-v", "-p", str(listen_port), com_port]
    kwargs: dict[str, object] = {
        "stdout": subprocess.DEVNULL,
        "stderr": subprocess.DEVNULL,
        "stdin": subprocess.DEVNULL,
        "start_new_session": True,
    }

    if platform.system() == "Windows":
        kwargs["creationflags"] = subprocess.CREATE_NO_WINDOW  # type: ignore[attr-defined]

    subprocess.Popen(cmd, **kwargs)
    time.sleep(1)

    existing = list_server_processes()
    if not existing:
        print("Failed to confirm esp_rfc2217_server startup.", file=sys.stderr)
        return 1

    print(f"RFC2217 server ready on {com_port} -> TCP {listen_port}")
    print(f"Using esptool config: {esptool_cfg}")
    print(f"Using monitor config: {setup_cfg}")
    for item in existing:
        print(item)
    return 0


def idf_py_cmd(args: list[str]) -> list[str]:
    """Return a command that can run idf.py with ESP-IDF exported if needed."""
    if which("idf.py"):
        return ["idf.py", *args]

    export_script = Path.home() / "esp" / "esp-idf" / "export.sh"
    if platform.system() != "Windows" and export_script.exists():
        joined = shlex.join(args)
        command = f"source {shlex.quote(str(export_script))} >/dev/null 2>&1 && exec idf.py {joined}"
        return ["bash", "-lc", command]

    raise SystemExit("idf.py not found. Export ESP-IDF first or set PATH accordingly.")


def build() -> None:
    """Build firmware."""
    run(idf_py_cmd(["build"]), cwd=ROOT_DIR, interactive=True)


def remote_url(host: str, port: int) -> str:
    """Return the validated RFC2217 URL for this project."""
    return f"rfc2217://{host}:{port}?ign_set_control&timeout=10"


def chip_id(esptool_bin: str, host: str, port: int) -> None:
    """Probe the remote device without writing flash."""
    write_esptool_config()
    subprocess.run(
        [esptool_bin, "--port", remote_url(host, port), "chip-id"],
        cwd=ROOT_DIR,
        check=True,
    )


def flash(esptool_bin: str, host: str, port: int, build_first: bool) -> None:
    """Flash the standard ESP-IDF build outputs over RFC2217."""
    write_esptool_config()

    if build_first:
        build()

    subprocess.run(
        [
            esptool_bin,
            "--chip",
            "esp32s3",
            "--after",
            "hard-reset",
            "--port",
            remote_url(host, port),
            "write-flash",
            "--flash-mode",
            "dio",
            "--flash-size",
            "2MB",
            "--flash-freq",
            "80m",
            "0x0",
            "build/bootloader/bootloader.bin",
            "0x8000",
            "build/partition_table/partition-table.bin",
            "0x10000",
            "build/esp32qjs.bin",
        ],
        cwd=ROOT_DIR,
        check=True,
    )


def monitor(host: str, port: int, baud: int) -> None:
    """Open ESP-IDF monitor over RFC2217."""
    write_monitor_config()
    run(
        idf_py_cmd(["-p", remote_url(host, port), "-b", str(baud), "monitor"]),
        cwd=ROOT_DIR,
        interactive=True,
    )


def flash_monitor(esptool_bin: str, host: str, port: int, build_first: bool, baud: int) -> None:
    """Build/flash, then open monitor over RFC2217."""
    flash(esptool_bin, host, port, build_first=build_first)
    monitor(host, port, baud)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Unified helper for remote ESP32-S3 flashing and monitoring."
    )
    sub = parser.add_subparsers(dest="command", required=True)

    server = sub.add_parser("server", help="Write config and start esp_rfc2217_server.")
    server.add_argument("--com-port", default="COM3")
    server.add_argument("--listen-port", type=int, default=4000)
    server.add_argument("--python-exe", default=sys.executable)
    server.add_argument("--force-restart", action="store_true")

    build_parser = sub.add_parser("build", help="Run idf.py build.")
    build_parser.set_defaults(_noop=True)

    chip = sub.add_parser("chip-id", help="Check remote RFC2217 connectivity.")
    chip.add_argument("--remote-host", default="192.168.68.54")
    chip.add_argument("--remote-port", type=int, default=4000)
    chip.add_argument("--esptool-bin", default="esptool")

    flash_parser = sub.add_parser("flash", help="Build and flash over RFC2217.")
    flash_parser.add_argument("--remote-host", default="192.168.68.54")
    flash_parser.add_argument("--remote-port", type=int, default=4000)
    flash_parser.add_argument("--esptool-bin", default="esptool")
    flash_parser.add_argument("--no-build", action="store_true")

    mon = sub.add_parser("monitor", help="Open ESP-IDF monitor over RFC2217.")
    mon.add_argument("--remote-host", default="192.168.68.54")
    mon.add_argument("--remote-port", type=int, default=4000)
    mon.add_argument("--baud", type=int, default=115200)

    fm = sub.add_parser("flash-monitor", help="Build/flash, then open monitor over RFC2217.")
    fm.add_argument("--remote-host", default="192.168.68.54")
    fm.add_argument("--remote-port", type=int, default=4000)
    fm.add_argument("--esptool-bin", default="esptool")
    fm.add_argument("--baud", type=int, default=115200)
    fm.add_argument("--no-build", action="store_true")

    return parser.parse_args()


def main() -> int:
    args = parse_args()

    if args.command == "server":
        return start_server(args.python_exe, args.listen_port, args.com_port, args.force_restart)

    if args.command == "build":
        build()
        return 0

    if args.command == "chip-id":
        chip_id(args.esptool_bin, args.remote_host, args.remote_port)
        return 0

    if args.command == "flash":
        flash(args.esptool_bin, args.remote_host, args.remote_port, build_first=not args.no_build)
        return 0

    if args.command == "monitor":
        monitor(args.remote_host, args.remote_port, args.baud)
        return 0

    if args.command == "flash-monitor":
        flash_monitor(
            args.esptool_bin,
            args.remote_host,
            args.remote_port,
            build_first=not args.no_build,
            baud=args.baud,
        )
        return 0

    return 2


if __name__ == "__main__":
    raise SystemExit(main())