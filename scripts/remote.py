#!/usr/bin/env python3
"""Unified helper for starting RFC2217 forwarding, flashing, and monitoring a remote ESP32-S3.

The script reads optional defaults from `/.env` in the repository root so the
common commands stay short during day-to-day use.
"""

from __future__ import annotations

import argparse
import os
import platform
import shlex
import subprocess
import sys
import time
from pathlib import Path
from urllib.parse import parse_qsl, quote, urlsplit, urlunsplit


ROOT_DIR = Path(__file__).resolve().parent.parent
ENV_PATH = ROOT_DIR / ".env"

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


def load_dotenv(path: Path) -> dict[str, str]:
    """Load simple `KEY=VALUE` pairs from the repository-local `.env` file."""
    values: dict[str, str] = {}
    if not path.exists():
        return values

    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("export "):
            line = line[7:].strip()
        if "=" not in line:
            continue

        key, value = line.split("=", 1)
        key = key.strip()
        value = value.strip()
        if value[:1] == value[-1:] and value[:1] in {"'", '"'}:
            value = value[1:-1]
        values[key] = value

    return values


def env_value(dotenv: dict[str, str], key: str, default: str) -> str:
    """Resolve a configuration value from process env, then `/.env`, then fallback."""
    return os.environ.get(key, dotenv.get(key, default))


def env_int(dotenv: dict[str, str], key: str, default: int) -> int:
    """Resolve and validate an integer configuration value."""
    raw_value = os.environ.get(key, dotenv.get(key))
    if raw_value is None:
        return default

    try:
        return int(raw_value)
    except ValueError as exc:
        raise SystemExit(f"Invalid integer for {key}: {raw_value!r}") from exc


def load_defaults() -> dict[str, str | int]:
    """Build CLI defaults from environment variables and `/.env`."""
    dotenv = load_dotenv(ENV_PATH)

    remote_port = env_int(dotenv, "REMOTE_PORT", 4000)
    remote_url_override = (
        os.environ.get("REMOTE_URL")
        or dotenv.get("REMOTE_URL")
        or os.environ.get("ESPPORT")
        or dotenv.get("ESPPORT")
        or ""
    )

    return {
        "com_port": env_value(dotenv, "COM_PORT", "COM3"),
        "listen_port": env_int(dotenv, "LISTEN_PORT", remote_port),
        "server_python_exe": env_value(dotenv, "SERVER_PYTHON_EXE", sys.executable),
        "remote_host": env_value(dotenv, "REMOTE_HOST", "192.168.68.54"),
        "remote_port": remote_port,
        "remote_url": remote_url_override,
        "esptool_bin": env_value(dotenv, "ESPTOOL_BIN", "esptool"),
        "idf_py_bin": env_value(dotenv, "IDF_PY_BIN", "idf.py"),
        "esp_idf_export_sh": env_value(
            dotenv, "ESP_IDF_EXPORT_SH", str(Path.home() / "esp" / "esp-idf" / "export.sh")
        ),
        "monitor_baud": env_int(dotenv, "MONITOR_BAUD", env_int(dotenv, "ESPBAUD", 115200)),
    }


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


def idf_py_cmd(args: list[str], idf_py_bin: str, export_script: str) -> list[str]:
    """Return a command that can run `idf.py`, exporting ESP-IDF on Unix when needed."""
    export_script_path = Path(export_script).expanduser()
    idf_env_ready = bool(os.environ.get("IDF_PATH") and os.environ.get("ESP_IDF_VERSION"))

    if platform.system() != "Windows" and export_script_path.exists() and not idf_env_ready:
        joined = shlex.join(args)
        command = (
            f"source {shlex.quote(str(export_script_path))} >/dev/null 2>&1 "
            f"&& exec {shlex.quote(idf_py_bin)} {joined}"
        )
        return ["bash", "-lc", command]

    if Path(idf_py_bin).exists() or which(idf_py_bin):
        return [idf_py_bin, *args]

    if platform.system() != "Windows" and export_script_path.exists():
        joined = shlex.join(args)
        command = (
            f"source {shlex.quote(str(export_script_path))} >/dev/null 2>&1 "
            f"&& exec {shlex.quote(idf_py_bin)} {joined}"
        )
        return ["bash", "-lc", command]

    raise SystemExit(
        f"{idf_py_bin} not found. Set IDF_PY_BIN/ESP_IDF_EXPORT_SH in /.env or export ESP-IDF first."
    )


