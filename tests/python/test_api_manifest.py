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


if __name__ == "__main__":
    unittest.main()
