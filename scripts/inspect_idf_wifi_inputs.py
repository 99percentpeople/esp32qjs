#!/usr/bin/env python3
"""Capture W-00 source inputs using an existing ESP-IDF build's compiler.

This is the input collection step, not a public API coverage classifier. It
preserves conditional declarations and fields for review without adding any
planned API to the actual runtime manifest. Outputs belong under build/.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import subprocess


ROOT = Path(__file__).resolve().parents[1]
# Reviewed public headers in the pinned IDF. Private/remote replacement headers
# are not silently substituted if one of these inputs moves or disappears.
HEADERS = (
    "components/esp_wifi/include/esp_wifi.h",
    "components/esp_wifi/include/esp_wifi_he.h",
    "components/esp_wifi/include/esp_wifi_he_types.h",
    "components/esp_wifi/include/esp_wifi_types.h",
    "components/esp_wifi/include/esp_wifi_types_generic.h",
    "components/esp_wifi/include/local/esp_wifi_types_native.h",
    "components/esp_wifi/include/esp_now.h",
    "components/esp_wifi/wifi_apps/nan_app/include/esp_nan.h",
    "components/esp_wifi/include/esp_mesh.h",
    "components/esp_wifi/include/esp_smartconfig.h",
    "components/wpa_supplicant/esp_supplicant/include/esp_eap_client.h",
    "components/wpa_supplicant/esp_supplicant/include/esp_wps.h",
    "components/wpa_supplicant/esp_supplicant/include/esp_dpp.h",
    "components/wpa_supplicant/esp_supplicant/include/esp_rrm.h",
    "components/wpa_supplicant/esp_supplicant/include/esp_wnm.h",
    "components/esp_phy/include/esp_phy.h",
)
LINE_MARKER = re.compile(r'^#\s+\d+\s+"([^"]+)"')


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def compiler_arguments(entry: dict) -> list[str]:
    args = entry.get("arguments") or shlex.split(entry["command"])
    result = []
    skip_next = False
    for arg in args:
        if skip_next:
            skip_next = False
        elif arg in ("-o", "-MF", "-MT", "-MQ"):
            skip_next = True
        elif arg not in ("-c", "-MD", "-MMD", "-MP", entry["file"]):
            result.append(arg)
    return result + ["-E", "-dD", "-x", "c", "-"]


def capture(build: Path, output: Path) -> dict:
    description = json.loads((build / "project_description.json").read_text())
    if Path(description["project_path"]).resolve() != ROOT:
        raise ValueError("Build must belong to this firmware checkout")
    idf = Path(description["idf_path"]).resolve()
    commands_path = build / "compile_commands.json"
    commands = json.loads(commands_path.read_text())
    entry = next(item for item in commands if item["file"].endswith(
        "/wifi_radio/esp32_mquickjs_wifi_radio.c"))
    headers = {str((idf / name).resolve()): name for name in HEADERS}
    header_hashes = {name: digest((idf / name).read_bytes()) for name in HEADERS}
    source = "".join(f'#include "{path}"\n' for path in headers)
    result = subprocess.run(compiler_arguments(entry), cwd=entry["directory"],
                            input=source, text=True, capture_output=True, check=True)
    selected: dict[str, list[str]] = {name: [] for name in HEADERS}
    current = None
    seen = set()
    for line in result.stdout.splitlines(keepends=True):
        marker = LINE_MARKER.match(line)
        if marker:
            current = headers.get(marker[1])
            if current is not None:
                seen.add(current)
        elif current is not None:
            selected[current].append(line)
    if seen != set(HEADERS):
        raise ValueError(f"Preprocessor did not visit headers: {set(HEADERS) - seen}")
    # Keep the full translation unit too: the selected declarations can refer
    # to dependency typedefs. Do not parse them as an isolated C translation unit.
    output.mkdir(parents=True, exist_ok=True)
    (output / "translation-unit.i").write_text(result.stdout)
    for name, lines in selected.items():
        path = output / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("".join(lines))
    return {
        "target": description["target"],
        "idfRevision": subprocess.check_output(
            ["git", "-C", str(idf), "rev-parse", "HEAD"], text=True).strip(),
        "compileCommandsSha256": digest(commands_path.read_bytes()),
        "sdkconfigSha256": digest(Path(description["config_file"]).read_bytes()),
        "preprocessorSha256": digest(result.stdout.encode()),
        "headers": [{"path": name, "sourceSha256": header_hashes[name],
                     "conditionalTextSha256": digest("".join(selected[name]).encode())}
                    for name in HEADERS],
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, action="append", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    builds = [path.resolve() for path in args.build_dir]
    if len({path.name for path in builds}) != len(builds):
        parser.error("Build directory names must be distinct")
    manifest_path = ROOT / "api-manifest.json"
    manifest = json.loads(manifest_path.read_text())
    inputs = [capture(build, args.output / build.name) for build in builds]
    report = {
        "schema": 1,
        "stage": "inputs-only",
        "coverageReviewed": False,
        "actualManifestSha256": digest(manifest_path.read_bytes()),
        "actualWirelessFunctions": [item for item in manifest["functions"]
                                    if item["feature"] in (
                                        "CONFIG_ESP32_MQUICKJS_FEATURE_WIFI",
                                        "CONFIG_ESP32_MQUICKJS_FEATURE_WIFI_CSI",
                                        "CONFIG_ESP32_MQUICKJS_FEATURE_ESPNOW")],
        "builds": inputs,
    }
    (args.output / "inputs.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"Captured {len(HEADERS)} public headers for {len(inputs)} builds; "
          "symbol/field classification remains pending")


if __name__ == "__main__":
    main()
