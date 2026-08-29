import subprocess
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class FeatureDocumentationTests(unittest.TestCase):
    def test_generated_feature_documentation_is_current(self):
        subprocess.run(
            [sys.executable, str(ROOT / "scripts" / "generate_feature_docs.py"), "--check"],
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
        )

    def test_runtime_integration_does_not_reference_removed_apps_layout(self):
        runtime = (ROOT / "docs" / "runtime-api.md").read_text(encoding="utf-8")
        self.assertNotIn("apps/<name>/flash_data/index.js", runtime)
