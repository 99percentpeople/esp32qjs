"""Serial monitor and project-owned RFC2217 server lifecycle."""

from __future__ import annotations

import os
import platform
import re
import signal
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path

from .build import (
    TOOL_STATE_DIR,
    esptool_environment,
    has_repo_uv_tooling,
    idf_py_cmd,
    module_available,
    run,
    split_command,
    write_esptool_config,
)
from .profiles import ROOT_DIR, ProjectConfig, is_rfc2217_target, normalize_target


RFC2217_SERVER_PID_PATH = TOOL_STATE_DIR / "rfc2217-server.pid"


SETUP_CFG_TEXT = """[esp-idf-monitor]
custom_reset_sequence = R0|D0|W0.1|D1|R0|W0.1|R1|D0|R1|W0.1|D0|R0
custom_hard_reset_sequence = R1|W0.2|R0

[esptool]
custom_reset_sequence = R0|D0|W0.1|D1|R0|W0.1|R1|D0|R1|W0.1|D0|R0
custom_hard_reset_sequence = R1|W0.2|R0
"""


ANSI_ESCAPE_RE = re.compile(r"\x1b(?:\[[0-?]*[ -/]*[@-~]|[@-Z\\-_])")


class MarkerTimeoutError(RuntimeError):
    """Raised when expected serial output does not arrive before the timeout."""

    def __init__(self, description: str, output: str):
        super().__init__(description)
        self.description = description
        self.output = output


@dataclass
class MonitorSession:
    process: subprocess.Popen[bytes]
    master_fd: int


def resolve_server_cmd(python_exe: str) -> list[str]:
    """Resolve the RFC2217 server command, preferring uv-managed esptool."""
    if python_exe != "auto":
        return [*split_command(python_exe), "-m", "esp_rfc2217_server"]

    if has_repo_uv_tooling():
        return ["uv", "run", "python", "-m", "esp_rfc2217_server"]

    if module_available("esp_rfc2217_server"):
        return [sys.executable, "-m", "esp_rfc2217_server"]

    raise SystemExit(
        "esp_rfc2217_server is not available. Run `uv sync` in the repo root or set "
        "SERVER_PYTHON_EXE."
    )


def monitor_config_path() -> Path:
    """Return the project-local config path used by esp-idf-monitor."""
    return ROOT_DIR / "setup.cfg"


def write_monitor_config() -> Path:
    path = monitor_config_path()
    path.write_text(SETUP_CFG_TEXT, encoding="ascii")
    return path


def write_all_configs() -> tuple[Path, Path]:
    return write_esptool_config(), write_monitor_config()


def read_server_pid() -> int | None:
    """Return the project-owned RFC2217 PID, dropping invalid state."""
    try:
        pid = int(RFC2217_SERVER_PID_PATH.read_text(encoding="ascii").strip())
    except (FileNotFoundError, OSError, ValueError):
        RFC2217_SERVER_PID_PATH.unlink(missing_ok=True)
        return None
    if pid <= 0:
        RFC2217_SERVER_PID_PATH.unlink(missing_ok=True)
        return None
    return pid


def server_process_command(pid: int) -> str:
    """Return a PID's command line, or an empty string when it is gone."""
    if platform.system() == "Windows":
        result = subprocess.run(
            [
                "powershell",
                "-NoProfile",
                "-Command",
                (
                    f"Get-CimInstance Win32_Process -Filter \"ProcessId = {pid}\" | "
                    "Select-Object -ExpandProperty CommandLine"
                ),
            ],
            capture_output=True,
            text=True,
            check=False,
        )
        return result.stdout.strip()

    result = subprocess.run(
        ["ps", "-p", str(pid), "-o", "command="],
        capture_output=True,
        text=True,
        check=False,
    )
    return result.stdout.strip()


def list_server_processes() -> list[str]:
    """Return the running project-owned RFC2217 server, if any."""
    pid = read_server_pid()
    if pid is None:
        return []
    command = server_process_command(pid)
    if "esp_rfc2217_server" not in command:
        RFC2217_SERVER_PID_PATH.unlink(missing_ok=True)
        return []
    return [f"{pid}\t{command}"]


