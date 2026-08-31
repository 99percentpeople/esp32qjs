"""ESP-IDF command construction, build actions, and local tool configuration."""

from __future__ import annotations

import importlib.util
import json
import os
import platform
import shlex
import shutil
import subprocess
import sys
from pathlib import Path

from .profiles import BUILD_ROOT, ROOT_DIR, ProjectConfig, format_path


TOOL_STATE_DIR = BUILD_ROOT / "tooling"


ESPTOOL_CONFIG_PATH = TOOL_STATE_DIR / "esptool.cfg"


ESPTOOL_CONFIG_TEXT = """[esptool]
custom_reset_sequence = R0|D0|W0.1|D1|R0|W0.1|R1|D0|R1|W0.1|D0|R0
custom_hard_reset_sequence = R1|W0.2|R0
"""


def run(
    cmd: list[str],
    cwd: Path | None = None,
    check: bool = True,
    interactive: bool = False,
    env: dict[str, str] | None = None,
) -> subprocess.CompletedProcess[str]:
    if interactive:
        return subprocess.run(cmd, cwd=cwd, check=check, text=True, env=env)
    return subprocess.run(
        cmd,
        cwd=cwd,
        check=check,
        text=True,
        capture_output=False,
        env=env,
    )


def run_streaming(
    cmd: list[str],
    cwd: Path | None = None,
    env: dict[str, str] | None = None,
) -> tuple[int, str]:
    """Run a command while streaming combined stdout/stderr and capturing it."""
    process = subprocess.Popen(
        cmd,
        cwd=cwd,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        bufsize=1,
        env=env,
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


def esptool_config_path() -> Path:
    """Return the project-local config used by esptool / esp_rfc2217_server."""
    return ESPTOOL_CONFIG_PATH


def write_esptool_config() -> Path:
    path = esptool_config_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(ESPTOOL_CONFIG_TEXT, encoding="ascii")
    return path


def esptool_environment(
    config_path: Path | None = None,
    base_environment: dict[str, str] | None = None,
) -> dict[str, str]:
    """Return a child-process environment selecting the project-local config."""
    path = config_path or write_esptool_config()
    environment = dict(os.environ if base_environment is None else base_environment)
    environment["ESPTOOL_CFGFILE"] = str(path)
    return environment


def idf_py_cmd(project_args: list[str], config: ProjectConfig) -> list[str]:
    """Return a command that can run `idf.py` for the selected mcu profile."""
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


def safe_remove_build_dir(build_dir: Path, allow_external: bool = False) -> None:
    """Remove a generated build directory after explicit confirmation."""
    resolved_build_dir = build_dir.resolve()
    resolved_build_root = BUILD_ROOT.resolve()
    always_unsafe = {
        Path(resolved_build_dir.anchor),
        Path.home().resolve(),
        ROOT_DIR.resolve(),
        resolved_build_root,
    }
    if (resolved_build_dir in always_unsafe or
        (resolved_build_root not in resolved_build_dir.parents and not allow_external)):
        raise SystemExit(f"Refusing to delete unsafe build directory: {build_dir}")
    shutil.rmtree(resolved_build_dir)


def run_idf_action_with_stale_build_recovery(project_args: list[str], config: ProjectConfig) -> None:
    """Run an idf.py action, offering one confirmed clean rebuild on stale-cache failures."""
    environment = esptool_environment()
    refresh_generated_sdkconfig(config)
    existing_build_dir = config.build_dir.exists()
    cmd = idf_py_cmd(project_args, config)
    return_code, output = run_streaming(cmd, cwd=ROOT_DIR, env=environment)
    if return_code == 0:
        return

    if existing_build_dir and is_stale_build_dir_failure(output):
        if not confirm_build_dir_reset(config.build_dir, config.assume_prompt):
            raise SystemExit(return_code)
        safe_remove_build_dir(
            config.build_dir,
            allow_external=config.allow_external_build_dir,
        )
        print(f"Retrying with a clean build directory: {config.build_dir}")
        return_code, _ = run_streaming(
            idf_py_cmd(project_args, config),
            cwd=ROOT_DIR,
            env=environment,
        )
        if return_code == 0:
            return

    raise SystemExit(return_code)


def build(config: ProjectConfig) -> None:
    """Build firmware for the selected mcu."""
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


def config_default_inputs(config: ProjectConfig) -> list[Path]:
    """Return tracked inputs that define the MCU and Build Context configuration."""
    return [
        *config.sdkconfig_defaults,
        config.mcu_file,
        config.build_context_manifest,
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
            "MCU/Build Context inputs changed."
        )
        config.generated_sdkconfig.unlink(missing_ok=True)
        old_sdkconfig = config.generated_sdkconfig.with_name(
            f"{config.generated_sdkconfig.name}.old"
        )
        old_sdkconfig.unlink(missing_ok=True)

    stamp.parent.mkdir(parents=True, exist_ok=True)
    if previous_identity != identity:
        stamp.write_text(identity, encoding="utf-8")