def build(idf_py_bin: str, export_script: str) -> None:
    """Build firmware."""
    run(idf_py_cmd(["build"], idf_py_bin, export_script), cwd=ROOT_DIR, interactive=True)


def default_remote_url(host: str, port: int) -> str:
    """Return the project's default RFC2217 URL."""
    return f"rfc2217://{host}:{port}?ign_set_control&timeout=10"


def encode_query_items(items: list[tuple[str, str]]) -> str:
    """Encode RFC2217 query items while preserving flag-style parameters."""
    return "&".join(
        name if value == "" else f"{quote(name, safe='')}={quote(value, safe='')}"
        for name, value in items
    )


def normalize_remote_url(raw_url: str) -> str:
    """Normalize legacy RFC2217 URLs so old `.env` values keep working."""
    if not raw_url:
        return raw_url

    parts = urlsplit(raw_url)
    if parts.scheme != "rfc2217":
        return raw_url

    query_items = parse_qsl(parts.query, keep_blank_values=True)
    keys = {name for name, _ in query_items}

    if "ign_set_control" not in keys:
        query_items.append(("ign_set_control", ""))
    if "timeout" not in keys:
        query_items.append(("timeout", "10"))

    return urlunsplit(parts._replace(query=encode_query_items(query_items)))


def remote_url(remote_url_override: str, host: str, port: int) -> str:
    """Return the RFC2217 URL, preferring an explicit override from CLI or `/.env`."""
    return normalize_remote_url(remote_url_override or default_remote_url(host, port))


def chip_id(esptool_bin: str, remote_url_override: str, host: str, port: int) -> None:
    """Probe the remote device without writing flash."""
    write_esptool_config()
    subprocess.run(
        [esptool_bin, "--port", remote_url(remote_url_override, host, port), "chip-id"],
        cwd=ROOT_DIR,
        check=True,
    )


