import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCRIPTS = ROOT / "scripts"
sys.path.insert(0, str(SCRIPTS))
TOOL_PATH = SCRIPTS / "precompile_startup.py"
SPEC = importlib.util.spec_from_file_location("esp32qjs_precompile_test", TOOL_PATH)
TOOL = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = TOOL
SPEC.loader.exec_module(TOOL)


class StartupPrecompileTests(unittest.TestCase):
    def test_bundles_only_manifest_sources_and_keeps_workspace_load(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            (root / "agent").mkdir()
            (root / "agent" / "fixed.js").write_text(
                "globalThis.fixedAgent = true;\n",
                encoding="utf-8",
            )
            (root / "index.js").write_text(
                'load("agent/fixed.js");\n'
                'if (fs.exists("index.js")) {\n  load("index.js");\n}\n',
                encoding="utf-8",
            )
            manifest = TOOL.StartupManifest(
                entry="index.js",
                output="index.js",
                inline=frozenset({"agent/fixed.js"}),
                remove_after_compile=("agent",),
            )

            bundle = TOOL.bundle_startup(root, manifest)

            self.assertIn("globalThis.fixedAgent = true", bundle)
            self.assertNotIn('load("agent/fixed.js")', bundle)
            self.assertIn('load("index.js")', bundle)

    def test_rejects_missing_and_recursive_manifest_sources(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            (root / "index.js").write_text('load("agent/missing.js");\n', encoding="utf-8")
            missing = TOOL.StartupManifest(
                entry="index.js",
                output="index.js",
                inline=frozenset({"agent/other.js"}),
                remove_after_compile=(),
            )
            with self.assertRaisesRegex(ValueError, "were not loaded"):
                TOOL.bundle_startup(root, missing)

            (root / "agent").mkdir()
            (root / "agent" / "loop.js").write_text('load("agent/loop.js");\n', encoding="utf-8")
            (root / "index.js").write_text('load("agent/loop.js");\n', encoding="utf-8")
            recursive = TOOL.StartupManifest(
                entry="index.js",
                output="index.js",
                inline=frozenset({"agent/loop.js"}),
                remove_after_compile=(),
            )
            with self.assertRaisesRegex(ValueError, "recursive startup load"):
                TOOL.bundle_startup(root, recursive)

    def test_manifest_rejects_traversal_and_unknown_fields(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            path = Path(temp_dir) / "manifest.json"
            document = {
                "version": 1,
                "entry": "../index.js",
                "output": "index.js",
                "inline": ["agent/fixed.js"],
                "removeAfterCompile": ["agent"],
            }
            path.write_text(json.dumps(document), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "stay below"):
                TOOL.load_manifest(path)

            document["entry"] = "index.js"
            document["unexpected"] = True
            path.write_text(json.dumps(document), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "contain exactly"):
                TOOL.load_manifest(path)


if __name__ == "__main__":
    unittest.main()
