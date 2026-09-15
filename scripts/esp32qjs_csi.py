#!/usr/bin/env python3
"""Command entry point; implementation lives in the esp32qjs package."""
from capture.csi import main

if __name__ == "__main__":
    raise SystemExit(main())
