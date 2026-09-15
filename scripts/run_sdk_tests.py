#!/usr/bin/env python3
"""Run registered SDK tooling/native fixtures; missing prerequisites fail this gate."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import unittest

from tool_paths import ROOT
from sdk_patches.registry import catalog


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--camera', action='store_true', help='Include the resolved managed camera component')
    parser.add_argument('--result', type=Path, default=ROOT / 'build/sdk-checks/native.json')
    args = parser.parse_args()
    if not os.environ.get('IDF_PATH'):
        parser.error('IDF_PATH is required for SDK validation')
    os.environ['ESP32QJS_REQUIRE_SDK'] = '1'
    sys.path.insert(0, str(ROOT))
    modules = sorted({name for patch in catalog()['patches']
                      if args.camera or patch['id'] != 'camera' for name in patch['tests']})
    native = [name for name in modules if name.startswith('tests.c.')]
    tooling = [name for name in modules if name.startswith('tests.python.')]
    suite = unittest.defaultTestLoader.loadTestsFromNames(tooling)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    if not result.wasSuccessful() or result.skipped:
        return 1
    command = [sys.executable, str(ROOT / 'scripts/run_native_tests.py'),
               '--require-all', '--result', str(args.result)]
    for name in native:
        command.extend(['--case', name])
    return subprocess.run(command, cwd=ROOT, check=False).returncode


if __name__ == '__main__':
    raise SystemExit(main())
