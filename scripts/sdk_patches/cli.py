"""Single command boundary for SDK preparation and upgrade inspection."""
import argparse
import importlib
import json
import os
from pathlib import Path
import sys

from sdk_patches.common.io import write_if_changed
from sdk_patches.registry import catalog, render_cmake


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    data = catalog()
    implementations = {patch['id']: patch['module'] for patch in data['patches'] if patch['module']}
    implementations.update(data['helpers'])
    if argv and argv[0] in implementations:
        implementation = importlib.import_module(implementations[argv[0]])
        if not hasattr(implementation, 'main'):
            raise SystemExit('Internal patch helper has no CLI: ' + argv[0])
        previous = sys.argv
        try:
            sys.argv = [previous[0] + ' ' + argv[0], *argv[1:]]
            return implementation.main()
        finally:
            sys.argv = previous
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    sub.add_parser('list', help='Show adapter ownership and ordered dependencies')
    cmake = sub.add_parser('cmake', help='Generate this build\'s ordered SDK includes')
    cmake.add_argument('--output', type=Path, required=True)
    check = sub.add_parser('check', help='Inspect SDK inputs and archive composition without modifying the SDK')
    check.add_argument('--idf-path', type=Path, default=os.environ.get('IDF_PATH'))
    check.add_argument('--target', choices=data['baseline']['targets'], action='append')
    check.add_argument('--result', type=Path)
    args = parser.parse_args(argv)
    if args.command == 'list':
        print(json.dumps(data, indent=2))
        return 0
    if args.command == 'cmake':
        write_if_changed(args.output, render_cmake())
        return 0
    if args.idf_path is None or not args.idf_path.is_dir():
        parser.error('--idf-path or IDF_PATH must point to an existing SDK')
    from sdk_patches.check import inspect_sdk
    report = inspect_sdk(args.idf_path.resolve(), args.target or data['baseline']['targets'])
    if args.result:
        write_if_changed(args.result, json.dumps(report, indent=2) + '\n')
    if not report['revisionMatches']:
        print('SDK commit needs review: ' + str(report['idfCommit']))
    mismatches = [row for row in report['inputs'] if row['status'] != 'matched']
    for row in mismatches:
        print(f'{row["status"]}: {row["path"]} ({", ".join(row["patches"])})')
    for row in report['compositions']:
        print(f'{row["target"]} optional={row["optionalPatches"]}: {row["status"]}'
              + (': ' + row['error'] if 'error' in row else ''))
    print(f'SDK preflight {report["status"]}: {len(report["inputs"])} inputs, {len(mismatches)} mismatches')
    return int(report['status'] != 'passed')
