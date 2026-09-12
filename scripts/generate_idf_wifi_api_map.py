#!/usr/bin/env python3
"""Check the reviewed Wi-Fi coverage map and generate target C inventories.

The actual JS manifest remains owned by generate_api_manifest.py. This tool
never infers JS types, ownership, or supported capabilities from C names.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

from inspect_idf_wifi_inputs import HEADERS, ROOT, capture

INVENTORY = ROOT / "docs/idf-wifi-api-inventory.json"
MAPPING = ROOT / "docs/idf-wifi-api-map.json"
RUNTIME = ROOT / "components/esp32_mquickjs/internal/esp32_mquickjs_wifi_coverage.inc"
TOKEN = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[A-Za-z_]\w*|[^\s]')
MARKER = re.compile(r'^#\s+\d+\s+"([^"]+)"')
DISPOSITIONS = {"mapped", "framework-owned", "build-time",
                "removed-or-deprecated", "private-excluded", "target-unsupported"}


def blank(text: str) -> str:
    return "\n" * text.count("\n")


def parser_source(source: str) -> str:
    """Normalize compiler syntax for declarations, preserving #line coordinates.

    GCC attributes/asm and inline bodies are outside this semantic inventory;
    original public-header hashes guard their changes separately. This is not
    an ABI size/offset calculator. Unknown declaration syntax fails closed.
    """
    source = "\n".join(line if not line.startswith("#") or MARKER.match(line)
                       else "" for line in source.splitlines())
    tokens = list(TOKEN.finditer(source))
    edits = []
    index = 0
    while index < len(tokens):
        token = tokens[index]
        if token[0] in ("__attribute__", "__attribute", "__asm__", "asm"):
            end = index + 1
            while tokens[end][0] in ("volatile", "__volatile__", "inline", "goto"):
                end += 1
            if tokens[end][0] != "(":
                raise ValueError(f"Unsupported compiler annotation: {token[0]}")
            depth = 1
            end += 1
            while depth:
                if tokens[end][0] == "(":
                    depth += 1
                elif tokens[end][0] == ")":
                    depth -= 1
                end += 1
            edits.append((token.start(), tokens[end - 1].end()))
            index = end
        else:
            index += 1
    for start, end in reversed(edits):
        source = source[:start] + blank(source[start:end]) + source[end:]
    source = re.sub(r"\b__extension__\b", "", source)
    source = re.sub(r"\b__inline\b", "inline", source)
    source = re.sub(r"\bthread_local\b", "_Thread_local", source)
    # C23's standard nullptr_t appears in GCC's dependency stddef.h. None of
    # the reviewed wireless declarations use it. Keep it parseable as a pointer.
    source = source.replace("__typeof__(nullptr)", "void *")
    tokens = list(TOKEN.finditer(source))
    depth = 0
    start = None
    edits = []
    for index, token in enumerate(tokens):
        if token[0] == "{":
            if depth == 0 and index and tokens[index - 1][0] == ")":
                start = token.start()
            depth += 1
        elif token[0] == "}":
            depth -= 1
            if depth == 0 and start is not None:
                edits.append((start, token.end()))
                start = None
    for start, end in reversed(edits):
        source = source[:start] + ";" + blank(source[start:end]) + source[end:]
    prelude = "typedef int __builtin_va_list;\n"
    # GNU23 has builtin bool; earlier modes already expand stdbool.h to _Bool.
    if re.search(r"\bbool\b", source):
        prelude += "typedef _Bool bool;\n"
    return prelude + source


def header_name(filename: str) -> str | None:
    return next((name for name in HEADERS if filename.endswith("/" + name)), None)


def extract(source: str) -> dict:
    from pycparser import c_ast, c_generator, c_parser

    generator = c_generator.CGenerator()
    try:
        tree = c_parser.CParser().parse(parser_source(source))
    except c_parser.ParseError as error:
        raise ValueError(f"Unsupported public-header translation unit: {error}") from error
    symbols = {}

    class Fields(c_ast.NodeVisitor):
        def __init__(self):
            self.fields = []
            self.prefix = ""

        def visit_Struct(self, node):
            for index, declaration in enumerate(node.decls or []):
                path = self.prefix + (declaration.name or f"<anonymous:{index}>")
                self.fields.append({"path": path,
                                    "declaration": generator.visit(declaration)})
                previous = self.prefix
                self.prefix = path + "."
                self.visit(declaration.type)
                self.prefix = previous

        visit_Union = visit_Struct

    for node in tree.ext:
        header = header_name(node.coord.file) if node.coord else None
        if header is None:
            continue
        class EnumValues(c_ast.NodeVisitor):
            def visit_Enum(self, enum):
                for index, value in enumerate(enum.values.enumerators if enum.values else []):
                    symbols[f"{header}::{value.name}"] = {
                        "kind": "enum-value", "declaration": generator.visit(value),
                        "ordinalInEnum": index, "fields": []}
        EnumValues().visit(node)
        if isinstance(node, c_ast.Decl) and isinstance(node.type, c_ast.Enum) and not node.type.name:
            continue
        if isinstance(node, c_ast.Typedef):
            kind = "typedef"
        elif isinstance(node, c_ast.Decl) and isinstance(node.type, c_ast.FuncDecl):
            kind = "function"
        elif isinstance(node, c_ast.Decl) and node.name:
            kind = "variable"
        elif isinstance(node, c_ast.Decl) and isinstance(node.type, (c_ast.Enum, c_ast.Struct, c_ast.Union)) and node.type.name:
            kind = "tag"
        else:
            raise ValueError(f"Unclassified public declaration: {node.coord}")
        name = node.name if kind != "tag" else type(node.type).__name__.lower() + " " + node.type.name
        key = f"{header}::{name}"
        fields = Fields()
        fields.visit(node)
        value = {"kind": kind, "declaration": generator.visit(node),
                 "fields": fields.fields}
        if key in symbols and symbols[key] != value:
            raise ValueError(f"Conflicting public declaration: {key}")
        symbols[key] = value
    current = None
    for line in source.splitlines():
        marker = MARKER.match(line)
        if marker:
            current = header_name(marker[1])
        elif current and (macro := re.match(r"#define\s+(\w+)(.*)", line)):
            symbols[f"{current}::{macro[1]}"] = {
                "kind": "macro", "declaration": macro[1] + macro[2].rstrip(), "fields": []}
    return dict(sorted(symbols.items()))


def build_variant(value: str) -> tuple[str, Path]:
    profile, separator, directory = value.partition("=")
    if not separator or not directory or not re.fullmatch(r"[a-z0-9][a-z0-9-]*", profile):
        raise argparse.ArgumentTypeError("Expected PROFILE=BUILD_DIR with a lowercase profile name")
    return profile, Path(directory)


def collect(builds: list[tuple[str, Path]]) -> dict:
    result = {"schema": 1, "sourceLicense": "ESP-IDF public headers: Apache-2.0 (Espressif)",
              "headers": {}, "variants": {}}
    for profile, build in builds:
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            metadata = capture(build.resolve(), output)
            if result.get("idfRevision", metadata["idfRevision"]) != metadata["idfRevision"]:
                raise ValueError("Targets must use the same reviewed IDF revision")
            result["idfRevision"] = metadata["idfRevision"]
            headers = {item["path"]: item["sourceSha256"] for item in metadata["headers"]}
            if result["headers"] and result["headers"] != headers:
                raise ValueError("Targets must use the same reviewed public headers")
            result["headers"] = headers
            target = metadata["target"]
            key = f"{target}/{profile}"
            if key in result["variants"]:
                raise ValueError(f"Duplicate variant: {key}")
            result["variants"][key] = {
                "target": target, "profile": profile,
                "symbols": extract((output / "translation-unit.i").read_text())}
    return result


def validate(inventory: dict, mapping: dict, actual: dict) -> None:
    if inventory.get("schema") != 1 or mapping.get("schema") != 1:
        raise ValueError("Expected the sole v1 inventory and mapping")
    if not inventory.get("variants"):
        raise ValueError("Expected at least one reviewed build variant")
    for key, variant in inventory["variants"].items():
        if key != f"{variant['target']}/{variant['profile']}":
            raise ValueError(f"Invalid variant identity: {key}")
    keys = set().union(*(set(item["symbols"]) for item in inventory["variants"].values()))
    entries = mapping["symbols"]
    if keys != set(entries):
        raise ValueError(f"Unclassified symbols: {sorted(keys - set(entries))}; "
                         f"stale mappings: {sorted(set(entries) - keys)}")
    actual_paths = {item["qualifiedName"] for item in actual["functions"]}
    for key, override in entries.items():
        task = override.get("task")
        if task not in mapping["tasks"]:
            raise ValueError(f"Missing task contract: {key}")
        entry = {**mapping["tasks"][task], **override}
        if entry.get("disposition") not in DISPOSITIONS:
            raise ValueError(f"Missing disposition: {key}")
        if entry.get("implementation") not in ("planned", "in-progress", "implemented"):
            raise ValueError(f"Missing implementation status: {key}")
        if entry.get("contract") not in ("review-required", "contract-pending", "reviewed"):
            raise ValueError(f"Missing contract status: {key}")
        if entry["disposition"] == "target-unsupported" and not entry.get("evidence"):
            raise ValueError(f"Unsupported target requires capability evidence: {key}")
        for field in ("ownershipContract", "completionContract", "validation"):
            if not entry.get(field):
                raise ValueError(f"Missing {field}: {key}")
        if not {"host", "build", "hardware"} <= entry["validation"].keys():
            raise ValueError(f"Incomplete validation status: {key}")
        js_path = entry.get("jsPath")
        if entry["implementation"] == "planned" and js_path in actual_paths:
            raise ValueError(f"Planned API already advertised as actual: {js_path}")
        if (entry["disposition"] == "mapped" and entry["contract"] == "reviewed"
                and entry["implementation"] in ("in-progress", "implemented")
                and js_path and js_path not in actual_paths):
            raise ValueError(f"Reviewed mapping has no actual registered API: {key}: {js_path}")
        if entry["implementation"] == "implemented" and entry["disposition"] == "mapped":
            if (entry["contract"] != "reviewed" or js_path not in actual_paths
                    or not entry.get("evidence") or entry.get("fieldCoverage") != "reviewed"
                    or entry["validation"].get("host") != "passed"):
                raise ValueError(f"Implemented mapping lacks reviewed actual API evidence: {key}")


def check_headers(inventory: dict, idf: Path) -> None:
    if "idfRevision" in inventory:
        revision = subprocess.check_output(
            ["git", "-C", str(idf), "rev-parse", "HEAD"], text=True).strip()
        if revision != inventory["idfRevision"]:
            raise ValueError("IDF revision changed; regenerate and review coverage")
    for name in HEADERS:
        digest = hashlib.sha256((idf / name).read_bytes()).hexdigest()
        if inventory["headers"].get(name) != digest:
            raise ValueError(f"Public header changed; regenerate and review symbol/field coverage: {name}")


def check_generated(expected: dict, generated: dict) -> None:
    if generated["headers"] != expected["headers"]:
        raise ValueError("Public header hashes changed; review required")
    if generated.get("idfRevision") != expected.get("idfRevision"):
        raise ValueError("IDF revision changed; review required")
    for variant, symbols in generated["variants"].items():
        if symbols != expected["variants"].get(variant):
            raise ValueError(f"Conditional declarations/fields changed: {variant}")


def runtime_summary(inventory: dict, mapping: dict, actual: dict) -> str:
    """Flash-resident summaries of the reviewed union, never target capabilities.

    Resolve task defaults exactly as validate() does. A registered jsPath is
    counted separately from reviewed implementation/contract/validation state.
    """
    dispositions = ("mapped", "framework-owned", "build-time", "removed-or-deprecated",
                    "private-excluded", "target-unsupported")
    implementations = ("planned", "in-progress", "implemented")
    contracts = ("review-required", "contract-pending", "reviewed")
    fields = ("symbols", "functionSymbols", "registeredSymbols", "mapped", "frameworkOwned",
              "buildTime", "removedOrDeprecated", "privateExcluded", "targetUnsupported",
              "planned", "inProgress", "implemented", "reviewRequired", "contractPending", "reviewed")
    paths = {item["qualifiedName"] for item in actual["functions"]}
    kinds = {}
    for variant in inventory["variants"].values():
        for key, declaration in variant["symbols"].items():
            if key in kinds and kinds[key] != declaration["kind"]:
                raise ValueError(f"Variant-dependent symbol kind requires coverage review: {key}")
            kinds[key] = declaration["kind"]

    def counts(keys):
        total = [0] * len(fields)
        for key in keys:
            override = mapping["symbols"][key]
            entry = {**mapping["tasks"][override["task"]], **override}
            total[0] += 1
            total[1] += kinds[key] == "function"
            total[2] += entry.get("jsPath") in paths
            total[3 + dispositions.index(entry["disposition"])] += 1
            total[9 + implementations.index(entry["implementation"])] += 1
            total[12 + contracts.index(entry["contract"])] += 1
        if any(value > 0xffffffff for value in total):
            raise ValueError("Runtime coverage count exceeds uint32_t")
        return "{" + ", ".join(str(value) + "U" for value in total) + "}"

    entries = mapping["symbols"]
    lines = ["/* Generated by scripts/generate_idf_wifi_api_map.py --write-runtime.",
             " * Reviewed inventory union; counts are not runtime or hardware qualification. */",
             f'#define WIFI_COVERAGE_IDF_REVISION {json.dumps(inventory["idfRevision"])}',
             f'#define WIFI_COVERAGE_INVENTORY_SHA256 "{hashlib.sha256(INVENTORY.read_bytes()).hexdigest()}"',
             f'#define WIFI_COVERAGE_MAP_SHA256 "{hashlib.sha256(MAPPING.read_bytes()).hexdigest()}"',
             f'#define WIFI_COVERAGE_MANIFEST_SHA256 "{hashlib.sha256((ROOT / "api-manifest.json").read_bytes()).hexdigest()}"',
             "static const char *const s_wifi_coverage_fields[] = {" +
             ", ".join(json.dumps(field) for field in fields) + "};",
             "typedef struct { const char *name; uint32_t counts[15]; } wifi_coverage_row_t;",
             "static const uint32_t s_wifi_coverage_total[] = " + counts(entries) + ";"]
    groups = {
        "headers": {header: [key for key in entries if key.split("::", 1)[0] == header]
                    for header in sorted(inventory["headers"])},
        "tasks": {task: [key for key, value in entries.items() if value["task"] == task]
                  for task in sorted(mapping["tasks"])},
        "variants": {variant: list(value["symbols"])
                     for variant, value in sorted(inventory["variants"].items())},
    }
    for group, rows in groups.items():
        lines.append(f"static const wifi_coverage_row_t s_wifi_coverage_{group}[] = {{")
        for name, keys in rows.items():
            lines.append("    {" + json.dumps(name) + ", " + counts(keys) + "},")
        lines.append("};")
    lines.append("static const struct { const char *header; const char *reason; } s_wifi_coverage_gaps[] = {")
    for header, reason in sorted(mapping.get("unexpandedHeaders", {}).items()):
        lines.append("    {" + json.dumps(header) + ", " + json.dumps(reason, ensure_ascii=False) + "},")
    # Sentinel permits an empty gap list without relying on zero-length arrays.
    lines.extend(["    {NULL, NULL},", "};", ""])
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true")
    mode.add_argument("--write", action="store_true")
    mode.add_argument("--write-runtime", action="store_true",
                      help="Generate the runtime summary from the reviewed inventory/map and current API manifest")
    mode.add_argument("--check-headers", type=Path, metavar="IDF_PATH")
    parser.add_argument("--build-dir", type=build_variant, metavar="PROFILE=BUILD_DIR",
                        action="append", default=[])
    args = parser.parse_args()
    try:
        expected = json.loads(INVENTORY.read_text())
        mapping = json.loads(MAPPING.read_text())
        actual = json.loads((ROOT / "api-manifest.json").read_text())
        # During regeneration the reviewed mapping already describes the new
        # headers; requiring it to match the old inventory makes adding or
        # removing a symbol impossible. Validate the newly collected set below.
        if not args.write:
            validate(expected, mapping, actual)
        if args.check_headers:
            check_headers(expected, args.check_headers)
        if args.build_dir:
            generated = collect(args.build_dir)
            if args.write:
                if not set(expected["variants"]) <= set(generated["variants"]):
                    raise ValueError("Regeneration requires all recorded variants")
                validate(generated, mapping, actual)
                INVENTORY.write_text(json.dumps(generated, indent=2, sort_keys=True) + "\n")
            else:
                combined = {**generated, "variants": {**expected["variants"], **generated["variants"]}}
                validate(combined, mapping, actual)
                check_generated(expected, generated)
        elif args.write:
            raise ValueError("--write requires actual target build directories")
        if args.write_runtime:
            if args.build_dir:
                raise ValueError("--write-runtime uses reviewed inputs; it does not recollect build variants")
            RUNTIME.write_text(runtime_summary(expected, mapping, actual), encoding="utf-8")
        elif not args.write and RUNTIME.read_text(encoding="utf-8") != runtime_summary(expected, mapping, actual):
            raise ValueError("Runtime Wi-Fi coverage is stale; run generate_idf_wifi_api_map.py --write-runtime")
        print(f"Wi-Fi coverage map checked: {len(mapping['symbols'])} classified entries; "
              "classification does not imply implemented or hardware-qualified")
        return 0
    except (ValueError, OSError) as error:
        print(str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
