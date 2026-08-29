#!/usr/bin/env python3
"""Generate feature catalog and stability tables from runtime-features.json."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CATALOG_PATH = ROOT / "components" / "esp32_mquickjs" / "runtime-features.json"
STABILITY_PATH = ROOT / "docs" / "feature-stability.json"
README_PATH = ROOT / "README.md"
STABILITY_DOC_PATH = ROOT / "docs" / "api-stability-plan.md"
README_MARKER = "FEATURE CATALOG"
STABILITY_MARKER = "FEATURE STABILITY"


def public_features() -> list[dict[str, object]]:
    catalog = json.loads(CATALOG_PATH.read_text(encoding="utf-8"))
    if catalog.get("schema") != 1 or not isinstance(catalog.get("features"), list):
        raise SystemExit(f"Invalid feature catalog: {CATALOG_PATH}")
    return [
        feature
        for feature in catalog["features"]
        if feature.get("public", True)
    ]


def feature_catalog_table(features: list[dict[str, object]]) -> str:
    lines = [
        "| Feature | Capability | Targets | Requires |",
        "| --- | --- | --- | --- |",
    ]
    for feature in features:
        targets = ", ".join(str(value) for value in feature.get("targets", [])) or "none"
        requires = ", ".join(
            f"`{value}`" for value in feature.get("requires", [])
            if value != "wifi_radio"
        ) or "none"
        lines.append(
            f"| `{feature['id']}` | {feature['label']} | {targets} | {requires} |"
        )
    return "\n".join(lines)


def feature_stability_table(features: list[dict[str, object]]) -> str:
    data = json.loads(STABILITY_PATH.read_text(encoding="utf-8"))
    if data.get("schema") != 1 or not isinstance(data.get("areas"), list):
        raise SystemExit(f"Invalid feature stability data: {STABILITY_PATH}")
    public_ids = {str(feature["id"]) for feature in features}
    covered: list[str] = []
    lines: list[str] = []
    for area in data["areas"]:
        area_features = [str(value) for value in area.get("features", [])]
        covered.extend(area_features)
        lines.append(
            f"| {area['area']} | {area['status']} | {area['evidence']} |"
        )
    if len(covered) != len(set(covered)) or set(covered) != public_ids:
        missing = sorted(public_ids - set(covered))
        extra = sorted(set(covered) - public_ids)
        raise SystemExit(
            f"Feature stability coverage mismatch: missing={missing}, extra={extra}"
        )
    return "\n".join(lines)


def replace_generated_block(text: str, marker: str, body: str) -> str:
    begin = f"<!-- BEGIN GENERATED {marker} -->"
    end = f"<!-- END GENERATED {marker} -->"
    if text.count(begin) != 1 or text.count(end) != 1:
        raise SystemExit(f"Expected one generated {marker} block")
    prefix, remainder = text.split(begin, 1)
    _, suffix = remainder.split(end, 1)
    return f"{prefix}{begin}\n{body}\n{end}{suffix}"


def render() -> dict[Path, str]:
    features = public_features()
    return {
        README_PATH: replace_generated_block(
            README_PATH.read_text(encoding="utf-8"),
            README_MARKER,
            feature_catalog_table(features),
        ),
        STABILITY_DOC_PATH: replace_generated_block(
            STABILITY_DOC_PATH.read_text(encoding="utf-8"),
            STABILITY_MARKER,
            feature_stability_table(features),
        ),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    rendered = render()
    stale = [path for path, content in rendered.items() if path.read_text(encoding="utf-8") != content]
    if args.check:
        if stale:
            print(
                "Feature documentation is stale: "
                + ", ".join(str(path.relative_to(ROOT)) for path in stale),
                file=sys.stderr,
            )
            return 1
        print(f"Feature documentation is current: features={len(public_features())}")
        return 0
    for path, content in rendered.items():
        path.write_text(content, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
