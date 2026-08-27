#!/usr/bin/env python3
"""Bundle allowlisted startup scripts and compile them to 32-bit MQuickJS bytecode."""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path, PurePosixPath

from check_js_syntax import DEFAULT_BUILD_DIR, build_checker


LOAD_LINE = re.compile(
    r"^[ \t]*load\((?P<quote>['\"])(?P<path>[^'\"]+)(?P=quote)\);[ \t]*$"
)
EXPECTED_MANIFEST_KEYS = {
    "version",
    "entry",
    "output",
    "inline",
    "removeAfterCompile",
}


@dataclass(frozen=True)
class StartupManifest:
    entry: str
    output: str
    inline: frozenset[str]
    remove_after_compile: tuple[str, ...]


def safe_relative_path(value: object, label: str) -> str:
    if not isinstance(value, str) or not value or "\\" in value or "\0" in value:
        raise ValueError(f"{label} must be a non-empty POSIX relative path")
    path = PurePosixPath(value)
    if path.is_absolute() or any(part in ("", ".", "..") for part in path.parts):
        raise ValueError(f"{label} must stay below its configured root")
    return path.as_posix()


def resolve_below(root: Path, relative: str, *, require_file: bool = False) -> Path:
    resolved_root = root.resolve(strict=True)
    candidate = (resolved_root / Path(*PurePosixPath(relative).parts)).resolve(strict=True)
    if candidate != resolved_root and resolved_root not in candidate.parents:
        raise ValueError(f"path escapes configured root: {relative}")
    if require_file and not candidate.is_file():
        raise ValueError(f"startup source is not a regular file: {relative}")
    return candidate


def load_manifest(path: Path) -> StartupManifest:
    raw = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(raw, dict) or set(raw) != EXPECTED_MANIFEST_KEYS:
        raise ValueError(
            "startup manifest must contain exactly "
            + ", ".join(sorted(EXPECTED_MANIFEST_KEYS))
        )
    if raw["version"] != 1:
        raise ValueError("unsupported startup precompile manifest version")
    inline_raw = raw["inline"]
    remove_raw = raw["removeAfterCompile"]
    if not isinstance(inline_raw, list):
        raise ValueError("startup manifest inline must be an array")
    if not isinstance(remove_raw, list):
        raise ValueError("startup manifest removeAfterCompile must be an array")
    inline = [safe_relative_path(value, "inline entry") for value in inline_raw]
    if len(set(inline)) != len(inline):
        raise ValueError("startup manifest contains duplicate inline entries")
    return StartupManifest(
        entry=safe_relative_path(raw["entry"], "entry"),
        output=safe_relative_path(raw["output"], "output"),
        inline=frozenset(inline),
        remove_after_compile=tuple(
            safe_relative_path(value, "removeAfterCompile entry") for value in remove_raw
        ),
    )


def bundle_startup(source_root: Path, manifest: StartupManifest) -> str:
    included: dict[str, int] = {}

    def expand(relative: str, stack: tuple[str, ...]) -> str:
        if relative in stack:
            raise ValueError("recursive startup load: " + " -> ".join((*stack, relative)))
        source_path = resolve_below(source_root, relative, require_file=True)
        lines = source_path.read_text(encoding="utf-8").splitlines(keepends=True)
        output = [f"/* bundled from {relative} */\n"]
        for line in lines:
            match = LOAD_LINE.fullmatch(line.rstrip("\r\n"))
            loaded = match.group("path") if match is not None else None
            if loaded in manifest.inline:
                included[loaded] = included.get(loaded, 0) + 1
                output.append(expand(loaded, (*stack, relative)))
            else:
                output.append(line)
        if output and not output[-1].endswith("\n"):
            output[-1] += "\n"
        return "".join(output)

    bundle = expand(manifest.entry, ())
    missing = sorted(manifest.inline - included.keys())
    repeated = sorted(path for path, count in included.items() if count != 1)
    if missing:
        raise ValueError("startup manifest entries were not loaded: " + ", ".join(missing))
    if repeated:
        raise ValueError("startup scripts must be loaded exactly once: " + ", ".join(repeated))
    return bundle


def compile_startup(
    manifest_path: Path,
    source_root: Path,
    staging_root: Path,
    build_dir: Path,
    *,
    compiler: str | None = None,
) -> Path:
    manifest = load_manifest(manifest_path.resolve(strict=True))
    source_root = source_root.resolve(strict=True)
    staging_root = staging_root.resolve(strict=True)
    bundle = bundle_startup(source_root, manifest)
    build_dir.mkdir(parents=True, exist_ok=True)
    bundle_path = build_dir / "startup.bundle.js"
    bundle_path.write_text(bundle, encoding="utf-8")
    tool = build_checker(build_dir / "host", compiler=compiler)

    output_path = staging_root / Path(*PurePosixPath(manifest.output).parts)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    temporary_output = output_path.with_name(output_path.name + ".precompile-tmp")
    try:
        result = subprocess.run(
            [str(tool), "--compile32", str(bundle_path), str(temporary_output)],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        if result.returncode != 0:
            detail = result.stderr.strip() or result.stdout.strip() or "unknown compiler failure"
            raise RuntimeError(f"MQuickJS startup compilation failed: {detail}")
        header = temporary_output.read_bytes()[:4]
        if (len(header) != 4 or
                int.from_bytes(header[:2], "little") != 0xACFB or
                int.from_bytes(header[2:], "little") != 1):
            raise RuntimeError(
                "host compiler did not produce little-endian 32-bit MQuickJS bytecode"
            )
        os.replace(temporary_output, output_path)
    finally:
        temporary_output.unlink(missing_ok=True)

    for relative in manifest.remove_after_compile:
        target = staging_root / Path(*PurePosixPath(relative).parts)
        resolved_parent = target.parent.resolve(strict=True)
        if resolved_parent != staging_root and staging_root not in resolved_parent.parents:
            raise ValueError(f"removeAfterCompile escapes staging root: {relative}")
        if target.is_dir() and not target.is_symlink():
            shutil.rmtree(target)
        elif target.exists() or target.is_symlink():
            target.unlink()

    print(
        "MQuickJS startup bytecode: "
        f"sourceBytes={len(bundle.encode('utf-8'))} bytecodeBytes={output_path.stat().st_size} "
        f"output={output_path}"
    )
    return output_path


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--source-root", required=True, type=Path)
    parser.add_argument("--staging-root", required=True, type=Path)
    parser.add_argument("--build-dir", type=Path, default=DEFAULT_BUILD_DIR / "startup")
    parser.add_argument("--cc")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        compile_startup(
            args.manifest,
            args.source_root,
            args.staging_root,
            args.build_dir.expanduser().resolve(),
            compiler=args.cc,
        )
    except (OSError, RuntimeError, ValueError, json.JSONDecodeError) as error:
        print(f"MQuickJS startup precompile failed: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
