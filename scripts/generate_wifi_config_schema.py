#!/usr/bin/env python3
"""Validate reviewed SDK config leaves and render the pending driver contract.

Uses the existing preprocessed target inventories, never an assumed C layout.
This is a contract artifact generator, not an API registrar or capability probe.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
HEADER = "components/esp_wifi/include/esp_wifi_types_generic.h"
INVENTORY = ROOT / "docs/idf-wifi-api-inventory.json"
MAPPING = ROOT / "docs/wifi-driver-config-fields.json"
OUTPUT = ROOT / "docs/generated/wifi-driver-config-schema.md"
FIELD_HEADER = ROOT / "components/esp32_mquickjs/internal/esp32_mquickjs_wifi_config_fields.h"
OBSERVATION_HEADER = ROOT / "components/esp32_mquickjs/internal/esp32_mquickjs_wifi_config_observations.inc"
TYPES = ROOT / "types/esp32qjs-c-api.d.ts"
READBACK_BEGIN = "  // BEGIN GENERATED WIFI DRIVER SNAPSHOTS"
READBACK_END = "  // END GENERATED WIFI DRIVER SNAPSHOTS"
STRUCTURES = {"wifi_sta_config_t": "WiFiStationDriverConfig",
              "wifi_ap_config_t": "WiFiAccessPointDriverConfig"}


def leaves(symbols: dict, name: str, chain: tuple[str, ...] = ()) -> dict[str, str]:
    """Resolve named nested struct typedefs, including threshold/PMF/BSS idle."""
    if name in chain:
        raise ValueError(f"Recursive config structure: {name}")
    key = HEADER + "::" + name
    if key not in symbols:
        raise ValueError(f"Missing config structure: {name}")
    result = {}
    fields = symbols[key]["fields"]
    if not fields:
        raise ValueError(f"Config type has no fields: {name}")
    for field in fields:
        path, declaration = field["path"], field["declaration"]
        # Unsupported pointers/anonymous layouts must receive explicit review.
        match = re.fullmatch(r"(\w+) (\w+)(?:\[(\d+)\]| : (\d+))?", declaration)
        if not match or match[2] != path:
            raise ValueError(f"Unreviewed field syntax: {name}.{path}: {declaration}")
        nested = symbols.get(HEADER + "::" + match[1])
        if nested and nested["fields"]:
            if match[3] or match[4]:
                raise ValueError(f"Unreviewed nested array/bitfield: {name}.{path}")
            for child, value in leaves(symbols, match[1], (*chain, name)).items():
                if path + "." + child in result:
                    raise ValueError(f"Duplicate field: {name}.{path}.{child}")
                result[path + "." + child] = value
        else:
            if path in result:
                raise ValueError(f"Duplicate field: {name}.{path}")
            result[path] = declaration
    return result


def validate(inventory: dict, mapping: dict, idf_path: Path | None = None) -> dict:
    if inventory.get("schema") != 1 or mapping.get("schema") != 1:
        raise ValueError("Only schema 1 is supported")
    if mapping.get("idfRevision") != inventory.get("idfRevision"):
        raise ValueError("SDK revision differs from reviewed field mapping")
    if mapping.get("headerSha256") != inventory["headers"][HEADER]:
        raise ValueError("SDK header differs from reviewed field mapping")
    if idf_path is not None:
        actual = hashlib.sha256((idf_path / HEADER).read_bytes()).hexdigest()
        if actual != mapping["headerSha256"]:
            raise ValueError("Live SDK config header changed; recapture and review inventory")
    if set(mapping.get("structures", {})) != set(STRUCTURES):
        raise ValueError("Both Station and AP config mappings are required")
    if not inventory.get("variants"):
        raise ValueError("No target inventories")
    evidence = {}
    for name, structure in mapping["structures"].items():
        expected = structure["fields"]
        paths = set()
        if structure["jsType"] != STRUCTURES[name]:
            raise ValueError(f"Invalid declaration name: {name}")
        if structure.get("readbackType") != STRUCTURES[name] + "Snapshot":
            raise ValueError(f"Invalid snapshot declaration name: {name}")
        observed_paths = set()
        for path, entry in expected.items():
            disposition = entry["disposition"]
            if disposition not in {"input", "derived", "deprecated", "reserved"}:
                raise ValueError(f"Unclassified field: {name}.{path}")
            if not all(isinstance(entry.get(k), str) and entry[k] for k in ["declaration", "constraints", "unit", "gate"]):
                raise ValueError(f"Incomplete field review: {name}.{path}")
            if not isinstance(entry.get("secret"), bool):
                raise ValueError(f"Missing secret policy: {name}.{path}")
            if path in {"password", "sae_h2e_identifier"} and not entry["secret"]:
                raise ValueError(f"Credential field lost secret policy: {name}.{path}")
            if not isinstance(entry.get("required"), bool):
                raise ValueError(f"Missing input presence policy: {name}.{path}")
            if disposition == "input":
                js = entry.get("jsPath", "")
                if not re.fullmatch(r"[a-zA-Z]\w*", js) or js in paths:
                    raise ValueError(f"Invalid/duplicate JS field: {name}.{path}")
                paths.add(js)
                if not entry.get("tsType") or entry.get("direction") != "write":
                    raise ValueError(f"Missing input type/direction: {name}.{path}")
            elif entry.get("jsPath") is not None or entry.get("direction") != "none":
                raise ValueError(f"Non-input field exported: {name}.{path}")
            observed = entry.get("readback")
            if disposition == "reserved":
                if observed is not None:
                    raise ValueError(f"Reserved field has readback: {name}.{path}")
                continue
            if not isinstance(observed, dict) or observed.get("codec") not in {"number", "boolean", "enum", "bytes"}:
                raise ValueError(f"Missing readback codec: {name}.{path}")
            js = observed.get("jsPath", "")
            if not re.fullmatch(r"[a-zA-Z]\w*", js) or js in observed_paths or not observed.get("tsType"):
                raise ValueError(f"Invalid readback property: {name}.{path}")
            observed_paths.add(js)
            if observed["codec"] == "enum" and observed.get("enum") not in {"scan", "sort", "auth", "pwe", "pk", "cipher"}:
                raise ValueError(f"Unreviewed readback enum: {name}.{path}")
            if entry["secret"] and (observed["codec"] != "bytes" or observed["tsType"] != "number[] | null"):
                raise ValueError(f"Secret readback lost redaction: {name}.{path}")
        declaration = {p: e["declaration"] for p, e in expected.items()}
        evidence[name] = {}
        for variant, data in inventory["variants"].items():
            actual = leaves(data["symbols"], name)
            if actual != declaration:
                added = sorted(actual.keys() - declaration.keys())
                removed = sorted(declaration.keys() - actual.keys())
                changed = sorted(k for k in actual.keys() & declaration.keys() if actual[k] != declaration[k])
                raise ValueError(f"{variant}/{name}: added={added} removed={removed} changed={changed}")
            evidence[name][variant] = len(actual)
    return evidence


def render(mapping: dict, evidence: dict) -> str:
    output = ["# Wi-Fi driver configuration schema", "",
        "Generated by `scripts/generate_wifi_config_schema.py`; edit the checked mapping.", "",
        "Complete SDK field inventory and proposal. `wifi.configure` exposes the implemented",
        "subset in `types/esp32qjs-c-api.d.ts`; additional authentication enum values below",
        "remain **contract-pending** as inputs. Driver snapshots preserve actual SDK values",
        "and enum IDs; explicit secret readback is separately build-gated and defaults off.",
        "`ByteSource` uses the existing ESP32QJS array-like/ByteView input, not typed arrays.",
        "SDK field presence does not prove a feature is enabled or RF-qualified.", "",
        f"SDK revision: `{mapping['idfRevision']}`. All recorded variants are checked.", "",
        "Configuration inputs require stopped, exclusive lifecycle admission. Parse and",
        "validate all fields before mutation; reject unsupported requests, never weaken",
        "security or silently ignore fields. Missing fields use explicit constructor",
        "defaults, not a patch over secret driver state. Buffers must be captured and",
        "credential storage cleared on every exit. Configure returns WiFiStatus, without credentials.", ""]
    for name, structure in mapping["structures"].items():
        entries = structure["fields"]
        public = sum(e["disposition"] != "reserved" for e in entries.values())
        output += [f"## `{name}` → `{structure['jsType']}`", "",
            f"{public} public leaves; {len(entries) - public} reserved leaves explicitly excluded.", "",
            "```ts", f"interface {structure['jsType']} {{"]
        for path, entry in entries.items():
            if entry["disposition"] != "input":
                continue
            note = f"{path}; unit: {entry['unit']}; {entry['constraints']}"
            if entry["secret"]:
                note += " Secret: never status/error/log or default readback."
            output += [f"  /** {note} */", f"  {entry['jsPath']}{'' if entry.get('required') else '?'}: {entry['tsType']};"]
        output += ["}", "```", "", "| SDK field | Disposition | Input | Readback | Gate / constraints |",
            "| --- | --- | --- | --- | --- |"]
        for path, entry in entries.items():
            observed = entry.get("readback")
            output.append(f"| `{path}` | {entry['disposition']} | {entry.get('jsPath') or '—'} | {observed['jsPath'] if observed else '—'} | {entry['gate']}; {entry['constraints']} |")
        output += ["", "Checked layouts: " + ", ".join(f"`{v}` ({n} leaves)" for v, n in evidence[name].items()) + ".", ""]
    return "\n".join(output)


def render_fields(mapping: dict) -> str:
    fields = ["/* Generated by scripts/generate_wifi_config_schema.py. */", "#pragma once", ""]
    for name, structure in mapping["structures"].items():
        symbol = "wifi_station_driver_keys" if name == "wifi_sta_config_t" else "wifi_ap_driver_keys"
        fields.append(f"static const char *const {symbol}[] = {{")
        fields.extend("    " + json.dumps(e["jsPath"]) + "," for e in structure["fields"].values()
                      if e["disposition"] == "input")
        fields.extend(["};", ""])
    return "\n".join(fields)


def render_observations(mapping: dict) -> str:
    output = ["/* Generated by scripts/generate_wifi_config_schema.py. All non-reserved SDK leaves. */"]
    for name, record in mapping["structures"].items():
        suffix = "STA" if name == "wifi_sta_config_t" else "AP"
        output.append(f"#define WIFI_DRIVER_OBSERVE_{suffix}(NUMBER, BOOLEAN, ENUM, BYTES, SECRET) \\")
        rows = []
        for path, entry in record["fields"].items():
            observed = entry.get("readback")
            if observed is None:
                continue
            codec = "SECRET" if entry["secret"] else observed["codec"].upper()
            extra = ", " + observed["enum"] if codec == "ENUM" else ""
            rows.append(f"    {codec}(\"{observed['jsPath']}\", {path}{extra});")
        output.append(" \\\n".join(rows))
    return "\n".join(output) + "\n"


def render_readback_types(mapping: dict) -> str:
    output = [READBACK_BEGIN,
        "  /** SDK enum ID is preserved even when its name is unknown or reserved. */",
        "  interface WiFiDriverEnumValue { id: number; name: string | null; }"]
    for name, record in mapping["structures"].items():
        sta = name == "wifi_sta_config_t"
        output += ["  /** Independent driver observation; not a writable configuration or a capability declaration. */",
            "  interface " + record["readbackType"] + " {",
            '    interface: "' + ("station" if sta else "access-point") + '";',
            "    secretsIncluded: boolean;",
            "    /** Null if SSID bytes are not valid UTF-8. */", "    ssid: string | null;",
            "    /** Null when redacted or when secret bytes are not valid UTF-8. */", "    password: string | null;",
            '    pmf: "disabled" | "optional" | "required" | null;']
        if sta:
            output += ["    bssid: string;", "    saeH2eIdentifier: string | null;"]
        for entry in record["fields"].values():
            observed = entry.get("readback")
            if observed is None:
                continue
            if entry["secret"]:
                output.append("    /** Null unless secret readback was explicitly enabled and requested. */")
            output.append(f"    {observed['jsPath']}: {observed['tsType']};")
        output += ["  }"]
    output.append(READBACK_END)
    return "\n".join(output)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--idf-path", type=Path, default=os.environ.get("IDF_PATH"))
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    try:
        inventory = json.loads(INVENTORY.read_text())
        mapping = json.loads(MAPPING.read_text())
        evidence = validate(inventory, mapping, args.idf_path)
        expected = render(mapping, evidence)
        types = TYPES.read_text()
        if types.count(READBACK_BEGIN) != 1 or types.count(READBACK_END) != 1:
            raise ValueError("Snapshot type markers must exist exactly once")
        begin, end = types.index(READBACK_BEGIN), types.index(READBACK_END) + len(READBACK_END)
        outputs = {OUTPUT: expected, FIELD_HEADER: render_fields(mapping),
            OBSERVATION_HEADER: render_observations(mapping),
            TYPES: types[:begin] + render_readback_types(mapping) + types[end:]}
        if args.check:
            for path, content in outputs.items():
                if not path.exists() or path.read_text() != content:
                    raise ValueError(f"Generated Wi-Fi config schema is stale: {path.name}")
        else:
            for path, content in outputs.items():
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(content)
    except (ValueError, KeyError, OSError) as error:
        print(f"Wi-Fi config schema: {error}", file=sys.stderr)
        return 1
    counts = ", ".join(f"{name}={sum(e['disposition'] != 'reserved' for e in s['fields'].values())}"
                       for name, s in mapping["structures"].items())
    print("Wi-Fi config schema: passed public leaves " + counts + "; "
          + ("live SDK header verified" if args.idf_path else "recorded inventory only"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
