#!/usr/bin/env python3
"""Validate a sole-v1 Monitor batch and emit one JSONL observation per frame."""
from __future__ import annotations

import argparse
import json
import os
import tempfile
from dataclasses import asdict
from pathlib import Path
from typing import Sequence

from capture.rx import RxBatch, parse_rx_batch
from capture.pcapng import encode_monitor_pcapng


def parse_batch(data: bytes | bytearray | memoryview) -> RxBatch:
    return parse_rx_batch(data, kind="monitor")


def summaries(batch: RxBatch) -> list[dict[str, object]]:
    if batch.kind != "monitor":
        raise ValueError("Monitor summaries require a Monitor batch")
    return [{"protocol": "esp32qjs-monitor/1", "frameIndex": index,
             "metadata": asdict(frame.metadata), "packetBytes": len(frame.packet),
             "packetReadableLength": frame.packet_readable_length,
             "capturedHeaderLength": frame.captured_header_length}
            for index, frame in enumerate(batch.frames)]


def _write_atomic(path: Path, data: bytes) -> None:
    temporary: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent, prefix=".monitor-", delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Validate a Monitor batch and export JSONL or PCAPNG")
    parser.add_argument("input", type=Path)
    parser.add_argument("--format", choices=("jsonl", "pcapng"), default="jsonl")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--boot-id", help="external physical device boot identity, not a session generation")
    parser.add_argument("--relative", action="store_true", help="explicitly accept relative timestamps, NOT UTC")
    parser.add_argument("--utc-anchor-us", type=int, help="UTC Unix microseconds at the monotonic anchor")
    parser.add_argument("--monotonic-anchor-us", type=int, help="device monotonic microseconds at the UTC anchor")
    args = parser.parse_args(argv)
    if args.output is not None and args.input.resolve() == args.output.resolve():
        parser.error("input and output must differ")
    if args.format == "pcapng":
        if args.output is None or args.boot_id is None:
            parser.error("PCAPNG requires --output and --boot-id")
        data = encode_monitor_pcapng(args.input.read_bytes(), boot_id=args.boot_id, relative=args.relative,
                                    utc_anchor_us=args.utc_anchor_us, monotonic_anchor_us=args.monotonic_anchor_us)
    else:
        if args.boot_id is not None or args.relative or args.utc_anchor_us is not None or args.monotonic_anchor_us is not None:
            parser.error("clock options apply only to PCAPNG; JSONL preserves native monotonic timestamps")
        records = summaries(parse_batch(args.input.read_bytes()))
        data = "".join(json.dumps(record, sort_keys=True) + "\n" for record in records).encode("utf-8")
    # All validation/encoding precedes publication; failed export preserves output.
    if args.output is None:
        print(data.decode("utf-8"), end="")
    else:
        _write_atomic(args.output, data)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
