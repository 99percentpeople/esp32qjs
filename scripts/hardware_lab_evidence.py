#!/usr/bin/env python3
"""Record reproducible, non-secret context for one hardware-lab run."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def build_context_digest(directory: Path) -> str:
    """Hash relative names and bytes for the complete immutable Build Context."""
    directory = directory.resolve()
    if not directory.is_dir():
        raise SystemExit(f"Build Context is not a directory: {directory}")
    digest = hashlib.sha256(b"ESP32QJS_BUILD_CONTEXT_V1\0")
    files = sorted(
        directory.rglob("*"),
        key=lambda path: path.relative_to(directory).as_posix(),
    )
    for path in files:
        if path.is_symlink():
            raise SystemExit(f"Build Context must not contain symlinks: {path}")
        if not path.is_file():
            continue
        relative = path.relative_to(directory).as_posix().encode("utf-8")
        data = path.read_bytes()
        digest.update(len(relative).to_bytes(4, "big"))
        digest.update(relative)
        digest.update(len(data).to_bytes(8, "big"))
        digest.update(data)
    return f"sha256:{digest.hexdigest()}"


def git_revision(directory: Path) -> str:
    """Return the exact checked-out revision or fail the evidence gate."""
    try:
        return subprocess.run(
            ["git", "-C", str(directory), "rev-parse", "HEAD"],
            check=True,
            capture_output=True,
            text=True,
        ).stdout.strip()
    except (OSError, subprocess.CalledProcessError) as exc:
        raise SystemExit(f"Cannot resolve Git revision for {directory}") from exc


def label(value: str) -> str:
    value = value.strip()
    return value if value else "unspecified"


def hardware_lab_evidence(
    build_context: Path,
    *,
    idf_path: Path,
    transport_target: str,
    router_label: str,
    peer_label: str,
    channel_band: str,
    test_command: str,
) -> dict[str, object]:
    """Build the v1 hardware evidence context without reading Wi-Fi secrets."""
    build_context = build_context.resolve()
    try:
        manifest = json.loads(
            (build_context / "manifest.json").read_text(encoding="utf-8")
        )
        board = manifest["board"]
        hardware = manifest["hardware"]
        context_id = manifest["contextId"]
        target = hardware["mcu"]
    except (OSError, KeyError, TypeError, json.JSONDecodeError) as exc:
        raise SystemExit(f"Invalid Build Context manifest: {build_context}") from exc
    if manifest.get("schema") != 1:
        raise SystemExit("Hardware lab requires the current Build Context v1")
    if not all(
        isinstance(value, str) and value
        for value in (context_id, target, board.get("id"), board.get("label"))
    ):
        raise SystemExit("Build Context identity is incomplete")

    return {
        "schema": 1,
        "firmwareCommit": git_revision(ROOT),
        "idfRevision": git_revision(idf_path.resolve()),
        "target": target,
        "board": {
            "id": board["id"],
            "label": board["label"],
        },
        "buildContext": {
            "id": context_id,
            "digest": build_context_digest(build_context),
        },
        "transportTarget": label(transport_target),
        "radio": {
            "router": label(router_label),
            "peer": label(peer_label),
            "channelBand": label(channel_band),
        },
        "testCommand": test_command.strip(),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-context", type=Path, required=True)
    parser.add_argument("--idf-path", type=Path, default=os.environ.get("IDF_PATH"))
    parser.add_argument("--transport-target", default="")
    parser.add_argument("--router-label", default="")
    parser.add_argument("--peer-label", default="")
    parser.add_argument("--channel-band", default="")
    parser.add_argument("--test-command", required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.idf_path is None:
        raise SystemExit("--idf-path or IDF_PATH is required")

    evidence = hardware_lab_evidence(
        args.build_context,
        idf_path=args.idf_path,
        transport_target=args.transport_target,
        router_label=args.router_label,
        peer_label=args.peer_label,
        channel_band=args.channel_band,
        test_command=args.test_command,
    )
    encoded = json.dumps(evidence, indent=2, sort_keys=True) + "\n"
    if args.output is not None:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(encoded, encoding="utf-8")
    print(encoded, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
