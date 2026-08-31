"""MCU profiles, immutable Build Contexts, and resolved project configuration."""

from __future__ import annotations

import argparse
import json
import os
import re
from dataclasses import dataclass
from pathlib import Path
from urllib.parse import parse_qsl, quote, urlsplit, urlunsplit


SCRIPT_DIR = Path(__file__).resolve().parent.parent


ROOT_DIR = Path(__file__).resolve().parent.parent.parent


BUILD_ROOT = ROOT_DIR / "build"


ENV_PATH = ROOT_DIR / ".env"


CONFIG_DIR = ROOT_DIR / "configs"


MCU_DIR = CONFIG_DIR / "mcus"


DEFAULT_BUILD_CONTEXT_DIR = ROOT_DIR / "tests" / "build-contexts" / "esp32s3"


SUPPORTED_MCU_TARGETS = frozenset(("esp32c3", "esp32c5", "esp32s3"))


SUPPORTED_FLASH_SIZE_MB = frozenset((4, 8, 16, 32))


SUPPORTED_PSRAM_MODES = frozenset(("none", "quad", "octal"))


GPIO_MAX_BY_MCU = {"esp32c3": 21, "esp32c5": 28, "esp32s3": 48}


@dataclass(frozen=True)
class MCUProfile:
    reference: str
    name: str
    file: Path
    label: str
    idf_target: str
    default_flash_size_mb: int
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
class BuildContextProfile:
    reference: str
    name: str
    file: Path
    directory: Path
    label: str
    flash_data_dir: Path
    sdkconfig_defaults: Path | None
    partition_layout: str
    storage_size: int | None
    mcu: str
    flash_size_mb: int
    psram_mode: str
    psram_size_bytes: int


@dataclass(frozen=True)
class ProjectConfig:
    mcu: str
    mcu_file: Path
    mcu_label: str
    build_context_id: str
    build_context_manifest: Path
    build_context_dir: Path
    build_context_label: str
    build_context_flash_data_dir: Path
    flash_data_override: Path | None
    build_dir: Path
    generated_sdkconfig: Path
    mcu_sdkconfig_defaults: Path | None
    build_context_sdkconfig_defaults: Path | None
    sdkconfig_defaults: tuple[Path, ...]
    partition_table: Path
    profile_constants_file: Path
    flash_size_mb: int
    psram_mode: str
    psram_size_bytes: int
    idf_target: str
    idf_path: str
    target: str
    monitor_baud: int
    listen_port: int
    server_python_exe: str
    esptool_bin: str
    assume_prompt: str
    allow_external_build_dir: bool
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
    mcu_env: dict[str, str],
    key: str,
    default: str,
) -> str:
    """Resolve a config value from process env, then `/.env`, then mcu profile."""
    return os.environ.get(key, repo_env.get(key, mcu_env.get(key, default)))


def merged_int(
    repo_env: dict[str, str],
    mcu_env: dict[str, str],
    key: str,
    default: int,
) -> int:
    """Resolve and validate an integer config value."""
    raw_value = os.environ.get(key, repo_env.get(key, mcu_env.get(key)))
    if raw_value is None:
        return default

    try:
        return int(raw_value)
    except ValueError as exc:
        raise SystemExit(f"Invalid integer for {key}: {raw_value!r}") from exc


def merged_optional_value(
    repo_env: dict[str, str],
    mcu_env: dict[str, str],
    key: str,
) -> str | None:
    """Resolve an optional config value, treating blank values as unset."""
    if key in os.environ:
        value = os.environ[key]
    elif key in repo_env:
        value = repo_env[key]
    elif key in mcu_env:
        value = mcu_env[key]
    else:
        return None

    value = value.strip()
    return value or None


def merged_optional_int(
    repo_env: dict[str, str],
    mcu_env: dict[str, str],
    key: str,
) -> int | None:
    """Resolve an optional integer config value."""
    raw_value = merged_optional_value(repo_env, mcu_env, key)
    if raw_value is None:
        return None

    try:
        return int(raw_value)
    except ValueError as exc:
        raise SystemExit(f"Invalid integer for {key}: {raw_value!r}") from exc


