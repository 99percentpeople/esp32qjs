#!/usr/bin/env python3
"""Compatibility entrypoint for the modular ESP32QJS development CLI."""

from __future__ import annotations

import sys as _sys
from pathlib import Path as _Path

_SCRIPT_DIR = _Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in _sys.path:
    _sys.path.insert(0, str(_SCRIPT_DIR))

# Keep the historic import surface available to repository-local callers while
# implementation ownership lives in the stable-responsibility modules.
from esp32qjs.build import *  # noqa: F403
from esp32qjs.cli import *  # noqa: F403
from esp32qjs.device_tests import *  # noqa: F403
from esp32qjs.flash import *  # noqa: F403
from esp32qjs.profiles import *  # noqa: F403
from esp32qjs.server import *  # noqa: F403


if __name__ == "__main__":
    raise SystemExit(main())