def stop_server_processes() -> None:
    """Stop only the RFC2217 server recorded in the project PID file."""
    pid = read_server_pid()
    if pid is None:
        return
    if "esp_rfc2217_server" not in server_process_command(pid):
        RFC2217_SERVER_PID_PATH.unlink(missing_ok=True)
        return
    if platform.system() == "Windows":
        subprocess.run(
            [
                "powershell",
                "-NoProfile",
                "-Command",
                f"Stop-Process -Id {pid} -Force",
            ],
            check=False,
        )
    else:
        try:
            os.kill(pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        except PermissionError as exc:
            raise SystemExit(
                f"Cannot stop project RFC2217 server PID {pid}: {exc}"
            ) from exc
    RFC2217_SERVER_PID_PATH.unlink(missing_ok=True)


def start_server(config: ProjectConfig, force_restart: bool) -> int:
    """Start esp_rfc2217_server after ensuring reset overrides exist."""
    esptool_cfg, setup_cfg = write_all_configs()
    local_target = serial_target(config, "server")

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

    cmd = [*resolve_server_cmd(config.server_python_exe), "-v", "-p", str(config.listen_port), local_target]
    kwargs: dict[str, object] = {
        "stdout": subprocess.DEVNULL,
        "stderr": subprocess.DEVNULL,
        "stdin": subprocess.DEVNULL,
        "start_new_session": True,
        "env": esptool_environment(esptool_cfg),
    }

    if platform.system() == "Windows":
        kwargs["creationflags"] = subprocess.CREATE_NO_WINDOW  # type: ignore[attr-defined]

    process = subprocess.Popen(cmd, **kwargs)
    RFC2217_SERVER_PID_PATH.parent.mkdir(parents=True, exist_ok=True)
    RFC2217_SERVER_PID_PATH.write_text(f"{process.pid}\n", encoding="ascii")
    time.sleep(1)

    existing = list_server_processes()
    if not existing:
        RFC2217_SERVER_PID_PATH.unlink(missing_ok=True)
        print("Failed to confirm esp_rfc2217_server startup.", file=sys.stderr)
        return 1

    print(f"RFC2217 server ready on {local_target} -> TCP {config.listen_port}")
    print(f"Using esptool config: {esptool_cfg}")
    print(f"Using monitor config: {setup_cfg}")
    for item in existing:
        print(item)
    return 0


def require_target(config: ProjectConfig) -> str:
    """Return the configured device target or raise a helpful error."""
    target = normalize_target(config.target)
    if not target:
        raise SystemExit("No device target configured. Set TARGET in .env or pass --target ...")
    return target


def serial_target(config: ProjectConfig, command_name: str) -> str:
    """Return the configured local serial target for commands that require one."""
    target = require_target(config)
    if is_rfc2217_target(target):
        raise SystemExit(f"{command_name} requires a local serial device target, got {target!r}.")
    return target


def command_port(config: ProjectConfig) -> str:
    """Resolve the esptool/monitor port for the configured target."""
    return require_target(config)


def monitor_cmd(config: ProjectConfig, *, no_reset: bool = False) -> list[str]:
    """Build the monitor command for the selected mcu profile."""
    write_monitor_config()
    project_args = ["-p", command_port(config), "-b", str(config.monitor_baud), "monitor"]
    if no_reset:
        project_args.append("--no-reset")
    return idf_py_cmd(project_args, config)


def monitor(config: ProjectConfig) -> None:
    """Open ESP-IDF monitor over the selected target."""
    run(
        monitor_cmd(config),
        cwd=ROOT_DIR,
        interactive=True,
        env=esptool_environment(),
    )


def normalize_serial_output(output: str) -> str:
    """Strip ANSI control sequences and carriage returns from serial captures."""
    return ANSI_ESCAPE_RE.sub("", output.replace("\r", ""))


def format_output_tail(output: str, max_lines: int = 20) -> str:
    """Render the trailing normalized serial output for error messages."""
    lines = normalize_serial_output(output).splitlines()
    if not lines:
        return "<no serial output>"
    return "\n".join(lines[-max_lines:])


def start_monitor_session(config: ProjectConfig) -> MonitorSession:
    """Start a resetting ESP-IDF monitor so USB devices boot after attachment."""
    master_fd, slave_fd = os.openpty()
    try:
        process = subprocess.Popen(
            monitor_cmd(config),
            cwd=ROOT_DIR,
            stdin=slave_fd,
            stdout=slave_fd,
            stderr=slave_fd,
            text=False,
            start_new_session=True,
            close_fds=True,
            env=esptool_environment(),
        )
    finally:
        os.close(slave_fd)

    os.set_blocking(master_fd, False)
    return MonitorSession(process=process, master_fd=master_fd)


def close_monitor_session(session: MonitorSession) -> None:
    """Close a PTY-backed monitor session and terminate the underlying process group if needed."""
    try:
        os.write(session.master_fd, b"\x1d")
    except OSError:
        pass

    try:
        session.process.wait(timeout=2)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(session.process.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        try:
            session.process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            try:
                os.killpg(session.process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            session.process.wait(timeout=2)
    finally:
        try:
            os.close(session.master_fd)
        except OSError:
            pass


def read_monitor_chunk(session: MonitorSession) -> bytes:
    """Read all currently available bytes from the PTY-backed monitor session."""
    chunks: list[bytes] = []

    while True:
        try:
            chunk = os.read(session.master_fd, 4096)
        except BlockingIOError:
            break
        except OSError:
            break

        if not chunk:
            break
        chunks.append(chunk)

    return b"".join(chunks)


def find_last_output_line_with_prefix(
    output: str,
    prefix: str,
    *,
    complete_only: bool = False,
) -> str | None:
    """Return the last normalized output line whose content starts with a marker prefix."""
    normalized = normalize_serial_output(output)
    lines = normalized.splitlines()
    if complete_only and normalized and not normalized.endswith("\n"):
        lines = lines[:-1]

    for line in reversed(lines):
        stripped = line.strip()
        if stripped.startswith(prefix):
            return stripped
    return None


def read_monitor_until_text(session: MonitorSession, marker: str, timeout_seconds: float, description: str) -> str:
    """Read monitor output until a normalized marker appears or timeout expires."""
    deadline = time.monotonic() + timeout_seconds
    output = ""

    while time.monotonic() < deadline:
        chunk = read_monitor_chunk(session)
        if chunk:
            output += chunk.decode("utf-8", errors="replace")
            normalized = normalize_serial_output(output)
            if marker in normalized:
                return normalized
            continue

        if session.process.poll() is not None:
            break

        time.sleep(0.05)

    raise MarkerTimeoutError(description, output)


def read_monitor_until_line_prefix(session: MonitorSession, prefix: str, timeout_seconds: float, description: str) -> str:
    """Read monitor output until a normalized output line starts with the given prefix."""
    deadline = time.monotonic() + timeout_seconds
    output = ""

    while time.monotonic() < deadline:
        chunk = read_monitor_chunk(session)
        if chunk:
            output += chunk.decode("utf-8", errors="replace")
            if find_last_output_line_with_prefix(output, prefix, complete_only=True) is not None:
                return normalize_serial_output(output)
            continue

        if session.process.poll() is not None:
            break

        time.sleep(0.05)

    raise MarkerTimeoutError(description, output)


def try_read_monitor_until_line_prefix(session: MonitorSession, prefix: str, timeout_seconds: float) -> str | None:
    """Best-effort variant of `read_monitor_until_line_prefix` used for optional readiness probes."""
    deadline = time.monotonic() + timeout_seconds
    output = ""

    while time.monotonic() < deadline:
        chunk = read_monitor_chunk(session)
        if chunk:
            output += chunk.decode("utf-8", errors="replace")
            normalized = normalize_serial_output(output)
            if find_last_output_line_with_prefix(normalized, prefix, complete_only=True) is not None:
                return normalized
            continue

        if session.process.poll() is not None:
            break

        time.sleep(0.05)

    return None
