"""Exercise production byte storage and its actual MQuickJS class finalizers."""
from tests.support.fixtures import fixture_text
import pathlib
import sys
import tempfile
import unittest
from tests.support.wireless_vm_fixture import build, run

EXTRA = fixture_text('wireless/test_wireless_byte_storage_gc/extra.inc')
MAIN = fixture_text('wireless/test_wireless_byte_storage_gc/main.inc')
INPUT_MAIN = fixture_text('wireless/test_wireless_byte_storage_gc/input_main.inc')
class WirelessByteStorageGc(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory();cls.addClassCleanup(cls.temp.cleanup)
        cls.binary=build(pathlib.Path(cls.temp.name)/"storage",EXTRA,MAIN)
        cls.input_binary=build(pathlib.Path(cls.temp.name)/"input",EXTRA,INPUT_MAIN)

    def test_construction_close_read_lease_and_finalizer(self):
        for mode in range(7):
            for gc in (0,1):
                for wireless in (0,1):
                    with self.subTest(mode=mode,gc=gc,wireless=wireless):
                        run([str(self.binary),str(mode),str(gc),str(wireless)])

    def test_invalid_length_byte_and_offset_are_rejected_before_transfer(self):
        for wireless in (0,1):
            run([str(self.input_binary),str(wireless)])
