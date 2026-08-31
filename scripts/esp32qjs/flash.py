"""Chip probing and explicit firmware/storage/workspace flash operations."""

from __future__ import annotations

import json
import subprocess
from pathlib import Path

from .build import (
    build,
    build_firmware_images,
    build_fs_image,
    build_workspace_image,
    esptool_environment,
    resolve_esptool_cmd,
)
from .profiles import ROOT_DIR, ProjectConfig
from .server import command_port, monitor


def chip_id(config: ProjectConfig) -> None:
    """Probe the device without writing flash."""
    subprocess.run(
        [*resolve_esptool_cmd(config.esptool_bin), "--port", command_port(config), "chip-id"],
        cwd=ROOT_DIR,
        check=True,
        env=esptool_environment(),
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
        env=esptool_environment(),
    )


def flash(
    config: ProjectConfig,
    build_first: bool,
    exclude_entries: tuple[str, ...] = (),
    initialize_workspace: bool = False,
) -> None:
    """Flash build outputs while preserving workspace unless explicitly initialized."""
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