def resolve_build_path(raw_path: str) -> Path:
    """Resolve relative build outputs under the repository build directory."""
    path = Path(raw_path).expanduser()
    if path.is_absolute():
        return path.resolve()
    if path.parts[:1] == ("build",):
        return (ROOT_DIR / path).resolve()
    return (BUILD_ROOT / path).resolve()


def format_path(path: Path | None) -> str:
    """Render a path relative to the repository root when possible."""
    if path is None:
        return ""
    try:
        return str(path.relative_to(ROOT_DIR))
    except ValueError:
        return str(path)


def mcu_reference_from_env(repo_env: dict[str, str], override: str | None) -> str:
    """Choose the mcu profile reference from CLI or local environment."""
    if override:
        return override
    if os.environ.get("MCU_FILE"):
        return os.environ["MCU_FILE"]
    if repo_env.get("MCU_FILE"):
        return repo_env["MCU_FILE"]
    if os.environ.get("MCU"):
        return os.environ["MCU"]
    if repo_env.get("MCU"):
        return repo_env["MCU"]
    return "esp32s3"


def mcu_env_file(mcu_dir: Path) -> Path:
    """Return the profile `.env` file for a mcu directory."""
    return mcu_dir / ".env"


def mcu_sdkconfig_defaults_file(mcu_dir: Path) -> Path:
    """Return the default sdkconfig defaults file for a mcu directory."""
    return mcu_dir / "sdkconfig.defaults"


def resolve_profile_path(raw_path: str, mcu_dir: Path) -> Path:
    """Resolve a mcu-relative or repo-relative path."""
    path = Path(raw_path).expanduser()
    if path.is_absolute():
        return path
    if raw_path.startswith("./") or raw_path.startswith("../") or raw_path.startswith("configs/"):
        return ROOT_DIR / path
    return mcu_dir / path


def resolve_mcu_file(reference: str) -> Path:
    """Resolve a mcu profile reference to an on-disk `.env` file."""
    ref_path = Path(reference).expanduser()
    if ref_path.is_absolute() or "/" in reference or "\\" in reference or reference.endswith(".env"):
        candidate = ref_path if ref_path.is_absolute() else ROOT_DIR / ref_path
    else:
        candidate = MCU_DIR / reference

    if candidate.is_dir():
        mcu_file = mcu_env_file(candidate)
    else:
        mcu_file = candidate

    if not mcu_file.exists():
        raise SystemExit(
            f"MCU profile {reference!r} not found. Add {mcu_file} or run "
            "`python scripts/remote.py mcus` to list available profiles."
        )

    return mcu_file.resolve()


def build_context_reference(repo_env: dict[str, str], override: str | None) -> str:
    """Choose the immutable Build Context from CLI or repository settings."""
    return override or os.environ.get("ESP32QJS_BUILD_CONTEXT_DIR") or repo_env.get(
        "ESP32QJS_BUILD_CONTEXT_DIR", str(DEFAULT_BUILD_CONTEXT_DIR)
    )


def load_build_context(reference: str | None) -> BuildContextProfile:
    """Load one fixed-layout Build Context and derive its MCU identity."""
    selected = build_context_reference(load_dotenv(ENV_PATH), reference)
    path = Path(selected).expanduser()
    directory = (path if path.is_absolute() else ROOT_DIR / path).resolve()
    required = (
        "manifest.json",
        "sdkconfig.defaults",
        "partitions.csv",
        "profile-constants.inc",
        "precompile.json",
    )
    if not directory.is_dir() or any(not (directory / name).is_file() for name in required):
        raise SystemExit(f"Build Context is incomplete: {directory}")
    flash_data = directory / "flash_data"
    if not flash_data.is_dir() or not (flash_data / "index.js").is_file():
        raise SystemExit(f"Build Context flash_data must contain index.js: {directory}")
    try:
        manifest = json.loads((directory / "manifest.json").read_text(encoding="utf-8"))
        hardware = manifest["hardware"]
        mcu = hardware["mcu"]
        flash_bytes = int(hardware["flashBytes"])
        psram_mode = hardware["psramMode"]
        psram_bytes = int(hardware["psramBytes"])
    except (KeyError, TypeError, ValueError, json.JSONDecodeError) as exc:
        raise SystemExit(f"Build Context manifest has invalid hardware identity: {directory}") from exc
    if mcu not in SUPPORTED_MCU_TARGETS or flash_bytes not in {
        size * 1024 * 1024 for size in SUPPORTED_FLASH_SIZE_MB
    } or psram_mode not in SUPPORTED_PSRAM_MODES or psram_bytes < 0:
        raise SystemExit(f"Build Context manifest selects unsupported hardware: {directory}")
    board = manifest.get("board", {})
    name = str(board.get("id", directory.name))
    label = str(board.get("label", name))
    return BuildContextProfile(
        reference=selected,
        name=name,
        file=directory / "manifest.json",
        directory=directory,
        label=label,
        flash_data_dir=flash_data,
        sdkconfig_defaults=directory / "sdkconfig.defaults",
        partition_layout="context",
        storage_size=None,
        mcu=mcu,
        flash_size_mb=flash_bytes // (1024 * 1024),
        psram_mode=psram_mode,
        psram_size_bytes=psram_bytes,
    )


