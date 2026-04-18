#!/usr/bin/env python3
"""Regenerate the mquickjs generated headers used by the ESP32 component."""

from __future__ import annotations

import subprocess
from pathlib import Path


ROOT_DIR = Path(__file__).resolve().parent.parent
MQUICKJS_DIR = ROOT_DIR / "vender" / "mquickjs"
OUTPUT_DIR = ROOT_DIR / "components" / "mquickjs" / "generated"


def run(cmd: list[str], cwd: Path) -> None:
    subprocess.run(cmd, cwd=cwd, check=True, text=True)


def write_stdout(cmd: list[str], cwd: Path, output_path: Path) -> None:
    with output_path.open("w", encoding="ascii", newline="\n") as output_file:
        subprocess.run(cmd, cwd=cwd, check=True, text=True, stdout=output_file)


def main() -> int:
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    run(["make", "mqjs_stdlib"], cwd=MQUICKJS_DIR)
    write_stdout(
        ["./mqjs_stdlib", "-a", "-m32"],
        cwd=MQUICKJS_DIR,
        output_path=OUTPUT_DIR / "mquickjs_atom.h",
    )
    write_stdout(
        ["./mqjs_stdlib", "-m32"],
        cwd=MQUICKJS_DIR,
        output_path=OUTPUT_DIR / "mqjs_stdlib.h",
    )

    print(f"Updated generated headers in {OUTPUT_DIR}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
