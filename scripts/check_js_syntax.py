#!/usr/bin/env python3
"""Build a host MQuickJS parser and validate first-party JavaScript sources."""

from __future__ import annotations

import argparse
import os
import shlex
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Iterable


ROOT_DIR = Path(__file__).resolve().parents[1]
VENDOR_DIR = ROOT_DIR / "components" / "esp32_mquickjs" / "vendor" / "mquickjs"
CHECKER_SOURCE = ROOT_DIR / "scripts" / "mquickjs_syntax_check.c"
DEFAULT_BUILD_DIR = ROOT_DIR / "build" / "js-syntax"
DEFAULT_SOURCE_ROOTS = (
    ROOT_DIR / "apps",
    ROOT_DIR / "shared" / "flash_data",
    ROOT_DIR / "tests" / "js" / "flash_data",
)
DEFAULT_DOCUMENTS = (
    ROOT_DIR / "README.md",
    ROOT_DIR / "docs" / "c-api.md",
    ROOT_DIR / "docs" / "js-api.md",
)


def compiler_command(explicit: str | None = None) -> list[str]:
    raw = explicit or os.environ.get("CC", "")
    candidates = [shlex.split(raw)] if raw else []
    candidates.extend([[name] for name in ("cc", "gcc", "clang")])
    for candidate in candidates:
        if candidate and shutil.which(candidate[0]):
            return candidate
    raise RuntimeError("No host C compiler found; set CC or install cc/gcc/clang.")


def source_dependencies() -> tuple[Path, ...]:
    names = (
        "mqjs_stdlib.c",
        "mquickjs_build.c",
        "mquickjs_build.h",
        "mquickjs.c",
        "mquickjs.h",
        "mquickjs_priv.h",
        "dtoa.c",
        "dtoa.h",
        "libm.c",
        "cutils.c",
        "cutils.h",
    )
    return (CHECKER_SOURCE, *(VENDOR_DIR / name for name in names))


def checker_is_current(tool: Path, atom_header: Path, stdlib_header: Path) -> bool:
    outputs = (tool, atom_header, stdlib_header)
    if not all(path.is_file() for path in outputs):
        return False
    newest_source = max(path.stat().st_mtime_ns for path in source_dependencies())
    return min(path.stat().st_mtime_ns for path in outputs) >= newest_source


def run_checked(command: list[str], *, cwd: Path = ROOT_DIR, stdout=None) -> None:
    subprocess.run(command, cwd=cwd, stdout=stdout, check=True)


def generate_header(command: list[str], output_path: Path) -> None:
    result = subprocess.run(command, cwd=ROOT_DIR, capture_output=True)
    if result.returncode != 0:
        if result.stderr:
            sys.stderr.buffer.write(result.stderr)
        raise subprocess.CalledProcessError(result.returncode, command)
    output_path.write_bytes(result.stdout)


def build_checker(build_dir: Path, *, compiler: str | None = None, rebuild: bool = False) -> Path:
    build_dir.mkdir(parents=True, exist_ok=True)
    tool = build_dir / "mquickjs_syntax_check"
    generator = build_dir / "mqjs_stdlib"
    atom_header = build_dir / "mquickjs_atom.h"
    stdlib_header = build_dir / "mqjs_stdlib.h"
    if not rebuild and checker_is_current(tool, atom_header, stdlib_header):
        return tool

    cc = compiler_command(compiler)
    common_flags = [
        "-O2",
        "-MMD",
        "-D_GNU_SOURCE",
        "-fno-math-errno",
        "-fno-trapping-math",
        f"-I{VENDOR_DIR}",
    ]
    print(f"Building MQuickJS syntax checker in {build_dir}")

    stdlib_object = build_dir / "mqjs_stdlib.host.o"
    builder_object = build_dir / "mquickjs_build.host.o"
    run_checked([
        *cc,
        *common_flags,
        "-c",
        "-o",
        str(stdlib_object),
        str(VENDOR_DIR / "mqjs_stdlib.c"),
    ])
    run_checked([
        *cc,
        *common_flags,
        "-c",
        "-o",
        str(builder_object),
        str(VENDOR_DIR / "mquickjs_build.c"),
    ])
    run_checked([
        *cc,
        "-o",
        str(generator),
        str(stdlib_object),
        str(builder_object),
    ])
    generate_header([str(generator), "-a"], atom_header)
    generate_header([str(generator)], stdlib_header)

    run_checked([
        *cc,
        *common_flags,
        f"-I{build_dir}",
        "-o",
        str(tool),
        str(CHECKER_SOURCE),
        str(VENDOR_DIR / "mquickjs.c"),
        str(VENDOR_DIR / "dtoa.c"),
        str(VENDOR_DIR / "libm.c"),
        str(VENDOR_DIR / "cutils.c"),
        "-lm",
    ])
    return tool