def legacy_target(repo_env: dict[str, str], mcu_env: dict[str, str]) -> tuple[str, int]:
    """Resolve compatibility target settings from older env keys."""
    explicit_target = merged_optional_value(repo_env, mcu_env, "TARGET")
    if explicit_target:
        return explicit_target, merged_optional_int(repo_env, mcu_env, "LISTEN_PORT") or 2217

    legacy_espport = merged_optional_value(repo_env, mcu_env, "ESPPORT")
    if legacy_espport:
        return legacy_espport, merged_optional_int(repo_env, mcu_env, "LISTEN_PORT") or 2217

    remote_url_override = merged_optional_value(repo_env, mcu_env, "REMOTE_URL")
    if remote_url_override:
        return remote_url_override, merged_optional_int(repo_env, mcu_env, "LISTEN_PORT") or 2217

    remote_host = merged_optional_value(repo_env, mcu_env, "REMOTE_HOST")
    remote_port = merged_optional_int(repo_env, mcu_env, "REMOTE_PORT")
    if remote_host is not None or remote_port is not None:
        return default_remote_url(remote_host or "127.0.0.1", remote_port or 2217), merged_optional_int(repo_env, mcu_env, "LISTEN_PORT") or (remote_port or 2217)

    com_port = merged_optional_value(repo_env, mcu_env, "COM_PORT")
    if com_port:
        return com_port, merged_optional_int(repo_env, mcu_env, "LISTEN_PORT") or 2217

    return "", merged_optional_int(repo_env, mcu_env, "LISTEN_PORT") or 2217


def load_profile(mcu_override: str | None = None) -> MCUProfile:
    """Build the effective mcu profile from `/.env` and `configs/mcus/*/.env`."""
    repo_env = load_dotenv(ENV_PATH)
    mcu_reference = mcu_reference_from_env(repo_env, mcu_override)
    mcu_file = resolve_mcu_file(mcu_reference)
    mcu_dir = mcu_file.parent
    mcu_env = load_dotenv(mcu_file)
    target, listen_port = legacy_target(repo_env, mcu_env)

    idf_path = merged_value(
        repo_env,
        mcu_env,
        "IDF_PATH",
        merged_value(repo_env, mcu_env, "IDF_PATH", str(Path.home() / "esp" / "esp-idf")),
    )
    sdkconfig_defaults_raw = merged_value(repo_env, mcu_env, "SDKCONFIG_DEFAULTS", "")
    if not sdkconfig_defaults_raw:
        sdkconfig_defaults_raw = merged_value(repo_env, mcu_env, "SDKCONFIG", "")

    idf_target = merged_value(repo_env, mcu_env, "IDF_TARGET", "")
    if not idf_target:
        raise SystemExit(f"{mcu_file} must define IDF_TARGET.")
    if idf_target not in SUPPORTED_MCU_TARGETS:
        supported = ", ".join(sorted(SUPPORTED_MCU_TARGETS))
        raise SystemExit(f"Unsupported MCU target {idf_target!r}; supported: {supported}.")
    default_flash_size_mb = merged_int(repo_env, mcu_env, "DEFAULT_FLASH_SIZE_MB", 4)
    if default_flash_size_mb not in SUPPORTED_FLASH_SIZE_MB:
        raise SystemExit("DEFAULT_FLASH_SIZE_MB must be one of 4, 8, 16, 32.")

    return MCUProfile(
        reference=mcu_reference,
        name=mcu_dir.name,
        file=mcu_file,
        label=merged_value(repo_env, mcu_env, "MCU_LABEL", mcu_dir.name),
        idf_target=idf_target,
        default_flash_size_mb=default_flash_size_mb,
        build_dir=resolve_build_path(
            merged_value(repo_env, mcu_env, "BUILD_DIR", mcu_dir.name)
        ),
        sdkconfig_defaults=(
            resolve_profile_path(sdkconfig_defaults_raw, mcu_dir)
            if sdkconfig_defaults_raw
            else (mcu_sdkconfig_defaults_file(mcu_dir) if mcu_sdkconfig_defaults_file(mcu_dir).exists() else None)
        ),
        idf_path=str(Path(idf_path).expanduser()),
        target=normalize_target(target),
        monitor_baud=merged_int(
            repo_env,
            mcu_env,
            "MONITOR_BAUD",
            merged_int(repo_env, mcu_env, "ESPBAUD", 115200),
        ),
        listen_port=listen_port,
        server_python_exe=merged_value(repo_env, mcu_env, "SERVER_PYTHON_EXE", "auto"),
        esptool_bin=merged_value(repo_env, mcu_env, "ESPTOOL_BIN", "auto"),
        test_wifi_ssid=merged_value(repo_env, mcu_env, "TEST_WIFI_SSID", ""),
        test_wifi_password=merged_value(repo_env, mcu_env, "TEST_WIFI_PASSWORD", ""),
        test_http_url=merged_value(repo_env, mcu_env, "TEST_HTTP_URL", ""),
        test_js_config=merged_value(repo_env, mcu_env, "TEST_JS_CONFIG", ""),
    )


