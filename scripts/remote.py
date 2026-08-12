#!/usr/bin/env python3
"""Unified helper for local and remote ESP32QJS application development.

The script combines a board profile from `configs/boards/*/.env` with a
bundled or directly referenced application profile. That keeps hardware
configuration independent from application behavior, partitions, and
LittleFS resources.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import os
import platform
import re
import shlex
import shutil
import signal
import subprocess
import sys
import time
from dataclasses import dataclass, field, replace
from pathlib import Path
from urllib.parse import parse_qsl, quote, urlsplit, urlunsplit


ROOT_DIR = Path(__file__).resolve().parent.parent
ENV_PATH = ROOT_DIR / ".env"
CONFIG_DIR = ROOT_DIR / "configs"
BOARD_DIR = CONFIG_DIR / "boards"
APP_DIR = ROOT_DIR / "apps"
SHARED_FLASH_DATA_DIR = ROOT_DIR / "shared" / "flash_data"

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

HOST_TEST_BUILD_DIR = ROOT_DIR / "build-host-tests"
JS_TEST_DIR = ROOT_DIR / "tests" / "js"
JS_TEST_FLASH_DATA_DIR = JS_TEST_DIR / "flash_data"
JS_TEST_SDKCONFIG_DEFAULTS = JS_TEST_DIR / "sdkconfig.defaults"
JS_SYNTAX_CHECK_SCRIPT = ROOT_DIR / "scripts" / "check_js_syntax.py"
JS_TEST_READY_MARKER = "__ESP32QJS_TEST_READY__"
JS_TEST_FEATURES_PREFIX = "__ESP32QJS_TEST_FEATURES__:"
JS_TEST_PASS_PREFIX = "__TEST_PASS__:"
JS_TEST_SKIP_PREFIX = "__TEST_SKIP__:"
JS_TEST_FAIL_PREFIX = "__TEST_FAIL__:"
JS_REPL_BANNER_MARKER = "Run help() for usage."
MONITOR_READY_MARKER = "--- Quit:"
ANSI_ESCAPE_RE = re.compile(r"\x1b(?:\[[0-?]*[ -/]*[@-~]|[@-Z\\-_])")
CTEST_SUMMARY_RE = re.compile(r"(?m)^(\d+)% tests passed, (\d+) tests failed out of (\d+)$")
APP_ID_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.-]{0,63}$")
TEST_SCOPE_ORDER = ("c", "js")


@dataclass(frozen=True)
class JsTestCase:
    path: str
    required_capabilities: tuple[str, ...] = ()
    timeout_seconds: float = 15.0
    reset_before: bool = False
    reset_after: bool = False


@dataclass(frozen=True)
class JsTestModule:
    name: str
    cases: tuple[JsTestCase, ...]
    required_features: tuple[str, ...] = ()


JS_TEST_MODULES = (
    JsTestModule("core", (JsTestCase("modules/core/eval.js"),)),
    JsTestModule("sys", (JsTestCase("modules/sys/runtime.js"),)),
    JsTestModule("gpio", (JsTestCase("modules/gpio/basic.js"),), required_features=("gpio",)),
    JsTestModule("ledc", (JsTestCase("modules/ledc/basic.js"),), required_features=("ledc",)),
    JsTestModule("adc", (JsTestCase("modules/adc/basic.js"),), required_features=("adc",)),
    JsTestModule("dac", (JsTestCase("modules/dac/basic.js"),), required_features=("dac",)),
    JsTestModule("i2c", (JsTestCase("modules/i2c/status.js"),), required_features=("i2c",)),
    JsTestModule(
        "spi",
        (
            JsTestCase("modules/spi/basic.js"),
            JsTestCase("modules/spi/loopback.js", required_capabilities=("loopback",)),
        ),
        required_features=("spi",),
    ),
    JsTestModule(
        "uart",
        (
            JsTestCase("modules/uart/basic.js"),
            JsTestCase("modules/uart/loopback.js", required_capabilities=("loopback",)),
        ),
        required_features=("uart",),
    ),
    JsTestModule("timers", (JsTestCase("modules/timers/runtime.js"),)),
    JsTestModule("fs", (JsTestCase("modules/fs/filesystem.js"),), required_features=("fs",)),
    JsTestModule("nvs", (JsTestCase("modules/nvs/basic.js"),), required_features=("nvs",)),
    JsTestModule("stream", (JsTestCase("modules/stream/stream.js"),)),
    JsTestModule("load", (JsTestCase("modules/load/load.js"),), required_features=("fs",)),
    JsTestModule(
        "displayBuffer",
        (
            JsTestCase("modules/display_buffer/basic.js", timeout_seconds=60.0),
            JsTestCase("modules/display_buffer/font.js"),
        ),
        required_features=("displayBuffer",),
    ),
    JsTestModule(
        "display",
        (
            JsTestCase(
                "modules/display/lifecycle.js",
                timeout_seconds=90.0,
                reset_before=True,
            ),
        ),
        required_features=("displayBuffer",),
    ),
    JsTestModule(
        "wifi",
        (
            JsTestCase("modules/wifi/offline.js"),
            JsTestCase("modules/wifi/network.js", required_capabilities=("network",), timeout_seconds=25.0),
        ),
        required_features=("wifi",),
    ),
    JsTestModule(
        "http",
        (
            JsTestCase("modules/http/offline.js"),
            JsTestCase("modules/http/network.js", required_capabilities=("network",), timeout_seconds=40.0),
        ),
        required_features=("http",),
    ),
    JsTestModule(
        "http_server",
        (JsTestCase("modules/http_server/offline.js"),),
        required_features=("httpServer",),
    ),
    JsTestModule(
        "socket",
        (JsTestCase("modules/socket/offline.js"),),
        required_features=("socket",),
    ),
    JsTestModule(
        "websocket",
        (
            JsTestCase("modules/websocket/offline.js"),
            JsTestCase("modules/websocket/network.js", required_capabilities=("network",), timeout_seconds=30.0),
        ),
        required_features=("websocket",),
    ),
    JsTestModule(
        "future",
        (JsTestCase("modules/timers/capacity.js", reset_before=True, reset_after=True),),
    ),
)
JS_TEST_MODULE_MAP = {module.name: module for module in JS_TEST_MODULES}
JS_TEST_CAPABILITY_FLAGS = {
    "network": "--network",
    "loopback": "--loopback",
}


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


@dataclass
class TestStageSummary:
    name: str
    status: str = "passed"
    passed_cases: int = 0
    skipped_cases: int = 0
    failed_cases: int = 0
    selection_label: str = ""
    selection: tuple[str, ...] = ()
    note: str = ""
    failure_details: list[str] = field(default_factory=list)

    @property
    def total_cases(self) -> int:
        return self.passed_cases + self.skipped_cases + self.failed_cases


@dataclass
class TestRunReport:
    scopes: tuple[str, ...]
    stages: list[TestStageSummary]

    @property
    def passed_cases(self) -> int:
        return sum(stage.passed_cases for stage in self.stages)

    @property
    def skipped_cases(self) -> int:
        return sum(stage.skipped_cases for stage in self.stages)

    @property
    def failed_cases(self) -> int:
        return sum(stage.failed_cases for stage in self.stages)

    @property
    def total_cases(self) -> int:
        return sum(stage.total_cases for stage in self.stages)

    @property
    def status(self) -> str:
        return "failed" if any(stage.status != "passed" for stage in self.stages) else "passed"


class TestStageError(RuntimeError):
    """Raised when a test stage fails after producing a summary."""

    def __init__(self, summary: TestStageSummary, message: str):
        super().__init__(message)
        self.summary = summary
        self.message = message


@dataclass(frozen=True)
class JsCaseResult:
    case: JsTestCase
    case_name: str
    status: str
    error: str = ""


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
    target: str
    monitor_baud: int
    listen_port: int
    server_python_exe: str
    esptool_bin: str
    test_wifi_ssid: str
    test_wifi_password: str
    test_http_url: str
    test_js_config: str


@dataclass(frozen=True)
class AppProfile:
    reference: str
    name: str
    file: Path
    directory: Path
    label: str
    flash_data_dir: Path
    sdkconfig_defaults: Path | None
    partition_table: Path


@dataclass(frozen=True)
class ProjectConfig:
    board: str
    board_file: Path
    board_label: str
    app: str
    app_file: Path
    app_profile_dir: Path
    app_label: str
    shared_flash_data_dir: Path
    app_flash_data_dir: Path
    flash_data_override: Path | None
    build_dir: Path
    generated_sdkconfig: Path
    board_sdkconfig_defaults: Path | None
    app_sdkconfig_defaults: Path | None
    sdkconfig_defaults: tuple[Path, ...]
    partition_table: Path
    idf_target: str
    idf_path: str
    target: str
    monitor_baud: int
    listen_port: int
    server_python_exe: str
    esptool_bin: str
    assume_prompt: str
    test_wifi_ssid: str
    test_wifi_password: str
    test_http_url: str
    test_js_config: str
    cmake_cache_entries: tuple[str, ...] = ()


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


def merged_optional_value(
    repo_env: dict[str, str],
    board_env: dict[str, str],
    key: str,
) -> str | None:
    """Resolve an optional config value, treating blank values as unset."""
    if key in os.environ:
        value = os.environ[key]
    elif key in repo_env:
        value = repo_env[key]
    elif key in board_env:
        value = board_env[key]
    else:
        return None

    value = value.strip()
    return value or None


def merged_optional_int(
    repo_env: dict[str, str],
    board_env: dict[str, str],
    key: str,
) -> int | None:
    """Resolve an optional integer config value."""
    raw_value = merged_optional_value(repo_env, board_env, key)
    if raw_value is None:
        return None

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


def app_reference_from_env(repo_env: dict[str, str], override: str | None) -> str:
    """Choose the application profile from CLI or repository-local settings."""
    if override:
        return override
    if os.environ.get("APP_FILE"):
        return os.environ["APP_FILE"]
    if repo_env.get("APP_FILE"):
        return repo_env["APP_FILE"]
    if os.environ.get("APP"):
        return os.environ["APP"]
    if repo_env.get("APP"):
        return repo_env["APP"]
    return "minimal"


def resolve_app_file(reference: str) -> Path:
    """Resolve an application profile to an on-disk `app.env` file."""
    ref_path = Path(reference).expanduser()
    if ref_path.is_absolute() or "/" in reference or "\\" in reference or reference.endswith(".env"):
        candidate = ref_path if ref_path.is_absolute() else ROOT_DIR / ref_path
    else:
        candidate = APP_DIR / reference

    app_file = candidate / "app.env" if candidate.is_dir() else candidate
    if not app_file.exists():
        raise SystemExit(
            f"Application profile {reference!r} not found. Add {app_file} or run "
            "`python scripts/remote.py apps` to list available applications."
        )
    return app_file.resolve()


def resolve_app_profile_path(
    raw_path: str,
    app_dir: Path,
    board_name: str,
    idf_target: str,
) -> Path:
    """Resolve an app path after expanding the selected board placeholders."""
    try:
        expanded = raw_path.format(board=board_name, idf_target=idf_target)
    except KeyError as exc:
        raise SystemExit(
            f"Unsupported application path placeholder {exc.args[0]!r} in {raw_path!r}; "
            "only {board} and {idf_target} are available."
        ) from exc

    path = Path(expanded).expanduser()
    if path.is_absolute():
        return path.resolve()
    if expanded.startswith(("apps/", "configs/", "shared/")):
        return (ROOT_DIR / path).resolve()
    return (app_dir / path).resolve()


def load_app_profile(
    app_override: str | None,
    board_name: str,
    idf_target: str,
) -> AppProfile:
    """Load an application profile for one board and validate all app inputs."""
    repo_env = load_dotenv(ENV_PATH)
    reference = app_reference_from_env(repo_env, app_override)
    app_file = resolve_app_file(reference)
    app_dir = app_file.parent
    app_env = load_dotenv(app_file)
    app_name = app_env.get("APP_ID", app_dir.name).strip()
    if not APP_ID_RE.fullmatch(app_name):
        raise SystemExit(
            f"{app_file} APP_ID must match {APP_ID_RE.pattern!r}; got {app_name!r}."
        )

    flash_data_raw = app_env.get("FLASH_DATA_DIR", "flash_data")
    flash_data_dir = resolve_app_profile_path(
        flash_data_raw, app_dir, board_name, idf_target
    )
    if not flash_data_dir.is_dir():
        raise SystemExit(f"Application flash data directory does not exist: {flash_data_dir}")
    if not (flash_data_dir / "index.js").is_file():
        raise SystemExit(f"Application flash data must contain index.js: {flash_data_dir}")

    app_defaults_raw = os.environ.get(
        "APP_SDKCONFIG_DEFAULTS",
        repo_env.get("APP_SDKCONFIG_DEFAULTS", app_env.get("APP_SDKCONFIG_DEFAULTS", "")),
    ).strip()
    if not app_defaults_raw and (app_dir / "sdkconfig.defaults").is_file():
        app_defaults_raw = "sdkconfig.defaults"
    app_defaults = (
        resolve_app_profile_path(app_defaults_raw, app_dir, board_name, idf_target)
        if app_defaults_raw
        else None
    )
    if app_defaults is not None and not app_defaults.is_file():
        raise SystemExit(f"Application sdkconfig defaults file does not exist: {app_defaults}")

    partition_raw = os.environ.get(
        "PARTITION_TABLE",
        repo_env.get(
            "PARTITION_TABLE",
            app_env.get("PARTITION_TABLE", "partitions/{board}.csv"),
        ),
    ).strip()
    if not partition_raw:
        raise SystemExit(f"{app_file} must define PARTITION_TABLE.")
    partition_table = resolve_app_profile_path(
        partition_raw, app_dir, board_name, idf_target
    )
    if not partition_table.is_file():
        raise SystemExit(
            f"Application partition table does not exist for board {board_name}: "
            f"{partition_table}"
        )

    return AppProfile(
        reference=reference,
        name=app_name,
        file=app_file,
        directory=app_dir,
        label=app_env.get("APP_LABEL", app_name),
        flash_data_dir=flash_data_dir,
        sdkconfig_defaults=app_defaults,
        partition_table=partition_table,
    )


def available_app_profiles() -> list[Path]:
    """Return application profiles shipped in the repository."""
    if not APP_DIR.exists():
        return []
    return sorted(path for path in APP_DIR.glob("*/app.env") if path.is_file())


def legacy_target(repo_env: dict[str, str], board_env: dict[str, str]) -> tuple[str, int]:
    """Resolve compatibility target settings from older env keys."""
    explicit_target = merged_optional_value(repo_env, board_env, "TARGET")
    if explicit_target:
        return explicit_target, merged_optional_int(repo_env, board_env, "LISTEN_PORT") or 2217

    legacy_espport = merged_optional_value(repo_env, board_env, "ESPPORT")
    if legacy_espport:
        return legacy_espport, merged_optional_int(repo_env, board_env, "LISTEN_PORT") or 2217

    remote_url_override = merged_optional_value(repo_env, board_env, "REMOTE_URL")
    if remote_url_override:
        return remote_url_override, merged_optional_int(repo_env, board_env, "LISTEN_PORT") or 2217

    remote_host = merged_optional_value(repo_env, board_env, "REMOTE_HOST")
    remote_port = merged_optional_int(repo_env, board_env, "REMOTE_PORT")
    if remote_host is not None or remote_port is not None:
        return default_remote_url(remote_host or "127.0.0.1", remote_port or 2217), merged_optional_int(repo_env, board_env, "LISTEN_PORT") or (remote_port or 2217)

    com_port = merged_optional_value(repo_env, board_env, "COM_PORT")
    if com_port:
        return com_port, merged_optional_int(repo_env, board_env, "LISTEN_PORT") or 2217

    return "", merged_optional_int(repo_env, board_env, "LISTEN_PORT") or 2217


def load_profile(board_override: str | None = None) -> BoardProfile:
    """Build the effective board profile from `/.env` and `configs/boards/*/.env`."""
    repo_env = load_dotenv(ENV_PATH)
    board_reference = board_reference_from_env(repo_env, board_override)
    board_file = resolve_board_file(board_reference)
    board_dir = board_file.parent
    board_env = load_dotenv(board_file)
    target, listen_port = legacy_target(repo_env, board_env)

    idf_path = merged_value(
        repo_env,
        board_env,
        "IDF_PATH",
        merged_value(repo_env, board_env, "IDF_PATH", str(Path.home() / "esp" / "esp-idf")),
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
        target=normalize_target(target),
        monitor_baud=merged_int(
            repo_env,
            board_env,
            "MONITOR_BAUD",
            merged_int(repo_env, board_env, "ESPBAUD", 115200),
        ),
        listen_port=listen_port,
        server_python_exe=merged_value(repo_env, board_env, "SERVER_PYTHON_EXE", "auto"),
        esptool_bin=merged_value(repo_env, board_env, "ESPTOOL_BIN", "auto"),
        test_wifi_ssid=merged_value(repo_env, board_env, "TEST_WIFI_SSID", ""),
        test_wifi_password=merged_value(repo_env, board_env, "TEST_WIFI_PASSWORD", ""),
        test_http_url=merged_value(repo_env, board_env, "TEST_HTTP_URL", ""),
        test_js_config=merged_value(repo_env, board_env, "TEST_JS_CONFIG", ""),
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
    }

    if platform.system() == "Windows":
        kwargs["creationflags"] = subprocess.CREATE_NO_WINDOW  # type: ignore[attr-defined]

    subprocess.Popen(cmd, **kwargs)
    time.sleep(1)

    existing = list_server_processes()
    if not existing:
        print("Failed to confirm esp_rfc2217_server startup.", file=sys.stderr)
        return 1

    print(f"RFC2217 server ready on {local_target} -> TCP {config.listen_port}")
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
    cmake_args.extend(config.cmake_cache_entries)

    if config.sdkconfig_defaults:
        joined_defaults = ";".join(str(path) for path in config.sdkconfig_defaults)
        cmake_args.append(f"-DSDKCONFIG_DEFAULTS={joined_defaults}")

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


def build_firmware_images(config: ProjectConfig) -> None:
    """Build flashable firmware images without rebuilding the LittleFS storage image."""
    run_idf_action_with_stale_build_recovery(["bootloader", "partition-table", "app"], config)


def build_fs_image(config: ProjectConfig) -> None:
    """Build only the LittleFS image used by the storage partition."""
    run_idf_action_with_stale_build_recovery(["littlefs_storage_bin"], config)


def build_workspace_image(config: ProjectConfig) -> None:
    """Build only the empty image used to initialize the workspace partition."""
    run_idf_action_with_stale_build_recovery(["littlefs_workspace_bin"], config)


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


def normalize_target(raw_target: str) -> str:
    """Normalize a device target so RFC2217 URLs keep the expected query flags."""
    target = raw_target.strip()
    if not target:
        return ""
    if urlsplit(target).scheme == "rfc2217":
        return normalize_remote_url(target)
    return target


def is_rfc2217_target(target: str) -> bool:
    """Return whether the configured target is an RFC2217 URL."""
    return urlsplit(normalize_target(target)).scheme == "rfc2217"


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


def chip_id(config: ProjectConfig) -> None:
    """Probe the device without writing flash."""
    write_esptool_config()
    subprocess.run(
        [*resolve_esptool_cmd(config.esptool_bin), "--port", command_port(config), "chip-id"],
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


def resolve_flash_entry_offsets(flasher_args: dict[str, object], entry_names: tuple[str, ...]) -> set[int]:
    """Resolve named flash entries to offsets so full flashes can skip selected partitions."""
    offsets: set[int] = set()

    for entry_name in entry_names:
        entry = flasher_args.get(entry_name)
        if not isinstance(entry, dict):
            continue
        offset = str(entry.get("offset", ""))
        if offset:
            offsets.add(int(offset, 0))

    return offsets


def resolve_flash_pairs(
    flasher_args: dict[str, object],
    build_dir: Path,
    exclude_entries: tuple[str, ...] = (),
) -> list[str]:
    """Resolve the full ESP-IDF flash plan, optionally skipping named entries."""
    flash_files = dict(flasher_args.get("flash_files", {}))
    excluded_offsets = resolve_flash_entry_offsets(flasher_args, exclude_entries)
    flash_pairs: list[str] = []

    for offset, file_name in sorted(flash_files.items(), key=lambda item: int(item[0], 0)):
        if int(offset, 0) in excluded_offsets:
            continue
        file_path = Path(file_name)
        if not file_path.is_absolute():
            file_path = build_dir / file_path
        flash_pairs.extend([offset, str(file_path)])

    if not flash_pairs:
        raise SystemExit("No flash entries remain after applying exclusions.")

    return flash_pairs


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
            command_port(config),
            "write-flash",
            *write_flash_args,
            *flash_pairs,
        ],
        cwd=ROOT_DIR,
        check=True,
    )


def flash(
    config: ProjectConfig,
    build_first: bool,
    exclude_entries: tuple[str, ...] = (),
    initialize_workspace: bool = False,
) -> None:
    """Flash build outputs while preserving workspace unless explicitly initialized."""
    write_esptool_config()

    effective_exclusions = tuple(dict.fromkeys((
        *exclude_entries,
        *(("workspace",) if not initialize_workspace else ()),
    )))

    if build_first:
        if "storage" in effective_exclusions and "workspace" in effective_exclusions:
            build_firmware_images(config)
        else:
            build(config)

    flasher_args = load_flasher_args(config.build_dir)
    extra_esptool_args = dict(flasher_args.get("extra_esptool_args", {}))
    write_flash_args = [str(arg) for arg in flasher_args.get("write_flash_args", [])]
    flash_pairs = resolve_flash_pairs(
        flasher_args,
        config.build_dir,
        exclude_entries=effective_exclusions,
    )

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


def flash_workspace(config: ProjectConfig, build_first: bool) -> None:
    """Build and flash an empty workspace image as an explicit destructive action."""
    write_esptool_config()

    if build_first:
        build_workspace_image(config)

    flasher_args = load_flasher_args(config.build_dir)
    extra_esptool_args = dict(flasher_args.get("extra_esptool_args", {}))
    write_flash_args = [str(arg) for arg in flasher_args.get("write_flash_args", [])]
    offset, file_path = resolve_flash_entry(flasher_args, config.build_dir, "workspace")

    write_flash(
        config,
        write_flash_args,
        [offset, str(file_path)],
        chip=str(extra_esptool_args.get("chip", config.idf_target)),
        before=str(extra_esptool_args.get("before", "default-reset")),
        after=str(extra_esptool_args.get("after", "hard-reset")),
    )


def monitor_cmd(config: ProjectConfig, *, no_reset: bool = False) -> list[str]:
    """Build the monitor command for the selected board profile."""
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
    )


def flash_monitor(
    config: ProjectConfig,
    build_first: bool,
    initialize_workspace: bool = False,
) -> None:
    """Build/flash, then open monitor over the selected target."""
    flash(
        config,
        build_first=build_first,
        initialize_workspace=initialize_workspace,
    )
    monitor(config)


def config_default_inputs(config: ProjectConfig) -> list[Path]:
    """Return tracked inputs that define the selected board/app configuration."""
    return [
        *config.sdkconfig_defaults,
        config.board_file,
        config.app_file,
        config.partition_table,
    ]


def refresh_generated_sdkconfig(config: ProjectConfig) -> None:
    """Regenerate the combination-local sdkconfig when profile inputs changed."""
    input_paths = [path.resolve() for path in config_default_inputs(config) if path.exists()]
    identity = json.dumps([str(path) for path in input_paths], indent=2) + "\n"
    stamp = config.generated_sdkconfig.with_name(
        f".{config.generated_sdkconfig.name}.inputs.json"
    )
    previous_identity = stamp.read_text(encoding="utf-8") if stamp.exists() else ""

    should_refresh = False
    if config.generated_sdkconfig.exists():
        generated_mtime = config.generated_sdkconfig.stat().st_mtime
        should_refresh = previous_identity != identity or any(
            path.stat().st_mtime > generated_mtime for path in input_paths
        )

    if should_refresh:
        print(
            f"Refreshing {format_path(config.generated_sdkconfig)} because "
            "board/application configuration inputs changed."
        )
        config.generated_sdkconfig.unlink(missing_ok=True)
        old_sdkconfig = config.generated_sdkconfig.with_name(
            f"{config.generated_sdkconfig.name}.old"
        )
        old_sdkconfig.unlink(missing_ok=True)

    stamp.parent.mkdir(parents=True, exist_ok=True)
    if previous_identity != identity:
        stamp.write_text(identity, encoding="utf-8")


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


def list_apps(selected_app: str) -> None:
    """Print bundled application profiles and the selected external profile."""
    profiles = available_app_profiles()
    selected = resolve_app_file(selected_app)
    selected_is_bundled = False

    for app_file in profiles:
        app_env = load_dotenv(app_file)
        is_selected = app_file.resolve() == selected
        selected_is_bundled = selected_is_bundled or is_selected
        marker = "*" if is_selected else " "
        app_name = app_env.get("APP_ID", app_file.parent.name)
        label = app_env.get("APP_LABEL", app_name)
        flash_data = app_env.get("FLASH_DATA_DIR", "flash_data")
        app_defaults = app_env.get("APP_SDKCONFIG_DEFAULTS", "sdkconfig.defaults")
        partition_table = app_env.get(
            "PARTITION_TABLE", "partitions/{board}.csv"
        )
        print(
            f"{marker} {app_name}: {label} "
            f"[{flash_data}, {app_defaults}, {partition_table}]"
        )

    if not selected_is_bundled:
        app_env = load_dotenv(selected)
        app_name = app_env.get("APP_ID", selected.parent.name)
        label = app_env.get("APP_LABEL", app_name)
        print(
            f"* {app_name}: {label} "
            f"[external: {selected.parent}]"
        )


def show_config(config: ProjectConfig) -> None:
    """Print the effective merged board, application, and tool configuration."""
    normalized_target = normalize_target(config.target)
    print(f"board={config.board}")
    print(f"board_file={config.board_file}")
    print(f"board_label={config.board_label}")
    print(f"app={config.app}")
    print(f"app_file={config.app_file}")
    print(f"app_profile_dir={config.app_profile_dir}")
    print(f"app_label={config.app_label}")
    print(f"shared_flash_data_dir={format_path(config.shared_flash_data_dir)}")
    print(f"app_flash_data_dir={format_path(config.app_flash_data_dir)}")
    print(f"flash_data_override={format_path(config.flash_data_override)}")
    print(f"idf_target={config.idf_target}")
    print(f"build_dir={format_path(config.build_dir)}")
    print(f"generated_sdkconfig={format_path(config.generated_sdkconfig)}")
    print(
        f"board_sdkconfig_defaults={format_path(config.board_sdkconfig_defaults)}"
    )
    print(f"app_sdkconfig_defaults={format_path(config.app_sdkconfig_defaults)}")
    print(
        "sdkconfig_defaults="
        + ";".join(format_path(path) for path in config.sdkconfig_defaults)
    )
    print(f"partition_table={format_path(config.partition_table)}")
    print(f"idf_path={config.idf_path}")
    print(f"target={config.target}")
    print(f"normalized_target={normalized_target}")
    print(f"target_kind={'rfc2217' if normalized_target and is_rfc2217_target(normalized_target) else ('serial' if normalized_target else '')}")
    print(f"command_port={normalized_target}")
    print(f"monitor_baud={config.monitor_baud}")
    print(f"listen_port={config.listen_port}")
    print(f"server_python_exe={config.server_python_exe}")
    print(f"esptool_bin={config.esptool_bin}")
    print(f"assume_prompt={config.assume_prompt}")
    print(f"test_wifi_ssid={config.test_wifi_ssid}")
    print(f"test_wifi_password_set={'yes' if config.test_wifi_password else 'no'}")
    print(f"test_http_url={config.test_http_url}")
    print(f"test_js_config_set={'yes' if config.test_js_config else 'no'}")


def parse_test_js_config(raw_config: str) -> dict[str, object]:
    """Decode optional JSON test configuration injected into the JS harness."""
    if not raw_config:
        return {}
    try:
        payload = json.loads(raw_config)
    except json.JSONDecodeError as exc:
        raise SystemExit(f"TEST_JS_CONFIG must be a JSON object ({exc}).") from exc
    if not isinstance(payload, dict):
        raise SystemExit("TEST_JS_CONFIG must be a JSON object.")
    return payload


def resolve_test_scopes(raw_scopes: list[str] | None) -> tuple[str, ...]:
    """Resolve selected test stages, defaulting to both C and JS in execution order."""
    if not raw_scopes:
        return TEST_SCOPE_ORDER

    selected = set(raw_scopes)
    return tuple(scope for scope in TEST_SCOPE_ORDER if scope in selected)


def resolve_js_modules(raw_modules: list[str] | None) -> tuple[JsTestModule, ...]:
    """Resolve JS test modules while preserving the registry order."""
    if not raw_modules:
        return JS_TEST_MODULES

    selected = set(raw_modules)
    return tuple(module for module in JS_TEST_MODULES if module.name in selected)


def resolve_js_test_capabilities(args: argparse.Namespace) -> set[str]:
    """Resolve opt-in JS test capabilities from CLI flags."""
    capabilities: set[str] = set()
    if args.network:
        capabilities.add("network")
    if args.loopback:
        capabilities.add("loopback")
    return capabilities


def describe_case_capabilities(capabilities: tuple[str, ...]) -> str:
    """Render required JS test capabilities as user-facing CLI flags."""
    return ", ".join(JS_TEST_CAPABILITY_FLAGS.get(capability, capability) for capability in capabilities)


def parse_ctest_summary(output: str) -> tuple[int, int] | None:
    """Extract failed and total test counts from ctest output when available."""
    match = CTEST_SUMMARY_RE.search(output)
    if match is None:
        return None
    failed = int(match.group(2))
    total = int(match.group(3))
    return failed, total


def print_test_stage_summary(summary: TestStageSummary) -> None:
    """Print one stage summary in a format shared by C and JS tests."""
    extras: list[str] = []
    if summary.selection:
        label = summary.selection_label or "selection"
        extras.append(f"{label}={','.join(summary.selection)}")
    if summary.note:
        extras.append(f"note={summary.note}")

    suffix = f" {' '.join(extras)}" if extras else ""
    print(
        f"{summary.name} summary: "
        f"status={summary.status} total={summary.total_cases} "
        f"passed={summary.passed_cases} skipped={summary.skipped_cases} failed={summary.failed_cases}"
        f"{suffix}",
        flush=True,
    )


def print_test_report(report: TestRunReport) -> None:
    """Print the final report for the whole `test` command run."""
    scopes = ",".join(report.scopes) if report.scopes else "<none>"
    print("Test report:", flush=True)
    for summary in report.stages:
        extras: list[str] = []
        if summary.selection:
            label = summary.selection_label or "selection"
            extras.append(f"{label}={','.join(summary.selection)}")
        if summary.note:
            extras.append(f"note={summary.note}")
        suffix = f" {' '.join(extras)}" if extras else ""
        print(
            f"- {summary.name}: status={summary.status} total={summary.total_cases} "
            f"passed={summary.passed_cases} skipped={summary.skipped_cases} failed={summary.failed_cases}"
            f"{suffix}",
            flush=True,
        )
        if summary.failure_details:
            print(f"  {summary.name} failures:", flush=True)
            for detail in summary.failure_details:
                print(f"  - {detail}", flush=True)
    print(
        f"Overall: status={report.status} scopes={scopes} stages={len(report.stages)} "
        f"total={report.total_cases} passed={report.passed_cases} "
        f"skipped={report.skipped_cases} failed={report.failed_cases}",
        flush=True,
    )


def run_host_c_tests() -> TestStageSummary:
    """Configure, build, and run the host-native C test suite."""
    summary = TestStageSummary(name="C")
    print(f"Running host C tests in {HOST_TEST_BUILD_DIR}", flush=True)
    try:
        subprocess.run(
            ["cmake", "-S", str(ROOT_DIR / "tests" / "c"), "-B", str(HOST_TEST_BUILD_DIR)],
            cwd=ROOT_DIR,
            check=True,
        )
        subprocess.run(
            ["cmake", "--build", str(HOST_TEST_BUILD_DIR)],
            cwd=ROOT_DIR,
            check=True,
        )
    except subprocess.CalledProcessError as exc:
        summary.status = "failed"
        summary.note = f"build step exited with code {exc.returncode}"
        print_test_stage_summary(summary)
        raise TestStageError(summary, "Host C tests failed during configure/build.") from exc

    ctest_result = subprocess.run(
        ["ctest", "--test-dir", str(HOST_TEST_BUILD_DIR), "--output-on-failure"],
        cwd=ROOT_DIR,
        check=False,
        capture_output=True,
        text=True,
    )
    if ctest_result.stdout:
        print(ctest_result.stdout, end="")
    if ctest_result.stderr:
        print(ctest_result.stderr, end="", file=sys.stderr)

    counts = parse_ctest_summary(f"{ctest_result.stdout}\n{ctest_result.stderr}")
    if counts is not None:
        failed, total = counts
        summary.passed_cases = total - failed
        summary.failed_cases = failed
    else:
        summary.note = "ctest summary unavailable"

    if ctest_result.returncode != 0:
        summary.status = "failed"
        if not summary.note:
            summary.note = f"ctest exited with code {ctest_result.returncode}"
        print_test_stage_summary(summary)
        raise TestStageError(summary, "Host C tests failed.") from subprocess.CalledProcessError(
            ctest_result.returncode,
            ctest_result.args,
            output=ctest_result.stdout,
            stderr=ctest_result.stderr,
        )

    print_test_stage_summary(summary)
    return summary


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
    """Start an ESP-IDF monitor session in a PTY so the JS runner can interact with it programmatically."""
    master_fd, slave_fd = os.openpty()
    try:
        process = subprocess.Popen(
            monitor_cmd(config, no_reset=True),
            cwd=ROOT_DIR,
            stdin=slave_fd,
            stdout=slave_fd,
            stderr=slave_fd,
            text=False,
            start_new_session=True,
            close_fds=True,
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


def wait_for_optional_js_repl_banner(session: MonitorSession, initial_output: str) -> None:
    """Wait for the REPL banner when observable, but let the runtime probe prove readiness."""
    if JS_REPL_BANNER_MARKER in initial_output:
        return

    try:
        read_monitor_until_text(session, JS_REPL_BANNER_MARKER, 25.0, "the JS REPL banner")
    except MarkerTimeoutError:
        print("JS REPL banner was not observed; probing the runtime directly", flush=True)


def send_js_command(session: MonitorSession, command: str) -> None:
    """Send one JavaScript command line to the remote REPL."""
    payload = f"{command}\r".encode("utf-8")
    os.write(session.master_fd, payload)


def js_test_build_config(config: ProjectConfig) -> ProjectConfig:
    """Return a build config that enables test instrumentation and test LittleFS."""
    build_dir = config.build_dir.parent / f"{config.build_dir.name}-js-test"
    target_defaults = JS_TEST_DIR / f"sdkconfig.{config.idf_target}.defaults"
    test_defaults = (JS_TEST_SDKCONFIG_DEFAULTS,)
    if target_defaults.is_file():
        test_defaults += (target_defaults,)
    cmake_entries = [
        entry for entry in config.cmake_cache_entries
        if not entry.startswith("-DESP32QJS_FLASH_DATA_DIR=") and
        not entry.startswith("-DESP32QJS_FLASH_DATA_INCLUDE_SHARED=")
    ]
    cmake_entries.append(f"-DESP32QJS_FLASH_DATA_DIR={JS_TEST_FLASH_DATA_DIR}")
    cmake_entries.append("-DESP32QJS_FLASH_DATA_INCLUDE_SHARED=ON")

    return replace(
        config,
        build_dir=build_dir,
        generated_sdkconfig=build_dir / config.generated_sdkconfig.name,
        sdkconfig_defaults=config.sdkconfig_defaults + test_defaults,
        flash_data_override=JS_TEST_FLASH_DATA_DIR,
        cmake_cache_entries=tuple(cmake_entries),
    )


def ensure_js_test_runtime(session: MonitorSession) -> None:
    """Confirm that the test bootstrap is active, either after boot or via a live probe."""
    ready_output = try_read_monitor_until_line_prefix(session, JS_TEST_READY_MARKER, 25.0)
    if ready_output is not None:
        return

    send_js_command(
        session,
        'print("__ESP32QJS_TEST_PROBE__:" + (typeof globalThis.test === "function" ? "ready" : "missing"))',
    )
    probe_output = read_monitor_until_line_prefix(session, "__ESP32QJS_TEST_PROBE__:", 5.0, "the JS test runtime probe")
    probe_line = find_last_output_line_with_prefix(probe_output, "__ESP32QJS_TEST_PROBE__:")
    if probe_line != "__ESP32QJS_TEST_PROBE__:ready":
        raise SystemExit(
            "The current LittleFS image does not expose the JS test runtime. "
            "Rerun without `--no-flash-fs` to flash the test storage image."
        )


def probe_js_runtime_features(session: MonitorSession) -> dict[str, bool]:
    """Read the runtime feature map from `sys.info().features`."""
    send_js_command(
        session,
        'print("__ESP32QJS_TEST_FEATURES__:" + JSON.stringify(sys.info().features))',
    )
    output = read_monitor_until_line_prefix(
        session,
        JS_TEST_FEATURES_PREFIX,
        25.0,
        "the JS runtime feature probe",
    )
    feature_line = find_last_output_line_with_prefix(output, JS_TEST_FEATURES_PREFIX)
    if feature_line is None:
        raise SystemExit("The JS runtime feature probe did not return a structured result.")
    try:
        payload = json.loads(feature_line[len(JS_TEST_FEATURES_PREFIX):])
    except json.JSONDecodeError as exc:
        raise SystemExit(f"The JS runtime feature probe emitted invalid JSON ({exc}).") from exc

    features: dict[str, bool] = {}
    for key, value in payload.items():
        if isinstance(value, bool):
            features[str(key)] = value
    return features


def configure_js_test_runtime(
    session: MonitorSession,
    config: ProjectConfig,
) -> None:
    """Push dynamic test configuration such as Wi-Fi credentials to the REPL."""
    test_config: dict[str, object] = {
        "wifiSsid": config.test_wifi_ssid,
        "wifiPassword": config.test_wifi_password,
        "httpUrl": config.test_http_url,
    }
    test_config.update(parse_test_js_config(config.test_js_config))
    encoded = json.dumps(json.dumps(test_config, separators=(",", ":")))

    send_js_command(
        session,
        (
            f"globalThis.testConfig = JSON.parse({encoded}); "
            'print("__ESP32QJS_TEST_CONFIG__")'
        ),
    )
    read_monitor_until_line_prefix(session, "__ESP32QJS_TEST_CONFIG__", 5.0, "the JS test config handshake")


def collect_js_runtime(session: MonitorSession) -> None:
    """Run a best-effort JS GC cycle between test cases to reduce cross-case state pressure."""
    try:
        send_js_command(
            session,
            'if (typeof gc === "function") gc(); print("__ESP32QJS_TEST_GC__")',
        )
        read_monitor_until_line_prefix(session, "__ESP32QJS_TEST_GC__", 5.0, "the JS GC handshake")
    except (MarkerTimeoutError, OSError):
        print("Warning: skipped JS GC handshake before the next case", flush=True)


def reset_js_test_runtime(session: MonitorSession, config: ProjectConfig) -> None:
    """Hard-reset the board and restore dynamic test configuration for an isolated case."""
    os.write(session.master_fd, b"\x14\x12")
    read_monitor_until_line_prefix(
        session,
        JS_TEST_READY_MARKER,
        25.0,
        "the isolated JS test runtime reset",
    )
    configure_js_test_runtime(session, config)


def validate_network_test_config(config: ProjectConfig, modules: tuple[JsTestModule, ...], network_enabled: bool) -> None:
    """Fail fast when network-only cases were requested without the needed credentials."""
    if not network_enabled:
        return

    module_names = {module.name for module in modules}
    missing: list[str] = []
    if "wifi" in module_names or "http" in module_names:
        if not config.test_wifi_ssid:
            missing.append("TEST_WIFI_SSID")
        if not config.test_wifi_password:
            missing.append("TEST_WIFI_PASSWORD")
    if "http" in module_names and not config.test_http_url:
        missing.append("TEST_HTTP_URL")

    if missing:
        joined = ", ".join(missing)
        raise SystemExit(
            f"Network JS tests require {joined}. Set them in `/.env` or the active board profile."
        )


def is_js_module_enabled(module: JsTestModule, runtime_features: dict[str, bool]) -> bool:
    """Check whether the runtime feature set supports a JS module."""
    return all(runtime_features.get(feature, False) for feature in module.required_features)


def resolve_js_modules_for_runtime(modules: tuple[JsTestModule, ...],
                                   runtime_features: dict[str, bool],
                                   explicit_selection: bool) -> tuple[tuple[JsTestModule, ...], str]:
    """Filter JS modules against the active board feature set."""
    if not runtime_features.get("fs", False):
        message = "board-backed JS tests require the fs feature because the harness is loaded from LittleFS"
        if explicit_selection:
            raise SystemExit(message)
        return (), message

    disabled_modules = tuple(
        module.name for module in modules if not is_js_module_enabled(module, runtime_features)
    )
    if explicit_selection and disabled_modules:
        joined = ", ".join(disabled_modules)
        raise SystemExit(f"Requested JS modules are disabled on this board: {joined}")

    enabled_modules = tuple(
        module for module in modules if is_js_module_enabled(module, runtime_features)
    )
    if disabled_modules:
        return enabled_modules, f"auto-skipped disabled modules: {','.join(disabled_modules)}"
    return enabled_modules, ""


def run_js_test_case(session: MonitorSession, case: JsTestCase) -> JsCaseResult:
    """Execute one JS test case file and collect a structured pass/fail result."""
    print(f"Running JS test {case.path}", flush=True)
    case_path = json.dumps(case.path)
    try:
        send_js_command(
            session,
            (
                "try { "
                f"load({case_path}); "
                "} catch (error) { "
                'print("__TEST_FAIL__:" + JSON.stringify({'
                f"name: {case_path}, "
                "error: String(error)"
                "})); "
                "}"
            ),
        )
    except OSError as exc:
        message = f"unable to send command to monitor session ({exc})"
        print(f"FAIL {case.path}: {message}", flush=True)
        return JsCaseResult(case=case, case_name=case.path, status="failed", error=message)

    deadline = time.monotonic() + case.timeout_seconds
    quiet_deadline: float | None = None
    output = ""
    result_line: str | None = None

    while True:
        now = time.monotonic()
        if quiet_deadline is None and now >= deadline:
            break
        if quiet_deadline is not None and now >= quiet_deadline:
            break

        chunk = read_monitor_chunk(session)
        if chunk:
            output += chunk.decode("utf-8", errors="replace")
            normalized = normalize_serial_output(output)
            for line in normalized.splitlines():
                if (
                    line.startswith(JS_TEST_PASS_PREFIX) or
                    line.startswith(JS_TEST_SKIP_PREFIX) or
                    line.startswith(JS_TEST_FAIL_PREFIX)
                ):
                    result_line = line
                    quiet_deadline = time.monotonic() + 0.2
            continue

        if session.process.poll() is not None:
            break

        time.sleep(0.05)

    if result_line is None:
        print(
            f"FAIL {case.path}: timed out waiting for a structured result\n"
            f"Last serial output:\n{format_output_tail(output)}",
            flush=True,
        )
        return JsCaseResult(
            case=case,
            case_name=case.path,
            status="failed",
            error="timed out waiting for a structured result",
        )

    if result_line.startswith(JS_TEST_FAIL_PREFIX):
        try:
            payload = json.loads(result_line[len(JS_TEST_FAIL_PREFIX):])
        except json.JSONDecodeError as exc:
            message = f"emitted an invalid failure payload ({exc})"
            print(
                f"FAIL {case.path}: {message}\n"
                f"Last serial output:\n{format_output_tail(output)}",
                flush=True,
            )
            return JsCaseResult(case=case, case_name=case.path, status="failed", error=message)

        case_name = payload.get("name", case.path)
        message = payload.get("error", "unknown error")
        print(
            f"FAIL {case_name}: {message}\n"
            f"Last serial output:\n{format_output_tail(output)}",
            flush=True,
        )
        return JsCaseResult(case=case, case_name=case_name, status="failed", error=message)

    if result_line.startswith(JS_TEST_SKIP_PREFIX):
        try:
            payload = json.loads(result_line[len(JS_TEST_SKIP_PREFIX):])
        except json.JSONDecodeError as exc:
            message = f"emitted an invalid skip payload ({exc})"
            print(
                f"FAIL {case.path}: {message}\n"
                f"Last serial output:\n{format_output_tail(output)}",
                flush=True,
            )
            return JsCaseResult(case=case, case_name=case.path, status="failed", error=message)

        case_name = payload.get("name", case.path)
        reason = payload.get("reason", "")
        reason_suffix = f" ({reason})" if reason else ""
        print(f"SKIP {case_name}{reason_suffix}", flush=True)
        return JsCaseResult(case=case, case_name=case_name, status="skipped", error=str(reason))

    try:
        payload = json.loads(result_line[len(JS_TEST_PASS_PREFIX):])
    except json.JSONDecodeError as exc:
        message = f"emitted an invalid pass payload ({exc})"
        print(
            f"FAIL {case.path}: {message}\n"
            f"Last serial output:\n{format_output_tail(output)}",
            flush=True,
        )
        return JsCaseResult(case=case, case_name=case.path, status="failed", error=message)

    case_name = payload.get("name", case.path)
    print(f"PASS {case_name}", flush=True)
    return JsCaseResult(case=case, case_name=case_name, status="passed")


def run_js_tests(config: ProjectConfig,
                 modules: tuple[JsTestModule, ...],
                 explicit_module_selection: bool,
                 enabled_capabilities: set[str],
                 flash_firmware_first: bool,
                 flash_fs_first: bool) -> TestStageSummary:
    """Flash the firmware and dedicated test image when needed, then drive JS tests over serial."""
    summary = TestStageSummary(
        name="JS",
        selection_label="modules",
        selection=tuple(module.name for module in modules),
    )
    js_config = js_test_build_config(config)

    if flash_firmware_first:
        print("Flashing dedicated JS test firmware (leaving storage for the JS test image)", flush=True)
        try:
            flash(js_config, build_first=True, exclude_entries=("storage",))
        except subprocess.CalledProcessError as exc:
            summary.status = "failed"
            summary.note = f"firmware flash exited with code {exc.returncode}"
            print_test_stage_summary(summary)
            raise TestStageError(summary, "JS firmware flash failed.") from exc

    if flash_fs_first:
        print(f"Flashing JS test LittleFS image from {JS_TEST_FLASH_DATA_DIR}", flush=True)
        try:
            flash_fs(js_config, build_first=True)
        except subprocess.CalledProcessError as exc:
            summary.status = "failed"
            summary.note = f"flash-fs exited with code {exc.returncode}"
            print_test_stage_summary(summary)
            raise TestStageError(summary, "JS test storage flash failed.") from exc

    if os.name == "nt":
        summary.status = "failed"
        summary.note = "board-backed JS tests require a POSIX host"
        print_test_stage_summary(summary)
        raise TestStageError(summary, "Board-backed JS tests currently require a POSIX host because they run through a PTY monitor session.")

    session = start_monitor_session(js_config)
    try:
        try:
            startup_output = read_monitor_until_text(session, MONITOR_READY_MARKER, 10.0, "the ESP-IDF monitor banner")
            wait_for_optional_js_repl_banner(session, startup_output)
        except MarkerTimeoutError as exc:
            summary.status = "failed"
            summary.note = f"startup timeout waiting for {exc.description}"
            print_test_stage_summary(summary)
            raise TestStageError(
                summary,
                f"Timed out waiting for {exc.description}.\n"
                f"Last serial output:\n{format_output_tail(exc.output)}",
            ) from exc
        try:
            runtime_features = probe_js_runtime_features(session)
        except MarkerTimeoutError as exc:
            summary.status = "failed"
            summary.note = f"feature probe timeout waiting for {exc.description}"
            print_test_stage_summary(summary)
            raise TestStageError(
                summary,
                f"Timed out waiting for {exc.description}.\n"
                f"Last serial output:\n{format_output_tail(exc.output)}",
            ) from exc
        try:
            selected_modules, selection_note = resolve_js_modules_for_runtime(
                modules,
                runtime_features,
                explicit_selection=explicit_module_selection,
            )
        except SystemExit as exc:
            summary.status = "failed"
            summary.note = str(exc)
            print_test_stage_summary(summary)
            raise TestStageError(summary, str(exc)) from exc

        summary.selection = tuple(module.name for module in selected_modules)
        summary.note = selection_note

        if not selected_modules:
            print_test_stage_summary(summary)
            return summary

        try:
            validate_network_test_config(config, selected_modules, "network" in enabled_capabilities)
        except SystemExit as exc:
            summary.status = "failed"
            summary.note = str(exc)
            print_test_stage_summary(summary)
            raise TestStageError(summary, str(exc)) from exc
        try:
            ensure_js_test_runtime(session)
        except MarkerTimeoutError as exc:
            summary.status = "failed"
            summary.note = f"startup timeout waiting for {exc.description}"
            print_test_stage_summary(summary)
            raise TestStageError(
                summary,
                f"Timed out waiting for {exc.description}.\n"
                f"Last serial output:\n{format_output_tail(exc.output)}",
            ) from exc
        try:
            configure_js_test_runtime(session, js_config)
        except MarkerTimeoutError as exc:
            summary.status = "failed"
            summary.note = f"config timeout waiting for {exc.description}"
            print_test_stage_summary(summary)
            raise TestStageError(
                summary,
                f"Timed out waiting for {exc.description}.\n"
                f"Last serial output:\n{format_output_tail(exc.output)}",
            ) from exc

        for module in selected_modules:
            print(f"Running JS module {module.name}", flush=True)
            for case in module.cases:
                missing_capabilities = tuple(
                    capability for capability in case.required_capabilities
                    if capability not in enabled_capabilities
                )
                if missing_capabilities:
                    required_flags = describe_case_capabilities(missing_capabilities)
                    print(f"Skipping JS test {case.path} (requires {required_flags})", flush=True)
                    summary.skipped_cases += 1
                    continue
                if case.reset_before:
                    try:
                        reset_js_test_runtime(session, js_config)
                    except (MarkerTimeoutError, OSError) as exc:
                        message = f"failed to reset the JS runtime before isolated case ({exc})"
                        print(f"FAIL {case.path}: {message}", flush=True)
                        summary.failed_cases += 1
                        summary.status = "failed"
                        summary.failure_details.append(f"{case.path}: {message}")
                        continue
                collect_js_runtime(session)
                result = run_js_test_case(session, case)
                if case.reset_after:
                    try:
                        reset_js_test_runtime(session, js_config)
                    except (MarkerTimeoutError, OSError) as exc:
                        message = f"failed to reset the JS runtime after isolated case ({exc})"
                        print(f"FAIL {case.path}: {message}", flush=True)
                        summary.failed_cases += 1
                        summary.status = "failed"
                        summary.failure_details.append(f"{case.path}: {message}")
                        continue
                if result.status == "failed":
                    summary.failed_cases += 1
                    summary.status = "failed"
                    summary.failure_details.append(f"{result.case_name}: {result.error}")
                    continue
                if result.status == "skipped":
                    summary.skipped_cases += 1
                    continue
                summary.passed_cases += 1
        print_test_stage_summary(summary)
        if summary.status != "passed":
            raise TestStageError(summary, "JS tests failed. See the final report for failure details.")
        return summary
    finally:
        close_monitor_session(session)


def run_js_syntax_check(app_flash_data_dir: Path | None = None) -> None:
    """Parse framework, selected-app, test, and documented JavaScript."""
    command = [sys.executable, str(JS_SYNTAX_CHECK_SCRIPT)]
    if app_flash_data_dir is not None:
        command.extend(("--extra-path", str(app_flash_data_dir)))
    returncode, _ = run_streaming(command, cwd=ROOT_DIR)
    if returncode != 0:
        raise SystemExit("MQuickJS JavaScript syntax validation failed.")


def run_test_command(config: ProjectConfig, args: argparse.Namespace) -> None:
    """Run the selected host C and/or board JS automated tests."""
    scopes = resolve_test_scopes(args.scope)
    report = TestRunReport(scopes=scopes, stages=[])

    if "js" not in scopes:
        if args.module:
            raise SystemExit("`--module` requires JS scope.")
        if args.network:
            raise SystemExit("`--network` requires JS scope.")
        if args.loopback:
            raise SystemExit("`--loopback` requires JS scope.")
        if args.no_flash_firmware:
            raise SystemExit("`--no-flash-firmware` requires JS scope.")
        if args.no_flash_fs:
            raise SystemExit("`--no-flash-fs` requires JS scope.")

    if "js" in scopes:
        run_js_syntax_check(config.app_flash_data_dir)

    stage_errors: list[str] = []

    if "c" in scopes:
        try:
            report.stages.append(run_host_c_tests())
        except TestStageError as exc:
            if not report.stages or report.stages[-1] is not exc.summary:
                report.stages.append(exc.summary)
            stage_errors.append(exc.message)

    if "js" in scopes:
        try:
            report.stages.append(
                run_js_tests(
                    config,
                    resolve_js_modules(args.module),
                    explicit_module_selection=bool(args.module),
                    enabled_capabilities=resolve_js_test_capabilities(args),
                    flash_firmware_first=not args.no_flash_firmware,
                    flash_fs_first=not args.no_flash_fs,
                )
            )
        except TestStageError as exc:
            if not report.stages or report.stages[-1] is not exc.summary:
                report.stages.append(exc.summary)
            stage_errors.append(exc.message)

    print_test_report(report)
    if stage_errors:
        raise SystemExit("One or more test stages failed.")


def build_project_config(
    args: argparse.Namespace,
    profile: BoardProfile,
    app_profile: AppProfile,
) -> ProjectConfig:
    """Convert parsed CLI args and selected profiles to the effective project config."""
    def cli_path(raw_path: str) -> Path | None:
        if not raw_path:
            return None
        path = Path(raw_path).expanduser()
        return (path if path.is_absolute() else ROOT_DIR / path).resolve()

    board_defaults = cli_path(getattr(args, "sdkconfig_defaults", ""))
    app_defaults = cli_path(getattr(args, "app_sdkconfig_defaults", ""))
    partition_table = cli_path(getattr(args, "partition_table", ""))
    if partition_table is None:
        partition_table = app_profile.partition_table

    for label, path in (
        ("Board sdkconfig defaults", board_defaults),
        ("Application sdkconfig defaults", app_defaults),
        ("Partition table", partition_table),
    ):
        if path is not None and not path.is_file():
            raise SystemExit(f"{label} file does not exist: {path}")

    sdkconfig_defaults = tuple(
        dict.fromkeys(
            path for path in (board_defaults, app_defaults) if path is not None
        )
    )

    build_dir = Path(args.build_dir).expanduser()
    if not build_dir.is_absolute():
        build_dir = ROOT_DIR / build_dir
    build_dir = build_dir.resolve()

    flash_data_override = cli_path(getattr(args, "flash_data_dir", ""))
    if flash_data_override is not None and not flash_data_override.is_dir():
        raise SystemExit(f"Flash data override does not exist: {flash_data_override}")

    shared_flash_data_dir = SHARED_FLASH_DATA_DIR.resolve()
    cmake_entries = (
        f"-DESP32QJS_BOARD={profile.name}",
        f"-DESP32QJS_APP={app_profile.name}",
        f"-DESP32QJS_APP_PROFILE_DIR={app_profile.directory}",
        f"-DESP32QJS_SHARED_FLASH_DATA_DIR={shared_flash_data_dir}",
        f"-DESP32QJS_APP_FLASH_DATA_DIR={app_profile.flash_data_dir}",
        f"-DESP32QJS_FLASH_DATA_DIR={flash_data_override or ''}",
        f"-DESP32QJS_BOARD_SDKCONFIG_DEFAULTS={board_defaults or ''}",
        f"-DESP32QJS_APP_SDKCONFIG_DEFAULTS={app_defaults or ''}",
        f"-DESP32QJS_PARTITION_TABLE={partition_table}",
    )

    config_slug = re.sub(
        r"[^A-Za-z0-9_.-]+", "_", f"{profile.name}.{app_profile.name}"
    )
    board_file = Path(args.board_file)
    return ProjectConfig(
        board=profile.name,
        board_file=board_file,
        board_label=profile.label,
        app=app_profile.name,
        app_file=app_profile.file,
        app_profile_dir=app_profile.directory,
        app_label=app_profile.label,
        shared_flash_data_dir=shared_flash_data_dir,
        app_flash_data_dir=app_profile.flash_data_dir,
        flash_data_override=flash_data_override,
        build_dir=build_dir,
        generated_sdkconfig=build_dir / f"sdkconfig.{config_slug}",
        board_sdkconfig_defaults=board_defaults,
        app_sdkconfig_defaults=app_defaults,
        sdkconfig_defaults=sdkconfig_defaults,
        partition_table=partition_table,
        idf_target=args.idf_target,
        idf_path=args.idf_path,
        target=normalize_target(getattr(args, "target", profile.target)),
        monitor_baud=getattr(args, "baud", profile.monitor_baud),
        listen_port=getattr(args, "listen_port", profile.listen_port),
        server_python_exe=getattr(args, "python_exe", profile.server_python_exe),
        esptool_bin=getattr(args, "esptool_bin", profile.esptool_bin),
        assume_prompt=args.assume,
        test_wifi_ssid=profile.test_wifi_ssid,
        test_wifi_password=profile.test_wifi_password,
        test_http_url=profile.test_http_url,
        test_js_config=profile.test_js_config,
        cmake_cache_entries=cmake_entries,
    )


def add_common_board_args(parser: argparse.ArgumentParser, profile: BoardProfile) -> None:
    """Attach the shared board/build-selection options at the top-level parser."""
    parser.add_argument(
        "--board",
        default=profile.reference,
        help="Board profile name from configs/boards/*/.env, or a direct path to a board directory/profile file.",
    )
    parser.add_argument("--build-dir", default=format_path(profile.build_dir))
    parser.add_argument("--idf-target", default=profile.idf_target)
    parser.add_argument(
        "--sdkconfig-defaults",
        default=format_path(profile.sdkconfig_defaults),
        help="Board/base sdkconfig defaults file applied before application defaults.",
    )
    parser.add_argument("--idf-path", default=profile.idf_path)
    parser.set_defaults(board_file=str(profile.file))


def add_common_app_args(parser: argparse.ArgumentParser, profile: AppProfile) -> None:
    """Attach application and LittleFS resource selection options."""
    parser.add_argument(
        "--app",
        default=profile.reference,
        help="Application name from apps/*/app.env, or a direct app directory/profile path.",
    )
    parser.add_argument(
        "--app-sdkconfig-defaults",
        default=format_path(profile.sdkconfig_defaults),
        help="Application sdkconfig defaults applied after the selected board defaults.",
    )
    parser.add_argument(
        "--partition-table",
        default=format_path(profile.partition_table),
        help="Application partition CSV for the selected board.",
    )
    parser.add_argument(
        "--flash-data-dir",
        default="",
        help="Use one complete LittleFS source directory instead of merging shared and app data.",
    )
    parser.set_defaults(app_file=str(profile.file))


def add_common_connection_args(parser: argparse.ArgumentParser, profile: BoardProfile) -> None:
    """Attach the shared device-target and tool overrides once at the top level."""
    parser.add_argument(
        "--target",
        default=profile.target,
        help="Device target for chip-id/flash/monitor/test/server. Use an rfc2217:// URL for remote boards, or a local serial device path such as /dev/ttyACM0 or COM3.",
    )
    parser.add_argument("--baud", type=int, default=profile.monitor_baud)
    parser.add_argument("--esptool-bin", default=profile.esptool_bin)
    parser.add_argument("--listen-port", type=int, default=profile.listen_port)
    parser.add_argument("--python-exe", default=profile.server_python_exe)


def parse_args(
    argv: list[str] | None = None,
) -> tuple[argparse.Namespace, BoardProfile, AppProfile]:
    """Parse CLI arguments after resolving board and application profiles."""
    raw_argv = sys.argv[1:] if argv is None else argv
    bootstrap = argparse.ArgumentParser(add_help=False)
    bootstrap.add_argument("--board")
    bootstrap.add_argument("--app")
    pre_args, _ = bootstrap.parse_known_args(raw_argv)
    profile = load_profile(pre_args.board)
    app_profile = load_app_profile(
        pre_args.app,
        board_name=profile.name,
        idf_target=profile.idf_target,
    )

    parser = argparse.ArgumentParser(description="Unified helper for ESP32QJS application development.")
    add_common_board_args(parser, profile)
    add_common_app_args(parser, app_profile)
    add_common_connection_args(parser, profile)
    parser.add_argument(
        "--assume",
        choices=("ask", "y", "n"),
        default="ask",
        help="Skip manual prompts by answering yes/no automatically. Default: ask.",
    )

    sub = parser.add_subparsers(dest="command", required=True)

    boards_parser = sub.add_parser("boards", help="List bundled board profiles.")
    boards_parser.set_defaults(_noop=True)

    apps_parser = sub.add_parser("apps", help="List bundled application profiles.")
    apps_parser.set_defaults(_noop=True)

    show_parser = sub.add_parser("show-config", help="Print the merged board/app/tool configuration.")
    show_parser.set_defaults(_noop=True)

    server = sub.add_parser("server", help="Write config and start esp_rfc2217_server.")
    server.add_argument("--force-restart", action="store_true")

    build_parser = sub.add_parser("build", help="Run idf.py build for the selected board.")
    build_parser.set_defaults(_noop=True)

    build_fs_parser = sub.add_parser("build-fs", help="Build only the LittleFS storage image.")
    build_fs_parser.set_defaults(_noop=True)

    check_js_parser = sub.add_parser(
        "check-js",
        help="Parse all first-party JavaScript with the exact MQuickJS engine.",
    )
    check_js_parser.set_defaults(_noop=True)

    chip = sub.add_parser("chip-id", help="Check connectivity over the selected target.")

    flash_parser = sub.add_parser("flash", help="Build and flash over the selected target.")
    flash_parser.add_argument("--no-build", action="store_true")
    flash_parser.add_argument(
        "--erase-workspace",
        action="store_true",
        help="Also flash the generated empty workspace image. Default: preserve workspace.",
    )

    flash_fs_parser = sub.add_parser("flash-fs", help="Build and flash only the LittleFS storage partition.")
    flash_fs_parser.add_argument("--no-build", action="store_true")

    flash_workspace_parser = sub.add_parser(
        "flash-workspace",
        help="Explicitly erase and initialize only the workspace partition.",
    )
    flash_workspace_parser.add_argument("--no-build", action="store_true")

    mon = sub.add_parser("monitor", help="Open ESP-IDF monitor over the selected target.")

    fm = sub.add_parser("flash-monitor", help="Build/flash, then open monitor over the selected target.")
    fm.add_argument("--no-build", action="store_true")
    fm.add_argument(
        "--erase-workspace",
        action="store_true",
        help="Also flash the generated empty workspace image. Default: preserve workspace.",
    )

    test = sub.add_parser("test", help="Run host C tests and/or board-backed JS tests.")
    test.add_argument(
        "--scope",
        action="append",
        choices=("c", "js"),
        help="Limit test stages. Repeat to include both. Default: run C and JS.",
    )
    test.add_argument(
        "--module",
        action="append",
        choices=[module.name for module in JS_TEST_MODULES],
        help="Limit JS tests to specific modules. Default: run board-enabled modules only.",
    )
    test.add_argument(
        "--network",
        action="store_true",
        help="Enable network-required JS cases inside the wifi/http modules.",
    )
    test.add_argument(
        "--loopback",
        action="store_true",
        help="Enable JS cases that require physical loopback wiring, such as SPI MOSI-to-MISO or UART TX-to-RX validation.",
    )
    test.add_argument(
        "--no-flash-firmware",
        action="store_true",
        help="Reuse the existing firmware instead of rebuilding and reflashing it before JS tests.",
    )
    test.add_argument(
        "--no-flash-fs",
        action="store_true",
        help="Reuse the existing JS test LittleFS image instead of rebuilding and reflashing it.",
    )

    return parser.parse_args(raw_argv), profile, app_profile


def main() -> int:
    args, profile, app_profile = parse_args()
    config = build_project_config(args, profile, app_profile)

    if args.command == "boards":
        list_boards(args.board)
        return 0

    if args.command == "apps":
        list_apps(args.app)
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

    if args.command == "check-js":
        run_js_syntax_check(config.app_flash_data_dir)
        return 0

    if args.command == "chip-id":
        chip_id(config)
        return 0

    if args.command == "flash":
        flash(
            config,
            build_first=not args.no_build,
            initialize_workspace=args.erase_workspace,
        )
        return 0

    if args.command == "flash-fs":
        flash_fs(config, build_first=not args.no_build)
        return 0

    if args.command == "flash-workspace":
        flash_workspace(config, build_first=not args.no_build)
        return 0

    if args.command == "monitor":
        monitor(config)
        return 0

    if args.command == "flash-monitor":
        flash_monitor(
            config,
            build_first=not args.no_build,
            initialize_workspace=args.erase_workspace,
        )
        return 0

    if args.command == "test":
        run_test_command(config, args)
        return 0

    return 2


if __name__ == "__main__":
    raise SystemExit(main())
