#!/usr/bin/env python3
"""Unified helper for remote ESP32 board development.

The script reads repository-local `/.env` defaults, then merges them with a
selected board profile from `configs/boards/*/.env`. That keeps day-to-day
commands short while still allowing different targets, build directories,
sdkconfig files, and remote serial bridges per board.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import os
import platform
import shlex
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from urllib.parse import parse_qsl, quote, urlsplit, urlunsplit


ROOT_DIR = Path(__file__).resolve().parent.parent
ENV_PATH = ROOT_DIR / ".env"
CONFIG_DIR = ROOT_DIR / "configs"
BOARD_DIR = CONFIG_DIR / "boards"

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


@dataclass(frozen=True)
class BoardProfile:
    reference: str
    name: str
    file: Path
    label: str
    idf_target: str
    build_dir: Path
    sdkconfig_defaults: Path | None
    idf_path: str
    remote_host: str
    remote_port: int
    remote_url: str
    monitor_baud: int
    com_port: str
    listen_port: int
    server_python_exe: str
    esptool_bin: str


@dataclass(frozen=True)
class ProjectConfig:
    board: str
    board_file: Path
    board_label: str
    build_dir: Path
    generated_sdkconfig: Path
    sdkconfig_defaults: Path | None
    idf_target: str
    idf_path: str
    remote_host: str
    remote_port: int
    remote_url: str
    monitor_baud: int
    com_port: str
    listen_port: int
    server_python_exe: str
    esptool_bin: str
    assume_prompt: str


def load_dotenv(path: Path) -> dict[str, str]:
    """Load simple `KEY=VALUE` pairs from a local `.env`-style file."""
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


def merged_value(
    repo_env: dict[str, str],
    board_env: dict[str, str],
    key: str,
    default: str,
) -> str:
    """Resolve a config value from process env, then `/.env`, then board profile."""
    return os.environ.get(key, repo_env.get(key, board_env.get(key, default)))


def merged_int(
    repo_env: dict[str, str],
    board_env: dict[str, str],
    key: str,
    default: int,
) -> int:
    """Resolve and validate an integer config value."""
    raw_value = os.environ.get(key, repo_env.get(key, board_env.get(key)))
    if raw_value is None:
        return default

    try:
        return int(raw_value)
    except ValueError as exc:
        raise SystemExit(f"Invalid integer for {key}: {raw_value!r}") from exc


def resolve_repo_path(raw_path: str) -> Path:
    """Resolve a project-relative path against the repository root."""
    path = Path(raw_path).expanduser()
    if path.is_absolute():
        return path
    return ROOT_DIR / path


def format_path(path: Path | None) -> str:
    """Render a path relative to the repository root when possible."""
    if path is None:
        return ""
    try:
        return str(path.relative_to(ROOT_DIR))
    except ValueError:
        return str(path)


def board_reference_from_env(repo_env: dict[str, str], override: str | None) -> str:
    """Choose the board profile reference from CLI or local environment."""
    if override:
        return override
    if os.environ.get("BOARD_FILE"):
        return os.environ["BOARD_FILE"]
    if repo_env.get("BOARD_FILE"):
        return repo_env["BOARD_FILE"]
    if os.environ.get("BOARD"):
        return os.environ["BOARD"]
    if repo_env.get("BOARD"):
        return repo_env["BOARD"]
    return "xiao_esp32s3"


def board_env_file(board_dir: Path) -> Path:
    """Return the profile `.env` file for a board directory."""
    return board_dir / ".env"


def board_sdkconfig_defaults_file(board_dir: Path) -> Path:
    """Return the default sdkconfig defaults file for a board directory."""
    return board_dir / "sdkconfig.defaults"


def resolve_profile_path(raw_path: str, board_dir: Path) -> Path:
    """Resolve a board-relative or repo-relative path."""
    path = Path(raw_path).expanduser()
    if path.is_absolute():
        return path
    if raw_path.startswith("./") or raw_path.startswith("../") or raw_path.startswith("configs/"):
        return ROOT_DIR / path
    return board_dir / path


def resolve_board_file(reference: str) -> Path:
    """Resolve a board profile reference to an on-disk `.env` file."""
    ref_path = Path(reference).expanduser()
    if ref_path.is_absolute() or "/" in reference or "\\" in reference or reference.endswith(".env"):
        candidate = ref_path if ref_path.is_absolute() else ROOT_DIR / ref_path
    else:
        candidate = BOARD_DIR / reference

    if candidate.is_dir():
        board_file = board_env_file(candidate)
    else:
        board_file = candidate

    if not board_file.exists():
        raise SystemExit(
            f"Board profile {reference!r} not found. Add {board_file} or run "
            "`python scripts/remote.py boards` to list available profiles."
        )

    return board_file.resolve()


def load_profile(board_override: str | None = None) -> BoardProfile:
    """Build the effective board profile from `/.env` and `configs/boards/*/.env`."""
    repo_env = load_dotenv(ENV_PATH)
    board_reference = board_reference_from_env(repo_env, board_override)
    board_file = resolve_board_file(board_reference)
    board_dir = board_file.parent
    board_env = load_dotenv(board_file)

    idf_path = merged_value(
        repo_env,
        board_env,
        "IDF_PATH",
        merged_value(repo_env, board_env, "IDF_PATH", str(Path.home() / "esp" / "esp-idf")),
    )
    remote_port = merged_int(repo_env, board_env, "REMOTE_PORT", 2217)
    remote_url_override = (
        os.environ.get("REMOTE_URL")
        or repo_env.get("REMOTE_URL")
        or board_env.get("REMOTE_URL")
        or ""
    )
    sdkconfig_defaults_raw = merged_value(repo_env, board_env, "SDKCONFIG_DEFAULTS", "")
    if not sdkconfig_defaults_raw:
        sdkconfig_defaults_raw = merged_value(repo_env, board_env, "SDKCONFIG", "")

    idf_target = merged_value(repo_env, board_env, "IDF_TARGET", "")
    if not idf_target:
        raise SystemExit(f"{board_file} must define IDF_TARGET.")

    return BoardProfile(
        reference=board_reference,
        name=board_dir.name,
        file=board_file,
        label=merged_value(repo_env, board_env, "BOARD_LABEL", board_dir.name),
        idf_target=idf_target,
        build_dir=resolve_repo_path(merged_value(repo_env, board_env, "BUILD_DIR", "build")),
        sdkconfig_defaults=(
            resolve_profile_path(sdkconfig_defaults_raw, board_dir)
            if sdkconfig_defaults_raw
            else (board_sdkconfig_defaults_file(board_dir) if board_sdkconfig_defaults_file(board_dir).exists() else None)
        ),
        idf_path=str(Path(idf_path).expanduser()),
        remote_host=merged_value(repo_env, board_env, "REMOTE_HOST", "127.0.0.1"),
        remote_port=remote_port,
        remote_url=remote_url_override,
        monitor_baud=merged_int(
            repo_env,
            board_env,
            "MONITOR_BAUD",
            merged_int(repo_env, board_env, "ESPBAUD", 115200),
        ),
        com_port=merged_value(repo_env, board_env, "COM_PORT", "COM3"),
        listen_port=merged_int(repo_env, board_env, "LISTEN_PORT", remote_port),
        server_python_exe=merged_value(repo_env, board_env, "SERVER_PYTHON_EXE", "auto"),
        esptool_bin=merged_value(repo_env, board_env, "ESPTOOL_BIN", "auto"),
    )


def available_board_profiles() -> list[Path]:
    """Return the board profiles shipped in the repository."""
    if not BOARD_DIR.exists():
        return []
    return sorted(path for path in BOARD_DIR.glob("*/.env") if path.is_file())


def run(
    cmd: list[str],
    cwd: Path | None = None,
    check: bool = True,
    interactive: bool = False,
) -> subprocess.CompletedProcess[str]:
    if interactive:
        return subprocess.run(cmd, cwd=cwd, check=check, text=True)
    return subprocess.run(cmd, cwd=cwd, check=check, text=True, capture_output=False)


def run_streaming(cmd: list[str], cwd: Path | None = None) -> tuple[int, str]:
    """Run a command while streaming combined stdout/stderr and capturing it."""
    process = subprocess.Popen(
        cmd,
        cwd=cwd,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        bufsize=1,
    )
    assert process.stdout is not None

    output_chunks: list[str] = []
    for chunk in process.stdout:
        print(chunk, end="")
        output_chunks.append(chunk)

    return process.wait(), "".join(output_chunks)


def which(name: str) -> str | None:
    from shutil import which as _which

    return _which(name)


def split_command(command: str) -> list[str]:
    """Split a configured command string into subprocess arguments."""
    parts = shlex.split(command)
    if not parts:
        raise SystemExit("Configured command is empty.")
    return parts


def has_repo_uv_tooling() -> bool:
    """Report whether the repository has a uv-managed Python tool environment."""
    return which("uv") is not None and (ROOT_DIR / "pyproject.toml").exists()


def module_available(module_name: str) -> bool:
    """Check whether a Python module is importable in the current interpreter."""
    return importlib.util.find_spec(module_name) is not None


def resolve_esptool_cmd(esptool_bin: str) -> list[str]:
    """Resolve the esptool command, preferring the repository-local uv environment."""
    if esptool_bin != "auto":
        return split_command(esptool_bin)

    if has_repo_uv_tooling():
        return ["uv", "run", "python", "-m", "esptool"]

    if which("esptool"):
        return ["esptool"]

    if module_available("esptool"):
        return [sys.executable, "-m", "esptool"]

    raise SystemExit(
        "esptool is not available. Run `uv sync` in the repo root or set ESPTOOL_BIN."
    )


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


def start_server(config: ProjectConfig, force_restart: bool) -> int:
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

    cmd = [*resolve_server_cmd(config.server_python_exe), "-v", "-p", str(config.listen_port), config.com_port]
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

    print(f"RFC2217 server ready on {config.com_port} -> TCP {config.listen_port}")
    print(f"Using esptool config: {esptool_cfg}")
    print(f"Using monitor config: {setup_cfg}")
    for item in existing:
        print(item)
    return 0


def idf_py_cmd(project_args: list[str], config: ProjectConfig) -> list[str]:
    """Return a command that can run `idf.py` for the selected board profile."""
    esp_idf_root = Path(config.idf_path).expanduser()
    idf_py_script = esp_idf_root / "tools" / "idf.py"
    export_script_path = esp_idf_root / "export.sh"
    idf_env_ready = bool(os.environ.get("IDF_PATH") and os.environ.get("ESP_IDF_VERSION"))
    cmake_args = [
        "-B",
        str(config.build_dir),
        f"-DIDF_TARGET={config.idf_target}",
        f"-DSDKCONFIG={config.generated_sdkconfig}",
    ]

    if config.sdkconfig_defaults is not None:
        cmake_args.append(f"-DSDKCONFIG_DEFAULTS={config.sdkconfig_defaults}")

    full_args = [*cmake_args, *project_args]

    if not idf_py_script.exists():
        raise SystemExit(
            f"idf.py not found under {esp_idf_root}. Set idf_path to the ESP-IDF install directory."
        )

    if platform.system() != "Windows" and export_script_path.exists() and not idf_env_ready:
        joined = shlex.join(full_args)
        command = (
            f"source {shlex.quote(str(export_script_path))} >/dev/null 2>&1 "
            f"&& exec {shlex.quote(str(idf_py_script))} {joined}"
        )
        return ["bash", "-lc", command]

    if idf_env_ready:
        return [str(idf_py_script), *full_args]

    if platform.system() != "Windows" and export_script_path.exists():
        joined = shlex.join(full_args)
        command = (
            f"source {shlex.quote(str(export_script_path))} >/dev/null 2>&1 "
            f"&& exec {shlex.quote(str(idf_py_script))} {joined}"
        )
        return ["bash", "-lc", command]

    if which("idf.py"):
        return ["idf.py", *full_args]

    raise SystemExit("ESP-IDF tooling is not ready. Set idf_path to the ESP-IDF install directory.")


STALE_BUILD_ERROR_PATTERNS = (
    "Does not match the generator used previously",
    "Either remove the CMakeCache.txt file and CMakeFiles directory",
    "does not match the source used to generate cache",
    "current CMakeCache.txt directory",
    "The build directory is configured for",
    "but the project requires",
    "specified on command line is not consistent with target",
    "in CMakeCache.txt. Run 'idf.py set-target",
)


def is_stale_build_dir_failure(output: str) -> bool:
    """Return whether command output looks like a stale build-directory/cache issue."""
    normalized = output.replace("\r", "")
    return any(pattern in normalized for pattern in STALE_BUILD_ERROR_PATTERNS)


def confirm_action(prompt: str, assume_prompt: str) -> bool:
    """Resolve a yes/no prompt interactively or from a non-interactive default."""
    if assume_prompt == "y":
        print(f"{prompt} y (from --assume y)")
        return True
    if assume_prompt == "n":
        print(f"{prompt} n (from --assume n)")
        return False
    try:
        answer = input(f"{prompt} [y/N]: ")
    except EOFError:
        return False
    return answer.strip().lower() in {"y", "yes"}


def confirm_build_dir_reset(build_dir: Path, assume_prompt: str) -> bool:
    """Ask whether the stale build directory may be deleted and rebuilt."""
    return confirm_action(
        f"Build directory {build_dir} looks stale. Delete it and retry?",
        assume_prompt,
    )


def safe_remove_build_dir(build_dir: Path) -> None:
    """Remove a generated build directory after explicit confirmation."""
    resolved_build_dir = build_dir.resolve()
    resolved_root = ROOT_DIR.resolve()
    if resolved_build_dir == resolved_root or resolved_root not in resolved_build_dir.parents:
        raise SystemExit(f"Refusing to delete unsafe build directory: {build_dir}")
    shutil.rmtree(resolved_build_dir)


def run_idf_action_with_stale_build_recovery(project_args: list[str], config: ProjectConfig) -> None:
    """Run an idf.py action, offering one confirmed clean rebuild on stale-cache failures."""
    refresh_generated_sdkconfig(config)
    existing_build_dir = config.build_dir.exists()
    cmd = idf_py_cmd(project_args, config)
    return_code, output = run_streaming(cmd, cwd=ROOT_DIR)
    if return_code == 0:
        return

    if existing_build_dir and is_stale_build_dir_failure(output):
        if not confirm_build_dir_reset(config.build_dir, config.assume_prompt):
            raise SystemExit(return_code)
        safe_remove_build_dir(config.build_dir)
        print(f"Retrying with a clean build directory: {config.build_dir}")
        return_code, _ = run_streaming(idf_py_cmd(project_args, config), cwd=ROOT_DIR)
        if return_code == 0:
            return

    raise SystemExit(return_code)


def build(config: ProjectConfig) -> None:
    """Build firmware for the selected board."""
    run_idf_action_with_stale_build_recovery(["build"], config)


def build_fs_image(config: ProjectConfig) -> None:
    """Build only the LittleFS image used by the storage partition."""
    run_idf_action_with_stale_build_recovery(["littlefs_storage_bin"], config)


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
    """Normalize legacy RFC2217 URLs so old values keep working."""
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
    """Return the RFC2217 URL, preferring an explicit override from config."""
    return normalize_remote_url(remote_url_override or default_remote_url(host, port))


def chip_id(config: ProjectConfig) -> None:
    """Probe the remote device without writing flash."""
    write_esptool_config()
    subprocess.run(
        [*resolve_esptool_cmd(config.esptool_bin), "--port", remote_url(config.remote_url, config.remote_host, config.remote_port), "chip-id"],
        cwd=ROOT_DIR,
        check=True,
    )


def load_flasher_args(build_dir: Path) -> dict[str, object]:
    """Load the current ESP-IDF flash plan generated in flasher_args.json."""
    flasher_args_path = build_dir / "flasher_args.json"

    if not flasher_args_path.exists():
        raise SystemExit(f"{flasher_args_path} not found. Run `python scripts/remote.py build` first.")

    return json.loads(flasher_args_path.read_text(encoding="utf-8"))


def resolve_flash_entry(
    flasher_args: dict[str, object],
    build_dir: Path,
    entry_name: str,
) -> tuple[str, Path]:
    """Resolve one named flash entry from flasher_args.json."""
    entry = flasher_args.get(entry_name)
    if not isinstance(entry, dict):
        raise SystemExit(f"Flash entry {entry_name!r} not found in {build_dir / 'flasher_args.json'}.")

    offset = str(entry.get("offset", ""))
    file_name = str(entry.get("file", ""))
    if not offset or not file_name:
        raise SystemExit(f"Flash entry {entry_name!r} is missing offset/file metadata.")

    file_path = Path(file_name)
    if not file_path.is_absolute():
        file_path = build_dir / file_path

    if not file_path.exists():
        raise SystemExit(
            f"{file_path} not found. Run `python scripts/remote.py build` or "
            "`python scripts/remote.py build-fs` first."
        )

    return offset, file_path


def write_flash(config: ProjectConfig, write_flash_args: list[str], flash_pairs: list[str], chip: str, before: str, after: str) -> None:
    """Run esptool write-flash with one or more offset/file pairs."""
    subprocess.run(
        [
            *resolve_esptool_cmd(config.esptool_bin),
            "--chip",
            chip,
            "--before",
            before,
            "--after",
            after,
            "--port",
            remote_url(config.remote_url, config.remote_host, config.remote_port),
            "write-flash",
            *write_flash_args,
            *flash_pairs,
        ],
        cwd=ROOT_DIR,
        check=True,
    )


def flash(config: ProjectConfig, build_first: bool) -> None:
    """Flash all ESP-IDF build outputs listed in flasher_args.json over RFC2217."""
    write_esptool_config()

    if build_first:
        build(config)

    flasher_args = load_flasher_args(config.build_dir)
    extra_esptool_args = dict(flasher_args.get("extra_esptool_args", {}))
    write_flash_args = [str(arg) for arg in flasher_args.get("write_flash_args", [])]
    flash_files = dict(flasher_args.get("flash_files", {}))
    flash_pairs: list[str] = []

    for offset, file_name in sorted(flash_files.items(), key=lambda item: int(item[0], 0)):
        file_path = Path(file_name)
        if not file_path.is_absolute():
            file_path = config.build_dir / file_path
        flash_pairs.extend([offset, str(file_path)])

    write_flash(
        config,
        write_flash_args,
        flash_pairs,
        chip=str(extra_esptool_args.get("chip", config.idf_target)),
        before=str(extra_esptool_args.get("before", "default-reset")),
        after=str(extra_esptool_args.get("after", "hard-reset")),
    )


def flash_fs(config: ProjectConfig, build_first: bool) -> None:
    """Build and flash only the LittleFS storage partition."""
    write_esptool_config()

    if build_first:
        build_fs_image(config)

    flasher_args = load_flasher_args(config.build_dir)
    extra_esptool_args = dict(flasher_args.get("extra_esptool_args", {}))
    write_flash_args = [str(arg) for arg in flasher_args.get("write_flash_args", [])]
    offset, file_path = resolve_flash_entry(flasher_args, config.build_dir, "storage")

    write_flash(
        config,
        write_flash_args,
        [offset, str(file_path)],
        chip=str(extra_esptool_args.get("chip", config.idf_target)),
        before=str(extra_esptool_args.get("before", "default-reset")),
        after=str(extra_esptool_args.get("after", "hard-reset")),
    )


def monitor(config: ProjectConfig) -> None:
    """Open ESP-IDF monitor over RFC2217."""
    write_monitor_config()
    run(
        idf_py_cmd(
            ["-p", remote_url(config.remote_url, config.remote_host, config.remote_port), "-b", str(config.monitor_baud), "monitor"],
            config,
        ),
        cwd=ROOT_DIR,
        interactive=True,
    )


def flash_monitor(config: ProjectConfig, build_first: bool) -> None:
    """Build/flash, then open monitor over RFC2217."""
    flash(config, build_first=build_first)
    monitor(config)


def config_default_inputs(config: ProjectConfig) -> list[Path]:
    """Return the defaults files that define the selected board configuration."""
    defaults: list[Path] = []
    if config.sdkconfig_defaults is not None:
        defaults.append(config.sdkconfig_defaults)
    return defaults


def refresh_generated_sdkconfig(config: ProjectConfig) -> None:
    """Regenerate the build-local sdkconfig when tracked defaults changed."""
    if not config.generated_sdkconfig.exists():
        return

    generated_mtime = config.generated_sdkconfig.stat().st_mtime
    defaults = [path for path in config_default_inputs(config) if path.exists()]
    if not defaults:
        return

    if not any(path.stat().st_mtime > generated_mtime for path in defaults):
        return

    print(f"Refreshing {format_path(config.generated_sdkconfig)} because board defaults changed.")
    config.generated_sdkconfig.unlink(missing_ok=True)
    old_sdkconfig = config.generated_sdkconfig.with_name(f"{config.generated_sdkconfig.name}.old")
    old_sdkconfig.unlink(missing_ok=True)


def list_boards(selected_board: str) -> None:
    """Print the board profiles bundled with the repository."""
    profiles = available_board_profiles()
    if not profiles:
        print("No board profiles found.")
        return

    selected = resolve_board_file(selected_board)
    for board_file in profiles:
        board_env = load_dotenv(board_file)
        marker = "*" if board_file.resolve() == selected else " "
        label = board_env.get("BOARD_LABEL", board_file.parent.name)
        target = board_env.get("IDF_TARGET", "?")
        build_dir = board_env.get("BUILD_DIR", "build")
        sdkconfig_defaults = board_env.get("SDKCONFIG_DEFAULTS", board_env.get("SDKCONFIG", ""))
        if not sdkconfig_defaults:
            sdkconfig_defaults = format_path(board_sdkconfig_defaults_file(board_file.parent))
        detail = f"{target}, {build_dir}"
        if sdkconfig_defaults:
            detail = f"{detail}, {sdkconfig_defaults}"
        print(f"{marker} {board_file.parent.name}: {label} [{detail}]")


def show_config(config: ProjectConfig) -> None:
    """Print the effective merged configuration for the selected board."""
    print(f"board={config.board}")
    print(f"board_file={config.board_file}")
    print(f"board_label={config.board_label}")
    print(f"idf_target={config.idf_target}")
    print(f"build_dir={format_path(config.build_dir)}")
    print(f"generated_sdkconfig={format_path(config.generated_sdkconfig)}")
    print(f"sdkconfig_defaults={format_path(config.sdkconfig_defaults)}")
    print(f"idf_path={config.idf_path}")
    print(f"remote_host={config.remote_host}")
    print(f"remote_port={config.remote_port}")
    print(f"remote_url={remote_url(config.remote_url, config.remote_host, config.remote_port)}")
    print(f"monitor_baud={config.monitor_baud}")
    print(f"com_port={config.com_port}")
    print(f"listen_port={config.listen_port}")
    print(f"server_python_exe={config.server_python_exe}")
    print(f"esptool_bin={config.esptool_bin}")
    print(f"assume_prompt={config.assume_prompt}")


def build_project_config(args: argparse.Namespace, profile: BoardProfile) -> ProjectConfig:
    """Convert parsed CLI args to the effective project config."""
    sdkconfig_defaults = (
        Path(args.sdkconfig_defaults).expanduser()
        if getattr(args, "sdkconfig_defaults", "")
        else None
    )
    if sdkconfig_defaults is not None and not sdkconfig_defaults.is_absolute():
        sdkconfig_defaults = ROOT_DIR / sdkconfig_defaults

    build_dir = Path(args.build_dir).expanduser()
    if not build_dir.is_absolute():
        build_dir = ROOT_DIR / build_dir

    board_file = Path(args.board_file)
    return ProjectConfig(
        board=profile.name,
        board_file=board_file,
        board_label=profile.label,
        build_dir=build_dir,
        generated_sdkconfig=build_dir / "sdkconfig",
        sdkconfig_defaults=sdkconfig_defaults,
        idf_target=args.idf_target,
        idf_path=args.idf_path,
        remote_host=getattr(args, "remote_host", profile.remote_host),
        remote_port=getattr(args, "remote_port", profile.remote_port),
        remote_url=getattr(args, "remote_url", profile.remote_url),
        monitor_baud=getattr(args, "baud", profile.monitor_baud),
        com_port=getattr(args, "com_port", profile.com_port),
        listen_port=getattr(args, "listen_port", profile.listen_port),
        server_python_exe=getattr(args, "python_exe", profile.server_python_exe),
        esptool_bin=getattr(args, "esptool_bin", profile.esptool_bin),
        assume_prompt=args.assume,
    )


def parse_args(argv: list[str] | None = None) -> tuple[argparse.Namespace, BoardProfile]:
    """Parse CLI arguments after resolving the selected board profile."""
    raw_argv = sys.argv[1:] if argv is None else argv
    bootstrap = argparse.ArgumentParser(add_help=False)
    bootstrap.add_argument("--board")
    pre_args, _ = bootstrap.parse_known_args(raw_argv)
    profile = load_profile(pre_args.board)

    parser = argparse.ArgumentParser(description="Unified helper for remote ESP32 board development.")
    parser.add_argument(
        "--board",
        default=profile.reference,
        help="Board profile name from configs/boards/*/.env, or a direct path to a board directory/profile file.",
    )
    parser.add_argument("--build-dir", default=format_path(profile.build_dir))
    parser.add_argument("--idf-target", default=profile.idf_target)
    parser.add_argument("--sdkconfig-defaults", default=format_path(profile.sdkconfig_defaults))
    parser.add_argument("--idf-path", default=profile.idf_path)
    parser.add_argument(
        "--assume",
        choices=("ask", "y", "n"),
        default="ask",
        help="Skip manual prompts by answering yes/no automatically. Default: ask.",
    )
    parser.set_defaults(board_file=str(profile.file))

    sub = parser.add_subparsers(dest="command", required=True)

    boards_parser = sub.add_parser("boards", help="List bundled board profiles.")
    boards_parser.set_defaults(_noop=True)

    show_parser = sub.add_parser("show-config", help="Print the merged board/tool configuration.")
    show_parser.set_defaults(_noop=True)

    server = sub.add_parser("server", help="Write config and start esp_rfc2217_server.")
    server.add_argument("--com-port", default=profile.com_port)
    server.add_argument("--listen-port", type=int, default=profile.listen_port)
    server.add_argument("--python-exe", default=profile.server_python_exe)
    server.add_argument("--force-restart", action="store_true")

    build_parser = sub.add_parser("build", help="Run idf.py build for the selected board.")
    build_parser.set_defaults(_noop=True)

    build_fs_parser = sub.add_parser("build-fs", help="Build only the LittleFS storage image.")
    build_fs_parser.set_defaults(_noop=True)

    chip = sub.add_parser("chip-id", help="Check remote RFC2217 connectivity.")
    chip.add_argument("--remote-url", default=profile.remote_url)
    chip.add_argument("--remote-host", default=profile.remote_host)
    chip.add_argument("--remote-port", type=int, default=profile.remote_port)
    chip.add_argument("--esptool-bin", default=profile.esptool_bin)

    flash_parser = sub.add_parser("flash", help="Build and flash over RFC2217.")
    flash_parser.add_argument("--remote-url", default=profile.remote_url)
    flash_parser.add_argument("--remote-host", default=profile.remote_host)
    flash_parser.add_argument("--remote-port", type=int, default=profile.remote_port)
    flash_parser.add_argument("--esptool-bin", default=profile.esptool_bin)
    flash_parser.add_argument("--no-build", action="store_true")

    flash_fs_parser = sub.add_parser("flash-fs", help="Build and flash only the LittleFS storage partition.")
    flash_fs_parser.add_argument("--remote-url", default=profile.remote_url)
    flash_fs_parser.add_argument("--remote-host", default=profile.remote_host)
    flash_fs_parser.add_argument("--remote-port", type=int, default=profile.remote_port)
    flash_fs_parser.add_argument("--esptool-bin", default=profile.esptool_bin)
    flash_fs_parser.add_argument("--no-build", action="store_true")

    mon = sub.add_parser("monitor", help="Open ESP-IDF monitor over RFC2217.")
    mon.add_argument("--remote-url", default=profile.remote_url)
    mon.add_argument("--remote-host", default=profile.remote_host)
    mon.add_argument("--remote-port", type=int, default=profile.remote_port)
    mon.add_argument("--baud", type=int, default=profile.monitor_baud)

    fm = sub.add_parser("flash-monitor", help="Build/flash, then open monitor over RFC2217.")
    fm.add_argument("--remote-url", default=profile.remote_url)
    fm.add_argument("--remote-host", default=profile.remote_host)
    fm.add_argument("--remote-port", type=int, default=profile.remote_port)
    fm.add_argument("--esptool-bin", default=profile.esptool_bin)
    fm.add_argument("--baud", type=int, default=profile.monitor_baud)
    fm.add_argument("--no-build", action="store_true")

    return parser.parse_args(raw_argv), profile


def main() -> int:
    args, profile = parse_args()
    config = build_project_config(args, profile)

    if args.command == "boards":
        list_boards(args.board)
        return 0

    if args.command == "show-config":
        show_config(config)
        return 0

    if args.command == "server":
        return start_server(config, args.force_restart)

    if args.command == "build":
        build(config)
        return 0

    if args.command == "build-fs":
        build_fs_image(config)
        return 0

    if args.command == "chip-id":
        chip_id(config)
        return 0

    if args.command == "flash":
        flash(config, build_first=not args.no_build)
        return 0

    if args.command == "flash-fs":
        flash_fs(config, build_first=not args.no_build)
        return 0

    if args.command == "monitor":
        monitor(config)
        return 0

    if args.command == "flash-monitor":
        flash_monitor(config, build_first=not args.no_build)
        return 0

    return 2


if __name__ == "__main__":
    raise SystemExit(main())
