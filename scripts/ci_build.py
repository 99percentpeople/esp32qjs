#!/usr/bin/env python3
"""Build one ESP32QJS target/profile pair inside the ESP-IDF CI image."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
from pathlib import Path

from prepare_ci_build_context import (
    ROOT,
    SUPPORTED_PROFILES,
    SUPPORTED_TARGETS,
    generate_context,
)
from build_tools.build import esptool_environment
from codegen.idf_wifi_api_map import INVENTORY, check_generated, check_headers, collect


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--target", choices=SUPPORTED_TARGETS, required=True)
    parser.add_argument("--profile", choices=SUPPORTED_PROFILES, required=True)
    args = parser.parse_args()

    # Guard all reviewed public headers, including APIs hidden by this profile.
    # Changed SDK symbols/fields require an explicit coverage review first.
    inventory = json.loads(INVENTORY.read_text())
    check_headers(inventory, Path(os.environ["IDF_PATH"]))

    slug = f"{args.target}-{args.profile}"
    build_dir = ROOT / "build" / "ci" / slug
    context_dir = ROOT / "build" / "ci-contexts" / slug
    generate_context(args.target, args.profile, context_dir)
    sdkconfig = build_dir / f"sdkconfig.{slug}"
    sdkconfig_old = build_dir / f"sdkconfig.{slug}.old"
    sdkconfig.unlink(missing_ok=True)
    sdkconfig_old.unlink(missing_ok=True)
    environment = esptool_environment()
    idf_py = Path(os.environ["IDF_PATH"]) / "tools" / "idf.py"
    defaults = ";".join(
        (
            str(ROOT / "configs" / "mcus" / args.target / "sdkconfig.defaults"),
            str(context_dir / "sdkconfig.defaults"),
        )
    )
    command = [
        str(idf_py),
        "-B",
        str(build_dir),
        f"-DIDF_TARGET={args.target}",
        f"-DSDKCONFIG={sdkconfig}",
        f"-DESP32QJS_MCU={args.target}",
        f"-DESP32QJS_MCU_SDKCONFIG_DEFAULTS={ROOT / 'configs' / 'mcus' / args.target / 'sdkconfig.defaults'}",
        f"-DESP32QJS_BUILD_CONTEXT_DIR={context_dir}",
        f"-DSDKCONFIG_DEFAULTS={defaults}",
        "build",
    ]
    subprocess.run(command, cwd=ROOT, check=True, env=environment)
    variant = f"{args.target}/{args.profile}"
    if variant in inventory["variants"]:
        check_generated(inventory, collect([(args.profile, build_dir)]))
        print(f"Wi-Fi conditional declarations/fields checked: {variant}")
    else:
        print(f"Wi-Fi public headers checked; no semantic baseline recorded for {variant}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
