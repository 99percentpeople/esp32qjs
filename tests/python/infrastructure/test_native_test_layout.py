"""Native test discovery, fixture transport, and reporting are host tooling."""
from tests.support.paths import ROOT as TEST_ROOT
import importlib.util
import io
import json
import os
from pathlib import Path
import tempfile
import types
import unittest
from contextlib import redirect_stderr, redirect_stdout
from unittest.mock import patch

from tests.support import fixtures
from tests.support.native_suite import NativeTestLoader, discover_native_tests, iter_cases, select_native_tests

ROOT = TEST_ROOT


class NativeTestLayoutTests(unittest.TestCase):
    def test_imported_cases_are_not_registered_again_but_local_subclasses_are(self):
        foreign = types.ModuleType("foreign_native_case")
        exec("import unittest\nclass Shared(unittest.TestCase):\n"
             " def test_behavior(self): pass\n", foreign.__dict__)
        local = types.ModuleType("local_native_case")
        local.Shared = foreign.Shared
        exec("class Owned(Shared):\n def test_extra(self): pass\n", local.__dict__)
        local.OwnedAlias = local.Owned
        cases = list(iter_cases(NativeTestLoader().loadTestsFromModule(local)))
        self.assertEqual([case.id() for case in cases], [
            "local_native_case.Owned.test_behavior", "local_native_case.Owned.test_extra",
        ])

    def test_pattern_selects_owned_cases_without_imported_helper_test_classes(self):
        suite, errors = discover_native_tests("test_wifi_nan_data.py")
        self.assertFalse(errors)
        cases = list(iter_cases(suite))
        self.assertTrue(cases)
        self.assertTrue(all(case.id().startswith("tests.c.integration.wifi.nan.test_wifi_nan_data.")
                            for case in cases))

    def test_empty_selection_is_an_error(self):
        with self.assertRaisesRegex(ValueError, "No native test cases"):
            discover_native_tests("no_such_native_case_*.py")

    def test_explicit_selection_is_native_only_and_deduplicated(self):
        name = ("tests.c.integration.wifi.station.test_wifi_late_station_netif.WiFiLateStationNetif."
                "test_start_in_progress_is_reconciled_after_the_native_fence")
        suite, errors = select_native_tests([name, name])
        self.assertFalse(errors)
        self.assertEqual([case.id() for case in iter_cases(suite)], [name])
        for invalid in ("tests.python.tooling.build.test_remote",
                        "tests.support.native_suite", "tests.c.integration.wifi",
                        "tests.c.integration.wifi..test_missing"):
            with self.subTest(selector=invalid):
                with self.assertRaisesRegex(ValueError, "Case selectors must name"):
                    select_native_tests([invalid])
        module = "tests.c.integration.wifi.provisioning.wps.test_wifi_wps_session"
        suite, errors = select_native_tests([module, module])
        discovered, discovery_errors = discover_native_tests("test_wifi_wps_session.py")
        self.assertFalse(errors or discovery_errors)
        self.assertEqual([case.id() for case in iter_cases(suite)],
                         [case.id() for case in iter_cases(discovered)])

    def test_import_errors_remain_failing_cases(self):
        loader = NativeTestLoader()
        suite = loader.loadTestsFromName("tests.c.integration.no_such_native_module")
        with redirect_stderr(io.StringIO()):
            result = unittest.TextTestRunner().run(suite)
        self.assertEqual(result.testsRun, 1)
        self.assertEqual(len(result.errors), 1)
        self.assertFalse(result.wasSuccessful())

    def test_external_source_preserves_bytes_and_missing_fixture_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            data = "/* 原文 */\r\nconst char *s = \\\"x\\\";\n".encode()
            (Path(directory) / "boundary.inc").write_bytes(data)
            fixtures.fixture_text.cache_clear()
            self.addCleanup(fixtures.fixture_text.cache_clear)
            with patch.object(fixtures, "FIXTURE_ROOT", Path(directory)):
                self.assertEqual(fixtures.fixture_text("boundary.inc").encode(), data)
                with self.assertRaises(FileNotFoundError):
                    fixtures.fixture_text("missing.inc")

    def test_runner_counts_failed_subtests_once_and_preserves_skips(self):
        spec = importlib.util.spec_from_file_location("native_test_entry", ROOT / "scripts/run_native_tests.py")
        runner = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(runner)

        class Cases(unittest.TestCase):
            def test_failure(self):
                for item in range(2):
                    with self.subTest(item=item):
                        self.fail("injected failure")

            def test_skip(self):
                self.skipTest("SDK fixture unavailable")

        suite = unittest.defaultTestLoader.loadTestsFromTestCase(Cases)
        with tempfile.TemporaryDirectory() as directory:
            result_file = Path(directory) / "result.json"
            with patch.object(runner, "discover_native_tests", return_value=(suite, [])), \
                    patch("sys.argv", ["run_native_tests.py", "--result", str(result_file)]), \
                    redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
                self.assertEqual(runner.main(), 1)
            result = json.loads(result_file.read_text())
        self.assertEqual((result["total"], result["passed"], result["failed"], result["skipped"]),
                         (2, 0, 1, 1))
        self.assertEqual(result["skipReasons"][0]["reason"], "SDK fixture unavailable")

    def test_strict_sdk_runner_rejects_skipped_fixtures(self):
        spec = importlib.util.spec_from_file_location("native_strict_entry", ROOT / "scripts/run_native_tests.py")
        runner = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(runner)

        class Cases(unittest.TestCase):
            def test_missing_sdk(self):
                self.skipTest("SDK fixture unavailable")

        with tempfile.TemporaryDirectory() as directory:
            result_file = Path(directory) / "result.json"
            for strict, expected in ((False, 0), (True, 1)):
                with self.subTest(strict=strict):
                    suite = unittest.defaultTestLoader.loadTestsFromTestCase(Cases)
                    argv = ["run_native_tests.py", "--result", str(result_file)]
                    if strict:
                        argv.append("--require-all")
                    with patch.object(runner, "discover_native_tests", return_value=(suite, [])), \
                            patch("sys.argv", argv), redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
                        self.assertEqual(runner.main(), expected)
                    result = json.loads(result_file.read_text())
                    self.assertEqual(result["status"], "failed" if strict else "passed")
                    self.assertEqual((result["passed"], result["skipped"]), (0, 1))
                    self.assertEqual(result["skipReasons"][0]["reason"], "SDK fixture unavailable")

    def test_native_stage_passes_resolved_sdk_and_reports_child_counts(self):
        from build_tools import device_tests

        def complete(command, **kwargs):
            self.assertEqual(kwargs["env"]["IDF_PATH"], "/reviewed/sdk")
            self.assertNotIn("stdout", kwargs)
            self.assertNotIn("capture_output", kwargs)
            path = Path(command[command.index("--result") + 1])
            path.write_text(json.dumps({"status": "passed", "passed": 2,
                                       "skipped": 1, "failed": 0, "failedCases": [],
                                       "notRun": 0, "notRunCases": []}))
            return types.SimpleNamespace(returncode=0)

        with patch.object(device_tests.subprocess, "run", side_effect=complete), \
                patch.object(device_tests.Path, "is_file", return_value=True), \
                redirect_stdout(io.StringIO()):
            result = device_tests.run_native_fixture_tests(types.SimpleNamespace(idf_path="/reviewed/sdk"))
        self.assertEqual((result.total_cases, result.passed_cases, result.skipped_cases), (3, 2, 1))

    def test_class_setup_failure_accounts_for_every_selected_case(self):
        spec = importlib.util.spec_from_file_location("native_setup_entry", ROOT / "scripts/run_native_tests.py")
        runner = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(runner)

        class Cases(unittest.TestCase):
            @classmethod
            def setUpClass(cls):
                raise RuntimeError("fixture setup failed")

            def test_one(self):
                pass

            def test_two(self):
                pass

        suite = unittest.defaultTestLoader.loadTestsFromTestCase(Cases)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "result.json"
            with patch.object(runner, "discover_native_tests", return_value=(suite, [])), \
                    patch("sys.argv", ["run_native_tests.py", "--result", str(path)]), \
                    redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
                self.assertEqual(runner.main(), 1)
            result = json.loads(path.read_text())
        self.assertEqual((result["total"], result["passed"], result["failed"], result["skipped"]),
                         (2, 0, 2, 0))

    def test_missing_sdk_is_an_explicit_fixture_skip(self):
        from tests.support.idf import require_idf
        from tests.c.integration.wifi.provisioning.wps.test_wifi_wps_session import headers as station_headers
        from tests.c.integration.wifi.provisioning.wps.test_wifi_wps_ap_session import headers as ap_headers

        with patch.dict(os.environ):
            os.environ.pop("IDF_PATH", None)
            for prepare in (require_idf, station_headers, ap_headers):
                with self.subTest(prepare=prepare.__module__), self.assertRaises(unittest.SkipTest):
                    prepare()

    def test_stopped_suite_does_not_report_unexecuted_cases_as_passed(self):
        from tests.support.native_suite import NativeTestResult, summarize_native_result

        class Cases(unittest.TestCase):
            def test_a_stop(self):
                self._outcome.result.stop()

            def test_b_never_executed(self):
                self.fail("must not run after stop")

        suite = unittest.defaultTestLoader.loadTestsFromTestCase(Cases)
        selected = [case.id() for case in iter_cases(suite)]
        result = unittest.TextTestRunner(stream=io.StringIO(), resultclass=NativeTestResult).run(suite)
        summary = summarize_native_result(result, selected)
        self.assertEqual((summary["status"], summary["total"], summary["passed"], summary["notRun"]),
                         ("failed", 2, 1, 1))
