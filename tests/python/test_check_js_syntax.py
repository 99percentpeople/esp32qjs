import importlib.util
import re
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
CHECKER_PATH = ROOT / "scripts" / "check_js_syntax.py"
SPEC = importlib.util.spec_from_file_location("esp32qjs_syntax_test", CHECKER_PATH)
CHECKER = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = CHECKER
SPEC.loader.exec_module(CHECKER)


class JavaScriptSyntaxToolTests(unittest.TestCase):
    def test_host_checker_ignores_esp_idf_target_compiler(self):
        with (
            patch.dict(
                CHECKER.os.environ,
                {"CC": "xtensa-esp32s3-elf-gcc", "HOSTCC": ""},
                clear=True,
            ),
            patch.object(
                CHECKER.shutil,
                "which",
                side_effect=lambda name: "/usr/bin/cc" if name == "cc" else None,
            ),
        ):
            self.assertEqual(CHECKER.compiler_command(), ["cc"])

    def test_engine_version_matches_vendored_changelog(self):
        version_header = (
            ROOT
            / "components"
            / "esp32_mquickjs"
            / "include"
            / "esp32_mquickjs_version.h"
        ).read_text(encoding="utf-8")
        changelog_version = (
            ROOT
            / "components"
            / "esp32_mquickjs"
            / "vendor"
            / "mquickjs"
            / "Changelog"
        ).read_text(encoding="utf-8").split(":", 1)[0]
        match = re.search(
            r'^#define ESP32_MQUICKJS_ENGINE_VERSION "([^"]+)"$',
            version_header,
            re.MULTILINE,
        )

        self.assertIsNotNone(match)
        self.assertEqual(match.group(1), changelog_version)

    def test_default_roots_cover_first_party_javascript(self):
        files = CHECKER.iter_js_files(CHECKER.DEFAULT_SOURCE_ROOTS)
        relative = {path.relative_to(ROOT).as_posix() for path in files}

        self.assertIn("tests/build-contexts/esp32s3/flash_data/index.js", relative)
        self.assertIn("tests/js/flash_data/index.js", relative)
        self.assertFalse(any("vendor/" in path for path in relative))

    def test_extra_paths_extend_default_sources(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            extra = Path(temp_dir) / "external.js"
            extra.write_text("var externalApp = true;\n", encoding="utf-8")
            args = CHECKER.parse_args(["--extra-path", str(extra)])
            files = CHECKER.iter_js_files([
                *CHECKER.DEFAULT_SOURCE_ROOTS,
                *args.extra_path,
            ])

            self.assertIn(extra.resolve(), files)
            self.assertTrue(args.extra_path)

    def test_document_snippets_keep_source_line_numbers(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            snippets = CHECKER.extract_documented_js(
                Path(temp_dir),
                CHECKER.DEFAULT_DOCUMENTS,
            )

            self.assertGreater(len(snippets), 0)
            api_snippets = [path for path in snippets if "global-helpers.md" in path.name]
            self.assertGreater(len(api_snippets), 0)
            content = api_snippets[0].read_text(encoding="utf-8")
            marker = int(api_snippets[0].stem.rsplit("_", 1)[-1])
            self.assertTrue(content.startswith("\n" * (marker - 1)))

    def test_extra_document_directory_is_discovered(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            nested = root / "skill" / "references"
            nested.mkdir(parents=True)
            document = nested / "api.md"
            document.write_text("```js\nvar value = 1;\n```\n", encoding="utf-8")

            self.assertEqual(CHECKER.iter_documents([root]), [document.resolve()])
            args = CHECKER.parse_args([
                "--docs-only",
                "--extra-doc",
                str(root),
            ])
            self.assertTrue(args.docs_only)
            self.assertEqual(args.extra_doc, [root])

    def test_non_javascript_path_is_rejected(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            text_file = Path(temp_dir) / "not-js.txt"
            text_file.write_text("var value = 1;", encoding="utf-8")

            with self.assertRaises(ValueError):
                CHECKER.iter_js_files([text_file])


if __name__ == "__main__":
    unittest.main()
