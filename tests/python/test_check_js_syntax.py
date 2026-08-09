import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CHECKER_PATH = ROOT / "scripts" / "check_js_syntax.py"
SPEC = importlib.util.spec_from_file_location("esp32qjs_syntax_test", CHECKER_PATH)
CHECKER = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = CHECKER
SPEC.loader.exec_module(CHECKER)


class JavaScriptSyntaxToolTests(unittest.TestCase):
    def test_default_roots_cover_first_party_javascript(self):
        files = CHECKER.iter_js_files(CHECKER.DEFAULT_SOURCE_ROOTS)
        relative = {path.relative_to(ROOT).as_posix() for path in files}

        self.assertIn("apps/minimal/flash_data/index.js", relative)
        self.assertIn("apps/demo/flash_data/index.js", relative)
        self.assertIn("shared/flash_data/_sys/display/core.js", relative)
        self.assertIn("tests/js/flash_data/index.js", relative)
        self.assertFalse(any("vendor/" in path for path in relative))

    def test_document_snippets_keep_source_line_numbers(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            snippets = CHECKER.extract_documented_js(Path(temp_dir))

            self.assertGreater(len(snippets), 0)
            c_api_snippets = [path for path in snippets if "c-api.md" in path.name]
            self.assertGreater(len(c_api_snippets), 0)
            content = c_api_snippets[0].read_text(encoding="utf-8")
            marker = int(c_api_snippets[0].stem.rsplit("_", 1)[-1])
            self.assertTrue(content.startswith("\n" * (marker - 1)))

    def test_non_javascript_path_is_rejected(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            text_file = Path(temp_dir) / "not-js.txt"
            text_file.write_text("var value = 1;", encoding="utf-8")

            with self.assertRaises(ValueError):
                CHECKER.iter_js_files([text_file])


if __name__ == "__main__":
    unittest.main()
