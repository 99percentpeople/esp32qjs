"""Production schema validator regressions; run with the Wi-Fi phase suite."""
import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import generate_wifi_config_schema as schema


class WiFiConfigSchema(unittest.TestCase):
    def setUp(self):
        self.inventory = json.loads(schema.INVENTORY.read_text())
        self.mapping = json.loads(schema.MAPPING.read_text())

    def test_all_recorded_variants_and_nested_fields_are_checked(self):
        evidence = schema.validate(self.inventory, self.mapping)
        for counts in evidence.values():
            self.assertEqual(set(counts), set(self.inventory["variants"]))
        fields = self.mapping["structures"]["wifi_sta_config_t"]["fields"]
        self.assertIn("threshold.rssi_5g_adjustment", fields)
        output = schema.render(self.mapping, evidence)
        self.assertEqual(output, schema.OUTPUT.read_text())
        fields = schema.render_fields(self.mapping)
        self.assertEqual(fields, schema.FIELD_HEADER.read_text())
        self.assertIn('"pmf"', fields)
        self.assertNotIn('"reserved1"', fields)
        self.assertIn("contract-pending", output)
        self.assertNotIn("reserved1?:", output)
        self.assertNotIn("capable?:", output)

    def test_new_nested_leaf_in_one_variant_requires_review(self):
        variant = next(iter(self.inventory["variants"].values()))
        variant["symbols"][schema.HEADER + "::wifi_scan_threshold_t"]["fields"].append(
            {"path": "new_threshold", "declaration": "uint8_t new_threshold"})
        with self.assertRaisesRegex(ValueError, "added=.*threshold.new_threshold"):
            schema.validate(self.inventory, self.mapping)

    def test_every_public_readback_leaf_and_secret_policy_are_generated(self):
        schema.validate(self.inventory, self.mapping)
        self.assertEqual(schema.render_observations(self.mapping), schema.OBSERVATION_HEADER.read_text())
        self.assertIn(schema.render_readback_types(self.mapping), schema.TYPES.read_text())
        fields = self.mapping['structures']['wifi_sta_config_t']['fields']
        fields['channel']['readback'] = None
        with self.assertRaisesRegex(ValueError, 'Missing readback codec'):
            schema.validate(self.inventory, self.mapping)

    def test_readback_reserved_duplicate_and_secret_exports_rejected(self):
        for name in ('reserved', 'duplicate', 'secret'):
            mapping = copy.deepcopy(self.mapping)
            fields = mapping['structures']['wifi_sta_config_t']['fields']
            if name == 'reserved': fields['reserved1']['readback'] = fields['channel']['readback']
            if name == 'duplicate': fields['channel']['readback']['jsPath'] = fields['ssid']['readback']['jsPath']
            if name == 'secret': fields['password']['readback']['tsType'] = 'number[]'
            with self.subTest(name=name), self.assertRaises(ValueError):
                schema.validate(self.inventory, mapping)

    def test_removed_leaf_or_changed_bit_width_requires_review(self):
        for change in ("remove", "width"):
            inventory = copy.deepcopy(self.inventory)
            fields = next(iter(inventory["variants"].values()))["symbols"][schema.HEADER + "::wifi_sta_config_t"]["fields"]
            field = next(f for f in fields if f["path"] == "he_dcm_set")
            if change == "remove":
                fields.remove(field)
            else:
                field["declaration"] = "uint32_t he_dcm_set : 2"
            with self.subTest(change=change), self.assertRaisesRegex(ValueError, "he_dcm_set"):
                schema.validate(inventory, self.mapping)

    def test_unknown_pointer_or_recursive_layout_fails_closed(self):
        for declaration in ("uint8_t *ssid", "wifi_sta_config_t ssid"):
            inventory = copy.deepcopy(self.inventory)
            fields = next(iter(inventory["variants"].values()))["symbols"][schema.HEADER + "::wifi_sta_config_t"]["fields"]
            fields[0]["declaration"] = declaration
            with self.subTest(declaration=declaration), self.assertRaises(ValueError):
                schema.validate(inventory, self.mapping)

    def test_duplicate_public_names_and_reserved_export_rejected(self):
        fields = self.mapping["structures"]["wifi_sta_config_t"]["fields"]
        fields["channel"]["jsPath"] = fields["ssid"]["jsPath"]
        with self.assertRaisesRegex(ValueError, "duplicate JS field"):
            schema.validate(self.inventory, self.mapping)
        fields["channel"]["jsPath"] = "channel"
        fields["reserved1"]["jsPath"] = "reserved1"
        with self.assertRaisesRegex(ValueError, "Non-input field exported"):
            schema.validate(self.inventory, self.mapping)

    def test_revision_and_header_drift_require_recapture(self):
        for key in ("idfRevision", "headerSha256"):
            mapping = copy.deepcopy(self.mapping)
            mapping[key] = "changed"
            with self.subTest(key=key), self.assertRaises(ValueError):
                schema.validate(self.inventory, mapping)
        with tempfile.TemporaryDirectory() as directory:
            header = Path(directory) / schema.HEADER
            header.parent.mkdir(parents=True)
            header.write_text("changed SDK header")
            with self.assertRaisesRegex(ValueError, "Live SDK config header changed"):
                schema.validate(self.inventory, self.mapping, Path(directory))

    def test_credential_policy_cannot_be_dropped(self):
        self.mapping["structures"]["wifi_sta_config_t"]["fields"]["password"]["secret"] = False
        with self.assertRaisesRegex(ValueError, "Credential field lost secret policy"):
            schema.validate(self.inventory, self.mapping)
