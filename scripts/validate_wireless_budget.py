#!/usr/bin/env python3
"""Check immutable Build Context quotas, optionally against resolved sdkconfig.json."""

import argparse
import json
from pathlib import Path
import sys

from build_tools.wireless_budget import validate_wireless_budget


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-context", required=True, type=Path)
    parser.add_argument("--sdkconfig", type=Path)
    args = parser.parse_args()
    try:
        result = validate_wireless_budget(args.build_context, sdkconfig=args.sdkconfig)
    except (OSError, ValueError) as error:
        print(f"Wireless budget: {error}", file=sys.stderr)
        return 1
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
