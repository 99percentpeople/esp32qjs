"""Exercise the production inventory parser and coverage gates."""
from tests.support.paths import ROOT as TEST_ROOT
import copy
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = TEST_ROOT
sys.path.insert(0, str(ROOT / "scripts"))
import generate_idf_wifi_api_map as coverage

HEADER = "components/esp_wifi/include/esp_wifi.h"
PREFIX = f'# 1 "/sdk/{HEADER}"\n'


def fixture():
    symbols = coverage.extract(PREFIX + "int esp_wifi_example(void);")
    key = next(iter(symbols))
    inventory = {"schema": 1, "headers": {}, "variants": {"esp32c5/representative": {"target": "esp32c5", "profile": "representative", "symbols": symbols}}}
    mapping = {"schema": 1, "tasks": {"W-07": {
        "disposition": "mapped", "implementation": "planned",
        "contract": "review-required", "ownershipContract": "Radio lane",
        "completionContract": "native terminal", "validation": {
            "host": "not-run", "build": "declarations-only", "hardware": "not-run"}}},
        "symbols": {key: {"task": "W-07"}}}
    actual = {"version": 1, "functions": [{"qualifiedName": "wifi.status"}]}
    return inventory, mapping, actual, key


class WifiApiCoverageTests(unittest.TestCase):
    def test_profile_cli_requires_explicit_unambiguous_identity(self):
        self.assertEqual(coverage.build_variant("representative=/tmp/build=a"),
                         ("representative", Path("/tmp/build=a")))
        for value in ("build", "=build", "representative=", "../escape=build"):
            with self.subTest(value=value), self.assertRaises(coverage.argparse.ArgumentTypeError):
                coverage.build_variant(value)

    def test_ci_compares_actual_recorded_profile_after_successful_build(self):
        import ci_build
        inventory, _, _, _ = fixture()
        changed = copy.deepcopy(inventory)
        changed["variants"]["esp32c5/representative"]["symbols"] = {}
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            path = root / "inventory.json"
            path.write_text(json.dumps(inventory))
            with patch.object(ci_build, "ROOT", root), \
                    patch.object(ci_build, "INVENTORY", path), \
                    patch.object(ci_build, "check_headers"), \
                    patch.object(ci_build, "generate_context"), \
                    patch.object(ci_build.subprocess, "run") as build, \
                    patch.object(ci_build, "collect", return_value=changed) as collect, \
                    patch.dict(ci_build.os.environ, {"IDF_PATH": "/sdk"}), \
                    patch.object(sys, "argv", ["ci", "--target", "esp32c5", "--profile", "representative"]):
                with self.assertRaisesRegex(ValueError, "Conditional declarations/fields changed"):
                    ci_build.main()
                build.assert_called_once()
                collect.assert_called_once_with([("representative", root / "build/ci/esp32c5-representative")])

    def test_collect_keeps_distinct_profiles_and_rejects_duplicate_variant(self):
        def capture(build, output):
            enabled = build.name != "disabled"
            (output / "translation-unit.i").write_text(
                PREFIX + f"#define WIFI_CSI_ENABLED {int(enabled)}\n")
            return {"target": "esp32c5", "idfRevision": "fixed", "headers": []}

        builds = [("representative", Path("enabled")), ("disabled", Path("disabled"))]
        with patch.object(coverage, "capture", side_effect=capture):
            result = coverage.collect(builds)
            variants = result["variants"]
            key = f"{HEADER}::WIFI_CSI_ENABLED"
            self.assertEqual(variants["esp32c5/representative"]["symbols"][key]["declaration"],
                             "WIFI_CSI_ENABLED 1")
            self.assertEqual(variants["esp32c5/disabled"]["symbols"][key]["declaration"],
                             "WIFI_CSI_ENABLED 0")
            with self.assertRaisesRegex(ValueError, "Duplicate variant"):
                coverage.collect([builds[0], builds[0]])

    def test_public_fields_callbacks_enums_and_macros_are_not_name_only(self):
        source = PREFIX + r'''
#define PACKET_LIMIT 32
typedef struct {
    unsigned length: 12;
    unsigned char bytes[PACKET_LIMIT];
    union { int channel; unsigned frequency; } location;
} __attribute__((packed, deprecated("message with )"))) packet_t;
enum { PACKET_OK = 0, PACKET_END };
typedef void (*packet_cb_t)(const packet_t *packet, void *arg);
int capture_start(packet_cb_t cb, const packet_t *options);
'''
        result = coverage.extract(source)
        fields = result[f"{HEADER}::packet_t"]["fields"]
        self.assertEqual([field["path"] for field in fields],
                         ["length", "bytes", "location", "location.channel", "location.frequency"])
        self.assertIn(": 12", fields[0]["declaration"])
        self.assertIn("bytes[PACKET_LIMIT]", fields[1]["declaration"])
        self.assertIn("(*packet_cb_t)", result[f"{HEADER}::packet_cb_t"]["declaration"])
        self.assertEqual(result[f"{HEADER}::PACKET_END"]["ordinalInEnum"], 1)
        self.assertEqual(result[f"{HEADER}::PACKET_LIMIT"]["kind"], "macro")

    def test_dependency_inline_assembly_does_not_hide_public_declarations(self):
        source = '# 1 "/sdk/dependency.h"\n' + r'''
typedef unsigned int word_t;
static inline word_t internal(void) {
    return ({ word_t x; asm volatile ("csrr %0, (csr)" : "=r"(x)); x; });
}
''' + PREFIX + "word_t public_query(void);"
        self.assertEqual(list(coverage.extract(source)), [f"{HEADER}::public_query"])

    def test_unknown_declaration_syntax_fails_closed(self):
        with self.assertRaisesRegex(ValueError, "Unsupported public-header"):
            coverage.extract(PREFIX + "unreviewed_keyword capture_new(void);")

    def test_new_and_removed_symbols_require_mapping_review(self):
        inventory, mapping, actual, key = fixture()
        coverage.validate(inventory, mapping, actual)
        inventory["variants"]["esp32c5/representative"]["symbols"][f"{HEADER}::new_api"] = {
            "kind": "function", "declaration": "void new_api(void)", "fields": []}
        with self.assertRaisesRegex(ValueError, "Unclassified symbols"):
            coverage.validate(inventory, mapping, actual)
        del inventory["variants"]["esp32c5/representative"]["symbols"][f"{HEADER}::new_api"]
        del inventory["variants"]["esp32c5/representative"]["symbols"][key]
        with self.assertRaisesRegex(ValueError, "stale mappings"):
            coverage.validate(inventory, mapping, actual)

    def test_planned_api_cannot_be_advertised_or_promoted_without_evidence(self):
        inventory, mapping, actual, key = fixture()
        entry = mapping["symbols"][key]
        entry["jsPath"] = "wifi.status"
        with self.assertRaisesRegex(ValueError, "Planned API already advertised"):
            coverage.validate(inventory, mapping, actual)
        entry["implementation"] = "implemented"
        with self.assertRaisesRegex(ValueError, "lacks reviewed actual API evidence"):
            coverage.validate(inventory, mapping, actual)
        entry.update(contract="reviewed", fieldCoverage="reviewed",
                     evidence=["production test"], validation={"host": "passed", "build": "passed", "hardware": "not-run"})
        coverage.validate(inventory, mapping, actual)

    def test_reviewed_in_progress_mapping_requires_actual_method_identity(self):
        inventory, mapping, actual, key = fixture()
        actual["functions"].append({"qualifiedName": "WiFiExample.prototype.close"})
        entry = mapping["symbols"][key]
        entry.update(implementation="in-progress", contract="reviewed", jsPath="WiFiExample.close")
        with self.assertRaisesRegex(ValueError, "no actual registered API"):
            coverage.validate(inventory, mapping, actual)
        entry["jsPath"] = "WiFiExample.prototype.close"
        coverage.validate(inventory, mapping, actual)

    def test_hidden_or_attribute_only_header_changes_require_review(self):
        with tempfile.TemporaryDirectory() as temporary:
            idf = Path(temporary)
            inventory = {"headers": {}}
            for name in coverage.HEADERS:
                path = idf / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("#if DISABLED\nvoid future_api(void);\n#endif\n")
                inventory["headers"][name] = hashlib.sha256(path.read_bytes()).hexdigest()
            coverage.check_headers(inventory, idf)
            (idf / HEADER).write_text("#if DISABLED\nvoid unknown_api(void);\n#endif\n")
            with self.assertRaisesRegex(ValueError, "Public header changed"):
                coverage.check_headers(inventory, idf)

    def test_cli_rejects_field_change_even_when_function_names_match(self):
        inventory, mapping, actual, key = fixture()
        source = PREFIX + "typedef struct { int bytes[4]; } config_t; int esp_wifi_example(void);"
        inventory["variants"]["esp32c5/representative"]["symbols"] = coverage.extract(source)
        mapping["symbols"][f"{HEADER}::config_t"] = {"task": "W-07"}
        changed = copy.deepcopy(inventory)
        changed["variants"]["esp32c5/representative"]["symbols"] = coverage.extract(source.replace("bytes[4]", "bytes[8]"))
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name, value in (("inventory.json", inventory), ("mapping.json", mapping),
                                ("api-manifest.json", actual)):
                (root / name).write_text(json.dumps(value))
            with patch.object(coverage, "ROOT", root), \
                    patch.object(coverage, "INVENTORY", root / "inventory.json"), \
                    patch.object(coverage, "MAPPING", root / "mapping.json"), \
                    patch.object(coverage, "collect", return_value=changed), \
                    patch.object(sys, "argv", ["coverage", "--check", "--build-dir", "representative=build"]):
                self.assertEqual(coverage.main(), 1)

    def test_checked_in_map_is_complete_for_collected_targets(self):
        inventory = json.loads(coverage.INVENTORY.read_text())
        mapping = json.loads(coverage.MAPPING.read_text())
        actual = json.loads((ROOT / "api-manifest.json").read_text())
        self.assertEqual({v["target"] for v in inventory["variants"].values()}, {"esp32c3", "esp32c5", "esp32s3"})
        coverage.validate(inventory, mapping, actual)

    def test_regeneration_accepts_explicitly_reviewed_new_mapping(self):
        inventory, mapping, actual, key = fixture()
        generated = copy.deepcopy(inventory)
        new_key = f"{HEADER}::new_api"
        generated["variants"]["esp32c5/representative"]["symbols"][new_key] = {
            "kind": "function", "declaration": "void new_api(void)", "fields": []}
        mapping["symbols"][new_key] = {"task": "W-07"}
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name, value in (("inventory.json", inventory), ("mapping.json", mapping),
                                ("api-manifest.json", actual)):
                (root / name).write_text(json.dumps(value))
            with patch.object(coverage, "ROOT", root), \
                    patch.object(coverage, "INVENTORY", root / "inventory.json"), \
                    patch.object(coverage, "MAPPING", root / "mapping.json"), \
                    patch.object(coverage, "collect", return_value=generated), \
                    patch.object(sys, "argv", ["coverage", "--write", "--build-dir", "representative=build"]):
                self.assertEqual(coverage.main(), 0)
            self.assertEqual(json.loads((root / "inventory.json").read_text()), generated)

    def test_regeneration_can_add_variant_but_cannot_erase_existing_baseline(self):
        inventory, mapping, actual, _ = fixture()
        generated = copy.deepcopy(inventory)
        variant = copy.deepcopy(inventory["variants"]["esp32c5/representative"])
        variant["profile"] = "disabled"
        generated["variants"]["esp32c5/disabled"] = variant
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name, value in (("inventory.json", inventory), ("mapping.json", mapping),
                                ("api-manifest.json", actual)):
                (root / name).write_text(json.dumps(value))
            with patch.object(coverage, "ROOT", root), \
                    patch.object(coverage, "INVENTORY", root / "inventory.json"), \
                    patch.object(coverage, "MAPPING", root / "mapping.json"), \
                    patch.object(coverage, "collect", return_value=generated) as collect, \
                    patch.object(sys, "argv", ["coverage", "--write", "--build-dir", "representative=build"]):
                self.assertEqual(coverage.main(), 0)
                collect.return_value = inventory
                self.assertEqual(coverage.main(), 1)
            self.assertEqual(json.loads((root / "inventory.json").read_text()), generated)
