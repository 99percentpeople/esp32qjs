#!/usr/bin/env python3
"""Validate and summarize a sole-v1 32/40/256 CSI batch."""
from __future__ import annotations

import argparse
import json
from dataclasses import asdict
from pathlib import Path
from typing import Sequence

if __package__:
    from .esp32qjs_rx import RxBatch, RxProtocolError, parse_rx_batch
else:
    from esp32qjs_rx import RxBatch, RxProtocolError, parse_rx_batch


def parse_batch(data: bytes | bytearray | memoryview) -> RxBatch:
    return parse_rx_batch(data, kind="csi")


def _summary(batch: RxBatch) -> dict[str, object]:
    return {
        "protocol": "esp32qjs-csi/1", "frameCount": len(batch.frames),
        "metadataBytes": batch.metadata_bytes, "csiBytes": batch.csi_bytes,
        "packetBytes": batch.packet_bytes, "totalBytes": batch.total_bytes,
        "frames": [{**asdict(frame.metadata), "csi_bytes": len(frame.csi),
                    "packet_bytes": len(frame.packet), "packet_readable_length": frame.packet_readable_length,
                    "captured_header_length": frame.captured_header_length} for frame in batch.frames],
    }


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Validate and summarize one esp32qjs-csi/1 batch")
    parser.add_argument("input", type=Path)
    args = parser.parse_args(argv)
    print(json.dumps(_summary(parse_batch(args.input.read_bytes())), indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