def available_mcu_profiles() -> list[Path]:
    """Return the mcu profiles shipped in the repository."""
    if not MCU_DIR.exists():
        return []
    return sorted(path for path in MCU_DIR.glob("*/.env") if path.is_file())


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


def list_mcus(selected_mcu: str) -> None:
    """Print the mcu profiles bundled with the repository."""
    profiles = available_mcu_profiles()
    if not profiles:
        print("No mcu profiles found.")
        return

    selected = resolve_mcu_file(selected_mcu)
    for mcu_file in profiles:
        mcu_env = load_dotenv(mcu_file)
        marker = "*" if mcu_file.resolve() == selected else " "
        label = mcu_env.get("MCU_LABEL", mcu_file.parent.name)
        target = mcu_env.get("IDF_TARGET", "?")
        build_dir = mcu_env.get("BUILD_DIR", "build")
        sdkconfig_defaults = mcu_env.get("SDKCONFIG_DEFAULTS", mcu_env.get("SDKCONFIG", ""))
        if not sdkconfig_defaults:
            sdkconfig_defaults = format_path(mcu_sdkconfig_defaults_file(mcu_file.parent))
        detail = f"{target}, {build_dir}"
        if sdkconfig_defaults:
            detail = f"{detail}, {sdkconfig_defaults}"
        print(f"{marker} {mcu_file.parent.name}: {label} [{detail}]")


def show_config(config: ProjectConfig) -> None:
    """Print the effective MCU, Build Context, and tool configuration."""
    normalized_target = normalize_target(config.target)
    print(f"mcu={config.mcu}")
    print(f"mcu_file={config.mcu_file}")
    print(f"mcu_label={config.mcu_label}")
    print(f"build_context_id={config.build_context_id}")
    print(f"build_context_manifest={config.build_context_manifest}")
    print(f"build_context_dir={config.build_context_dir}")
    print(f"build_context_label={config.build_context_label}")
    print(f"build_context_flash_data={format_path(config.build_context_flash_data_dir)}")
    print(f"idf_target={config.idf_target}")
    print(f"build_dir={format_path(config.build_dir)}")
    print(f"generated_sdkconfig={format_path(config.generated_sdkconfig)}")
    print(
        f"mcu_sdkconfig_defaults={format_path(config.mcu_sdkconfig_defaults)}"
    )
    print(
        "build_context_sdkconfig_defaults="
        f"{format_path(config.build_context_sdkconfig_defaults)}"
    )
    print(
        "sdkconfig_defaults="
        + ";".join(format_path(path) for path in config.sdkconfig_defaults)
    )
    print(f"partition_table={format_path(config.partition_table)}")
    print(f"profile_constants_file={format_path(config.profile_constants_file)}")
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
    print(f"test_wifi_ssid_set={'yes' if config.test_wifi_ssid else 'no'}")
    print(f"test_wifi_password_set={'yes' if config.test_wifi_password else 'no'}")
    print(f"test_http_url={config.test_http_url}")
    print(f"test_js_config_set={'yes' if config.test_js_config else 'no'}")


