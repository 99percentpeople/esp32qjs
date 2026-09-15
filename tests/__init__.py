"""Test package bootstrap for the repository's uninstalled host-tool packages."""
import sys
from tests.support.paths import ROOT

if str(ROOT / 'scripts') not in sys.path:
    sys.path.insert(0, str(ROOT / 'scripts'))
