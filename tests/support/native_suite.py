"""Discover native integration cases without re-running imported TestCases."""
import re
import unittest

from tests.support.paths import ROOT
CASE_ROOT = ROOT / "tests/c/integration"


def iter_cases(suite):
    for item in suite:
        if isinstance(item, unittest.TestSuite):
            yield from iter_cases(item)
        else:
            yield item


class NativeTestLoader(unittest.TestLoader):
    def loadTestsFromModule(self, module, *, pattern=None):
        suite = super().loadTestsFromModule(module, pattern=pattern)
        return unique_suite(
            case for case in iter_cases(suite)
            if case.__class__.__module__ == module.__name__
        )


def unique_suite(cases):
    by_id = {}
    for case in cases:
        by_id.setdefault(case.id(), case)
    return unittest.TestSuite(by_id.values())


def select_native_tests(names):
    native_name = r"tests\.c\.integration\.(?:[a-z_]\w*\.)+test_\w+(?:\.\w+)*"
    if not names or any(re.fullmatch(native_name, name) is None for name in names):
        raise ValueError("Case selectors must name tests.c.integration.<domain>.test_* cases")
    loader = NativeTestLoader()
    suite = unique_suite(iter_cases(loader.loadTestsFromNames(names)))
    return suite, loader.errors


class NativeTestResult(unittest.TextTestResult):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.successful_ids = set()

    def addSuccess(self, test):
        self.successful_ids.add(test.id())
        super().addSuccess(test)


def summarize_native_result(result, selected_ids):
    selected = set(selected_ids)

    def expand(test):
        name = getattr(test, "test_case", test).id()
        if name in selected:
            return {name}
        hook = re.fullmatch(r"(?:setUp|tearDown)(?:Class|Module) \((.+)\)", name)
        if hook:
            return {case for case in selected if case.startswith(hook[1] + ".")}
        return set()

    failed = set().union(*(expand(case) for case, _ in result.failures + result.errors),
                         *(expand(case) for case in result.unexpectedSuccesses))
    skipped = set().union(*(expand(case) for case, _ in result.skipped + result.expectedFailures)) - failed
    passed = result.successful_ids - failed - skipped
    not_run = selected - passed - failed - skipped
    states = {case: state for state, cases in (
        ("passed", passed), ("failed", failed), ("skipped", skipped), ("not-run", not_run),
    ) for case in sorted(cases)}
    return {
        "status": "passed" if result.wasSuccessful() and not not_run else "failed",
        "total": len(selected), "executed": result.testsRun,
        "passed": len(passed), "failed": len(failed), "skipped": len(skipped),
        "notRun": len(not_run), "failedCases": sorted(failed), "notRunCases": sorted(not_run),
        "caseResults": states,
        "fixtureErrors": [case.id() for case, _ in result.errors if case.id() not in selected],
        "skipReasons": [{"case": case.id(), "reason": reason} for case, reason in result.skipped],
        "expectedFailures": [case.id() for case, _ in result.expectedFailures],
    }


def discover_native_tests(pattern="test_*.py"):
    loader = NativeTestLoader()
    suite = loader.discover(str(CASE_ROOT), pattern=pattern, top_level_dir=str(ROOT))
    if not suite.countTestCases():
        raise ValueError(f"No native test cases match {pattern!r}")
    return suite, loader.errors
