#!/usr/bin/env python3
"""Generate deterministic firmware-only Build Contexts for the CI matrix."""

from __future__ import annotations

import argparse
import json
import shutil
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FEATURE_CATALOG = ROOT / "components" / "esp32_mquickjs" / "runtime-features.json"
TEMPLATE_CONTEXT = ROOT / "tests" / "build-contexts" / "esp32s3"
SUPPORTED_TARGETS = ("esp32c3", "esp32c5", "esp32s3")
SUPPORTED_PROFILES = (
    "minimal",
    "bitmap",
    "representative",
    "representative-psram",
    "disabled",
)


def load_features() -> list[dict[str, object]]:
    catalog = json.loads(FEATURE_CATALOG.read_text(encoding="utf-8"))
    if catalog.get("schema") != 1 or not isinstance(catalog.get("features"), list):
        raise SystemExit(f"Invalid runtime feature catalog: {FEATURE_CATALOG}")
    return catalog["features"]


def selected_feature_ids(
    features: list[dict[str, object]], target: str, profile: str
) -> set[str]:
    supported = {
        str(feature["id"])
        for feature in features
        if target in feature.get("targets", [])
    }
    if profile == "disabled":
        return set()
    if profile == "minimal":
        return {"fs"}
    if profile == "bitmap":
        return {"fs", "bitmap"}
    return supported


def sdkconfig_text(
    features: list[dict[str, object]], target: str, profile: str
) -> tuple[str, list[str], str, int]:
    selected = selected_feature_ids(features, target, profile)
    psram_mode = "quad" if profile == "representative-psram" else "none"
    psram_bytes = 8 * 1024 * 1024 if psram_mode == "quad" else 0
    heap_size = (
        4194304
        if psram_mode != "none"
        else (106496 if target == "esp32s3" else 262144)
    )
    lines = [
        "# Generated CI Build Context v1; do not edit.",
        "CONFIG_ESPTOOLPY_HEADER_FLASHSIZE_UPDATE=y",
        "CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y",
        'CONFIG_ESPTOOLPY_FLASHSIZE="8MB"',
        f'CONFIG_ESP32_MQUICKJS_PSRAM_MODE="{psram_mode}"',
        f"CONFIG_SPIRAM={'y' if psram_mode != 'none' else 'n'}",
        f"CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC={'n' if psram_mode != 'none' else 'y'}",
        f"CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC={'y' if psram_mode != 'none' else 'n'}",
        f"CONFIG_ESP32QJS_JS_HEAP_SIZE={heap_size}",
    ]
    native_features: list[str] = []
    for feature in features:
        feature_id = str(feature["id"])
        kconfig = str(feature["kconfig"])
        enabled = feature_id in selected
        lines.append(f"{kconfig}={'y' if enabled else 'n'}")
        if enabled:
            lines.extend(str(entry) for entry in feature.get("sdkconfig", []))
            if feature.get("public", True):
                native_features.append(feature_id)
    usb_serial_enabled = "usb_serial" in selected
    lines.extend(
        [
            f"CONFIG_ESP32QJS_ENABLE_REPL={'n' if usb_serial_enabled else 'y'}",
            "CONFIG_ESP32QJS_AUTORUN_INDEX_JS=y",
            "CONFIG_ESP32QJS_LITTLEFS_FORMAT_ON_MOUNT_FAIL=n",
        ]
    )
    return "\n".join(dict.fromkeys(lines)) + "\n", native_features, psram_mode, psram_bytes


def generate_context(target: str, profile: str, output: Path) -> Path:
    if target not in SUPPORTED_TARGETS:
        raise SystemExit(f"Unsupported CI target: {target}")
    if profile not in SUPPORTED_PROFILES:
        raise SystemExit(f"Unsupported CI profile: {profile}")
    if profile == "representative-psram" and target != "esp32s3":
        raise SystemExit("representative-psram is only supported for esp32s3")

    features = load_features()
    defaults, native_features, psram_mode, psram_bytes = sdkconfig_text(
        features, target, profile
    )
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    shutil.copy2(TEMPLATE_CONTEXT / "partitions.csv", output / "partitions.csv")
    shutil.copy2(TEMPLATE_CONTEXT / "profile-constants.inc", output / "profile-constants.inc")
    shutil.copy2(TEMPLATE_CONTEXT / "precompile.json", output / "precompile.json")
    destination_flash_data = output / "flash_data"
    if destination_flash_data.exists():
        shutil.rmtree(destination_flash_data)
    shutil.copytree(TEMPLATE_CONTEXT / "flash_data", destination_flash_data)
    (output / "sdkconfig.defaults").write_text(defaults, encoding="utf-8")
    manifest = {
        "schema": 1,
        "contextId": f"firmware-ci-{target}-{profile}",
        "firmwareCommit": "ci",
        "board": {
            "id": "firmware-ci",
            "label": f"Firmware CI {target} {profile}",
            "digest": "ci",
        },
        "hardware": {
            "mcu": target,
            "flashBytes": 8 * 1024 * 1024,
            "psramMode": psram_mode,
            "psramBytes": psram_bytes,
        },
        "nativeFeatures": native_features,
        "libraries": [],
        "digests": {},
    }
    (output / "manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
    )
    return output


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--target", choices=SUPPORTED_TARGETS, required=True)
    parser.add_argument("--profile", choices=SUPPORTED_PROFILES, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    print(generate_context(args.target, args.profile, args.output))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
