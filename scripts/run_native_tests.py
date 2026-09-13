#!/usr/bin/env python3
"""Run host/SDK/VM native fixtures; never build or flash a device image."""
import argparse
import json
from pathlib import Path
import sys
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tests.support.native_suite import (
    NativeTestResult, discover_native_tests, iter_cases, select_native_tests,
    summarize_native_result,
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument("--pattern", default="test_*.py", help="Native case filename pattern")
    selection.add_argument("--case", action="append", help="Qualified native case, class or module; repeatable")
    parser.add_argument("--list", action="store_true", help="List owned cases without executing them")
    parser.add_argument("--result", type=Path, help="Write the execution summary as JSON")
    args = parser.parse_args()
    try:
        suite, import_errors = (select_native_tests(args.case) if args.case
                                else discover_native_tests(args.pattern))
    except ValueError as error:
        parser.error(str(error))
    if args.list:
        if import_errors:
            print("\n".join(import_errors), file=sys.stderr)
            return 1
        for case in iter_cases(suite):
            print(case.id())
        return 0

    started = time.monotonic()
    selected_ids = [case.id() for case in iter_cases(suite)]
    result = unittest.TextTestRunner(verbosity=2, resultclass=NativeTestResult).run(suite)
    summary = {
        "schema": 1,
        "suite": "native-fixtures",
        "pattern": None if args.case else args.pattern,
        "selection": args.case or [],
        **summarize_native_result(result, selected_ids),
        "seconds": time.monotonic() - started,
    }
    if args.result:
        args.result.parent.mkdir(parents=True, exist_ok=True)
        args.result.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print("Native C/SDK/VM fixtures: " + json.dumps({key: summary[key] for key in (
        "status", "total", "executed", "passed", "failed", "skipped", "notRun", "seconds",
    )}), flush=True)
    return 0 if summary["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
