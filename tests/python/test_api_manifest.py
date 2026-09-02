import json
import subprocess
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class ApiManifestTests(unittest.TestCase):
    def test_checked_in_manifest_matches_and_validates_every_native_callable(self):
        result = subprocess.run(
            [sys.executable, "scripts/generate_api_manifest.py", "--check"],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(
            result.returncode,
            0,
            msg=f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}",
        )

    def test_shared_module_docs_cover_every_public_native_feature(self):
        docs_manifest = json.loads(
            (ROOT / "docs/api/docs.json").read_text(encoding="utf-8")
        )
        feature_catalog = json.loads(
            (
                ROOT
                / "components/esp32_mquickjs/runtime-features.json"
            ).read_text(encoding="utf-8")
        )
        entries = docs_manifest["docs"]
        self.assertEqual(docs_manifest["schema"], 1)
        self.assertEqual(
            len({entry["id"] for entry in entries}),
            len(entries),
        )
        self.assertEqual(
            len({entry["path"] for entry in entries}),
            len(entries),
        )
        self.assertEqual(
            {entry["path"] for entry in entries},
            {
                f"api/{path.name}"
                for path in (ROOT / "docs/api").glob("*.md")
                if path.name != "README.md"
            },
        )
        for entry in entries:
            self.assertEqual(
                set(entry),
                {"id", "title", "path", "requiresAnyFeatures"},
            )
            self.assertTrue(entry["path"].startswith("api/"))
            self.assertTrue((ROOT / "docs" / entry["path"]).is_file())

        documented_features = {
            feature
            for entry in entries
            for feature in entry["requiresAnyFeatures"]
        }
        public_features = {
            feature["id"]
            for feature in feature_catalog["features"]
            if feature.get("public", True)
        }
        self.assertEqual(documented_features, public_features)
        self.assertTrue((ROOT / "docs/api/README.md").is_file())
        self.assertFalse((ROOT / "docs/c-api.md").exists())
        self.assertFalse((ROOT / "docs/js-api.md").exists())

    def test_callable_manifest_uses_only_shared_module_docs(self):
        manifest = json.loads(
            (ROOT / "api-manifest.json").read_text(encoding="utf-8")
        )
        listed = {
            f"docs/{entry['path']}"
            for entry in json.loads(
                (ROOT / "docs/api/docs.json").read_text(encoding="utf-8")
            )["docs"]
        }
        referenced = {
            entry["documentation"]["file"]
            for group in ("classes", "functions")
            for entry in manifest[group]
        }
        self.assertTrue(referenced)
        self.assertTrue(referenced <= listed)
        self.assertTrue(all(path.startswith("docs/api/") for path in referenced))


if __name__ == "__main__":
    unittest.main()
