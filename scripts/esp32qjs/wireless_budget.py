"""Validate explicit wireless admission limits in an immutable Build Context.

Only resolved framework inputs are read here; no Board resolution or .env access.
Allocator/SDK overhead and concurrent module demand still require measurement.
"""

from __future__ import annotations

import json
from pathlib import Path
import re


PREFIX = "ESP32_MQUICKJS_WIRELESS_"
LIMITS = {
    PREFIX + "INTERNAL_BUDGET_BYTES": 16 * 1024 * 1024,
    PREFIX + "PSRAM_BUDGET_BYTES": 64 * 1024 * 1024,
    PREFIX + "CONTROL_RESERVE_BYTES": 16 * 1024 * 1024,
}
FEATURES = ("WIFI", "WIFI_CSI", "ESPNOW", "BLE")


def read_defaults(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        match = re.fullmatch(r"\s*CONFIG_(\w+)\s*=(.*?)\s*", line)
        unset = re.fullmatch(r"\s*# CONFIG_(\w+) is not set\s*", line)
        if not match and not unset:
            continue
        name, value = (match[1], match[2]) if match else (unset[1], "n")
        if name in values and (name in LIMITS or name == "SPIRAM" or
                               name.startswith("ESP32_MQUICKJS_FEATURE_")):
            raise ValueError(f"sdkconfig.defaults:{line_number}: duplicate CONFIG_{name}")
        values[name] = value
    return values


def validate_wireless_budget(directory: Path, manifest: dict | None = None,
                             sdkconfig: Path | None = None) -> dict:
    directory = Path(directory)
    if manifest is None:
        manifest = json.loads((directory / "manifest.json").read_text(encoding="utf-8"))
    defaults = read_defaults(directory / "sdkconfig.defaults")
    resolved = None if sdkconfig is None else json.loads(sdkconfig.read_text(encoding="utf-8"))
    if not isinstance(manifest, dict) or (resolved is not None and not isinstance(resolved, dict)):
        raise ValueError("Build Context manifest and resolved sdkconfig must be objects")
    features = manifest.get("nativeFeatures", [])
    if not isinstance(features, list) or not all(isinstance(item, str) for item in features):
        raise ValueError("Build Context nativeFeatures must be a string array")
    enabled = bool(set(features) & {name.lower() for name in FEATURES}) or any(
        defaults.get("ESP32_MQUICKJS_FEATURE_" + name) == "y" or
        (resolved is not None and resolved.get("ESP32_MQUICKJS_FEATURE_" + name) is True)
        for name in FEATURES)
    if not enabled and not any(name in defaults for name in LIMITS):
        return {"schema": "wireless-budget/1", "wirelessEnabled": False, "limits": None}
    quotas = {}
    for name, maximum in LIMITS.items():
        text = defaults.get(name)
        if text is None or not re.fullmatch(r"0|[1-9][0-9]*", text):
            raise ValueError(f"Build Context requires explicit decimal CONFIG_{name}")
        value = int(text)
        if value > maximum:
            raise ValueError(f"CONFIG_{name} exceeds {maximum} bytes")
        if resolved is not None and (type(resolved.get(name)) is not int or resolved[name] != value):
            raise ValueError(f"Resolved CONFIG_{name} differs from immutable Build Context; recreate configuration")
        quotas[name] = value
    internal, psram, control = (quotas[name] for name in LIMITS)
    if control > internal or (enabled and (control == 0 or internal == control)):
        raise ValueError("Wireless budgets require 0 < control reserve < internal limit when enabled")
    hardware = manifest.get("hardware", {})
    if not isinstance(hardware, dict):
        raise ValueError("Build Context hardware must be an object")
    mode, physical = hardware.get("psramMode"), hardware.get("psramBytes")
    if mode not in ("none", "quad", "octal") or type(physical) is not int or physical < 0:
        raise ValueError("Build Context requires an exact nonnegative PSRAM byte count and bus mode")
    has_psram = mode != "none"
    if has_psram != (physical > 0):
        raise ValueError("Build Context PSRAM mode and byte count disagree")
    if psram > physical:
        raise ValueError("Wireless PSRAM quota exceeds Build Context physical PSRAM bytes")
    if "SPIRAM" in defaults and defaults["SPIRAM"] != ("y" if has_psram else "n"):
        raise ValueError("CONFIG_SPIRAM disagrees with Build Context hardware")
    if resolved is not None:
        if resolved.get("IDF_TARGET") != hardware.get("mcu"):
            raise ValueError("Resolved IDF_TARGET differs from Build Context hardware")
        if (resolved.get("SPIRAM") is True) != has_psram:
            raise ValueError("Resolved CONFIG_SPIRAM differs from Build Context hardware")
        if resolved.get("ESP32_MQUICKJS_PSRAM_MODE") != mode:
            raise ValueError("Resolved framework PSRAM mode differs from Build Context hardware")
        if has_psram and (resolved.get("SPIRAM_MODE_QUAD", False) is not (mode == "quad") or
                          resolved.get("SPIRAM_MODE_OCT", False) is not (mode == "octal")):
            raise ValueError("Resolved PSRAM driver bus mode differs from Build Context hardware")
        # This is a stack-payload lower bound. The target C assertion includes
        # actual StaticTask_t sizes; metadata/queues still use runtime admission.
        if enabled:
            workers = resolved.get("ESP32_MQUICKJS_FUTURE_WORKER_POOL_SIZE")
            if type(workers) is not int or not 1 <= workers <= 4:
                raise ValueError("Resolved Future worker count is outside the supported range")
            if workers * 4096 > internal - control:
                raise ValueError("Wireless internal data quota cannot hold the configured Future worker stacks")
    return {"schema": "wireless-budget/1", "wirelessEnabled": enabled,
            "limits": {"internalBytes": internal, "psramBytes": psram, "controlReserveBytes": control},
            "resolvedChecked": resolved is not None}
