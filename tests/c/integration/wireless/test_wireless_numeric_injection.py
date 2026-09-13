"""Numeric fault boundaries must agree with the linked MQuickJS representation."""
from tests.support.fixtures import fixture_text
import tempfile
import unittest

from tests.support.wireless_vm_fixture import build, run


MAIN = fixture_text('wireless/test_wireless_numeric_injection/main.inc')


class WirelessNumericInjection(unittest.TestCase):
    def test_immediate_numbers_do_not_allocate_and_boxed_numbers_still_fail(self):
        with tempfile.TemporaryDirectory() as tmp:
            run([str(build(tmp, '', MAIN))])