def build_project_config(
    args: argparse.Namespace,
    profile: MCUProfile,
    build_context: BuildContextProfile,
) -> ProjectConfig:
    """Convert one MCU profile and immutable Build Context to project config."""
    def cli_path(raw_path: str) -> Path | None:
        if not raw_path:
            return None
        path = Path(raw_path).expanduser()
        return (path if path.is_absolute() else ROOT_DIR / path).resolve()

    mcu_defaults = cli_path(getattr(args, "sdkconfig_defaults", ""))
    if mcu_defaults is not None and not mcu_defaults.is_file():
        raise SystemExit(f"MCU sdkconfig defaults file does not exist: {mcu_defaults}")
    if build_context.mcu != profile.idf_target:
        raise SystemExit(
            f"Build Context targets {build_context.mcu}, not {profile.idf_target}"
        )

    build_dir = resolve_build_path(args.build_dir)
    build_root = BUILD_ROOT.resolve()
    allow_external_build_dir = bool(args.allow_external_build_dir)
    if (build_root not in build_dir.parents and
        not allow_external_build_dir):
        raise SystemExit(
            f"Build directory {build_dir} is outside {BUILD_ROOT}; pass "
            "--allow-external-build-dir to opt in explicitly."
        )
    sdkconfig_defaults = tuple(
        dict.fromkeys(
            path for path in (mcu_defaults, build_context.sdkconfig_defaults) if path is not None
        )
    )
    cmake_entries = (
        f"-DESP32QJS_MCU={profile.name}",
        f"-DESP32QJS_MCU_SDKCONFIG_DEFAULTS={mcu_defaults or ''}",
        f"-DESP32QJS_BUILD_CONTEXT_DIR={build_context.directory}",
    )

    config_slug = re.sub(
        r"[^A-Za-z0-9_.-]+", "_", f"{profile.name}.{build_context.name}"
    )
    mcu_file = Path(args.mcu_file)
    return ProjectConfig(
        mcu=profile.name,
        mcu_file=mcu_file,
        mcu_label=profile.label,
        build_context_id=build_context.name,
        build_context_manifest=build_context.file,
        build_context_dir=build_context.directory,
        build_context_label=build_context.label,
        build_context_flash_data_dir=build_context.flash_data_dir,
        flash_data_override=None,
        build_dir=build_dir,
        generated_sdkconfig=build_dir / f"sdkconfig.{config_slug}",
        mcu_sdkconfig_defaults=mcu_defaults,
        build_context_sdkconfig_defaults=build_context.sdkconfig_defaults,
        sdkconfig_defaults=sdkconfig_defaults,
        partition_table=build_context.directory / "partitions.csv",
        profile_constants_file=build_context.directory / "profile-constants.inc",
        flash_size_mb=build_context.flash_size_mb,
        psram_mode=build_context.psram_mode,
        psram_size_bytes=build_context.psram_size_bytes,
        idf_target=profile.idf_target,
        idf_path=args.idf_path,
        target=normalize_target(getattr(args, "target", profile.target)),
        monitor_baud=getattr(args, "baud", profile.monitor_baud),
        listen_port=getattr(args, "listen_port", profile.listen_port),
        server_python_exe=getattr(args, "python_exe", profile.server_python_exe),
        esptool_bin=getattr(args, "esptool_bin", profile.esptool_bin),
        assume_prompt=args.assume,
        allow_external_build_dir=allow_external_build_dir,
        test_wifi_ssid=profile.test_wifi_ssid,
        test_wifi_password=profile.test_wifi_password,
        test_http_url=profile.test_http_url,
        test_js_config=profile.test_js_config,
        cmake_cache_entries=cmake_entries,
    )