def flash(
    esptool_bin: str,
    remote_url_override: str,
    host: str,
    port: int,
    build_first: bool,
    idf_py_bin: str,
    export_script: str,
) -> None:
    """Flash the standard ESP-IDF build outputs over RFC2217."""
    write_esptool_config()

    if build_first:
        build(idf_py_bin, export_script)

    subprocess.run(
        [
            esptool_bin,
            "--chip",
            "esp32s3",
            "--after",
            "hard-reset",
            "--port",
            remote_url(remote_url_override, host, port),
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


def monitor(remote_url_override: str, host: str, port: int, baud: int, idf_py_bin: str, export_script: str) -> None:
    """Open ESP-IDF monitor over RFC2217."""
    write_monitor_config()
    run(
        idf_py_cmd(
            ["-p", remote_url(remote_url_override, host, port), "-b", str(baud), "monitor"],
            idf_py_bin,
            export_script,
        ),
        cwd=ROOT_DIR,
        interactive=True,
    )


def flash_monitor(
    esptool_bin: str,
    remote_url_override: str,
    host: str,
    port: int,
    build_first: bool,
    baud: int,
    idf_py_bin: str,
    export_script: str,
) -> None:
    """Build/flash, then open monitor over RFC2217."""
    flash(
        esptool_bin,
        remote_url_override,
        host,
        port,
        build_first=build_first,
        idf_py_bin=idf_py_bin,
        export_script=export_script,
    )
    monitor(remote_url_override, host, port, baud, idf_py_bin, export_script)


def parse_args() -> argparse.Namespace:
    defaults = load_defaults()
    parser = argparse.ArgumentParser(
        description="Unified helper for remote ESP32-S3 flashing and monitoring."
    )
    sub = parser.add_subparsers(dest="command", required=True)

    server = sub.add_parser("server", help="Write config and start esp_rfc2217_server.")
    server.add_argument("--com-port", default=defaults["com_port"])
    server.add_argument("--listen-port", type=int, default=defaults["listen_port"])
    server.add_argument("--python-exe", default=defaults["server_python_exe"])
    server.add_argument("--force-restart", action="store_true")

    build_parser = sub.add_parser("build", help="Run idf.py build.")
    build_parser.set_defaults(_noop=True)

    chip = sub.add_parser("chip-id", help="Check remote RFC2217 connectivity.")
    chip.add_argument("--remote-url", default=defaults["remote_url"])
    chip.add_argument("--remote-host", default=defaults["remote_host"])
    chip.add_argument("--remote-port", type=int, default=defaults["remote_port"])
    chip.add_argument("--esptool-bin", default=defaults["esptool_bin"])

    flash_parser = sub.add_parser("flash", help="Build and flash over RFC2217.")
    flash_parser.add_argument("--remote-url", default=defaults["remote_url"])
    flash_parser.add_argument("--remote-host", default=defaults["remote_host"])
    flash_parser.add_argument("--remote-port", type=int, default=defaults["remote_port"])
    flash_parser.add_argument("--esptool-bin", default=defaults["esptool_bin"])
    flash_parser.add_argument("--no-build", action="store_true")

    mon = sub.add_parser("monitor", help="Open ESP-IDF monitor over RFC2217.")
    mon.add_argument("--remote-url", default=defaults["remote_url"])
    mon.add_argument("--remote-host", default=defaults["remote_host"])
    mon.add_argument("--remote-port", type=int, default=defaults["remote_port"])
    mon.add_argument("--baud", type=int, default=defaults["monitor_baud"])

    fm = sub.add_parser("flash-monitor", help="Build/flash, then open monitor over RFC2217.")
    fm.add_argument("--remote-url", default=defaults["remote_url"])
    fm.add_argument("--remote-host", default=defaults["remote_host"])
    fm.add_argument("--remote-port", type=int, default=defaults["remote_port"])
    fm.add_argument("--esptool-bin", default=defaults["esptool_bin"])
    fm.add_argument("--baud", type=int, default=defaults["monitor_baud"])
    fm.add_argument("--no-build", action="store_true")

    parser.set_defaults(
        idf_py_bin=defaults["idf_py_bin"],
        esp_idf_export_sh=defaults["esp_idf_export_sh"],
    )

    return parser.parse_args()


def main() -> int:
    args = parse_args()

    if args.command == "server":
        return start_server(args.python_exe, args.listen_port, args.com_port, args.force_restart)

    if args.command == "build":
        build(args.idf_py_bin, args.esp_idf_export_sh)
        return 0

    if args.command == "chip-id":
        chip_id(args.esptool_bin, args.remote_url, args.remote_host, args.remote_port)
        return 0

    if args.command == "flash":
        flash(
            args.esptool_bin,
            args.remote_url,
            args.remote_host,
            args.remote_port,
            build_first=not args.no_build,
            idf_py_bin=args.idf_py_bin,
            export_script=args.esp_idf_export_sh,
        )
        return 0

    if args.command == "monitor":
        monitor(
            args.remote_url,
            args.remote_host,
            args.remote_port,
            args.baud,
            args.idf_py_bin,
            args.esp_idf_export_sh,
        )
        return 0

    if args.command == "flash-monitor":
        flash_monitor(
            args.esptool_bin,
            args.remote_url,
            args.remote_host,
            args.remote_port,
            build_first=not args.no_build,
            baud=args.baud,
            idf_py_bin=args.idf_py_bin,
            export_script=args.esp_idf_export_sh,
        )
        return 0

    return 2


if __name__ == "__main__":
    raise SystemExit(main())