def verify_checker_dialect(tool: Path, build_dir: Path) -> None:
    valid_source = build_dir / "dialect-valid.js"
    modern_source = build_dir / "dialect-modern.js"
    valid_source.write_text("var value = 1;\n", encoding="utf-8")
    modern_source.write_text("const value = (input) => input;\n", encoding="utf-8")

    valid = subprocess.run([str(tool), str(valid_source)], capture_output=True, text=True)
    modern = subprocess.run([str(tool), str(modern_source)], capture_output=True, text=True)
    if valid.returncode != 0:
        raise RuntimeError(
            "MQuickJS checker rejected baseline ES5 syntax:\n" + valid.stderr.strip()
        )
    if modern.returncode == 0:
        raise RuntimeError(
            "MQuickJS checker unexpectedly accepted const/arrow syntax."
        )


def iter_js_files(paths: Iterable[Path]) -> list[Path]:
    files: set[Path] = set()
    for raw_path in paths:
        path = raw_path.expanduser()
        if not path.is_absolute():
            path = (ROOT_DIR / path).resolve()
        else:
            path = path.resolve()
        if path.is_dir():
            files.update(candidate.resolve() for candidate in path.rglob("*.js") if candidate.is_file())
        elif path.is_file() and path.suffix == ".js":
            files.add(path)
        else:
            raise ValueError(f"JavaScript source path does not exist or is not a .js file: {path}")
    return sorted(files)


def extract_documented_js(build_dir: Path) -> list[Path]:
    snippet_dir = build_dir / "doc-snippets"
    snippet_dir.mkdir(parents=True, exist_ok=True)
    for stale_file in snippet_dir.glob("*.js"):
        stale_file.unlink()

    snippets: list[Path] = []
    for document in DEFAULT_DOCUMENTS:
        lines = document.read_text(encoding="utf-8").splitlines()
        index = 0
        while index < len(lines):
            if lines[index].strip().lower() not in ("```js", "```javascript"):
                index += 1
                continue
            start_line = index + 2
            index += 1
            body_lines: list[str] = []
            while index < len(lines) and lines[index].strip() != "```":
                body_lines.append(lines[index])
                index += 1
            body = "\n".join(body_lines)
            stripped = body.strip()
            if stripped.startswith("{") and stripped.endswith("}"):
                body = "(" + body + ");"
            name = (
                str(document.relative_to(ROOT_DIR))
                .replace("/", "__")
                .replace("\\", "__")
            )
            snippet_path = snippet_dir / f"{name}__line_{start_line}.js"
            snippet_path.write_text("\n" * (start_line - 1) + body + "\n", encoding="utf-8")
            snippets.append(snippet_path.resolve())
            index += 1
    return snippets


def display_path(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT_DIR))
    except ValueError:
        return str(path)


def check_sources(tool: Path, files: list[Path], *, source_count: int, snippet_count: int) -> int:
    if not files:
        print("No JavaScript files found.", file=sys.stderr)
        return 2
    command = [str(tool), *(display_path(path) for path in files)]
    result = subprocess.run(command, cwd=ROOT_DIR)
    summary = f"sources={source_count} docSnippets={snippet_count}"
    if result.returncode == 0:
        print(f"MQuickJS syntax: passed {summary}")
    else:
        print(f"MQuickJS syntax: failed {summary}", file=sys.stderr)
    return result.returncode


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Parse JavaScript with the exact MQuickJS engine used by the firmware."
    )
    parser.add_argument(
        "paths",
        nargs="*",
        type=Path,
        help="Files or directories to check instead of the default first-party roots.",
    )
    parser.add_argument(
        "--extra-path",
        action="append",
        default=[],
        type=Path,
        help="Add a file or directory to the default or explicitly selected source roots. Repeatable.",
    )
    parser.add_argument("--build-dir", type=Path, default=DEFAULT_BUILD_DIR)
    parser.add_argument("--cc", help="Host C compiler command. Defaults to CC, cc, gcc, or clang.")
    parser.add_argument("--rebuild", action="store_true", help="Rebuild the host parser even when cached.")
    parser.add_argument(
        "--no-docs",
        action="store_true",
        help="Skip runnable JavaScript examples in README.md and the API references.",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    build_dir = args.build_dir.expanduser()
    if not build_dir.is_absolute():
        build_dir = (ROOT_DIR / build_dir).resolve()
    try:
        tool = build_checker(build_dir, compiler=args.cc, rebuild=args.rebuild)
        verify_checker_dialect(tool, build_dir)
        paths = [*(args.paths if args.paths else DEFAULT_SOURCE_ROOTS), *args.extra_path]
        source_files = iter_js_files(paths)
        snippets = [] if args.no_docs or args.paths else extract_documented_js(build_dir)
    except (OSError, RuntimeError, ValueError, subprocess.CalledProcessError) as exc:
        print(f"MQuickJS syntax checker setup failed: {exc}", file=sys.stderr)
        return 2
    return check_sources(
        tool,
        [*source_files, *snippets],
        source_count=len(source_files),
        snippet_count=len(snippets),
    )


if __name__ == "__main__":
    raise SystemExit(main())
