import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "hardware_lab_evidence.py"
SPEC = importlib.util.spec_from_file_location("esp32qjs_hardware_lab_evidence_test", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


class HardwareLabEvidenceTests(unittest.TestCase):
    def test_records_reproducible_context_and_radio_identity(self):
        with tempfile.TemporaryDirectory() as temp_name:
            temp = Path(temp_name)
            context = temp / "context"
            context.mkdir()
            (context / "manifest.json").write_text(
                json.dumps({
                    "schema": 1,
                    "contextId": "sha256:context-id",
                    "board": {"id": "sticks3", "label": "StickS3"},
                    "hardware": {"mcu": "esp32s3"},
                }),
                encoding="utf-8",
            )
            (context / "sdkconfig.defaults").write_text("CONFIG_TEST=y\n")

            first_digest = MODULE.build_context_digest(context)
            (context / "sdkconfig.defaults").write_text("CONFIG_TEST=n\n")
            second_digest = MODULE.build_context_digest(context)
            self.assertNotEqual(first_digest, second_digest)

            with patch.object(
                MODULE,
                "git_revision",
                side_effect=("firmware-commit", "idf-commit"),
            ):
                evidence = MODULE.hardware_lab_evidence(
                    context,
                    idf_path=temp / "esp-idf",
                    transport_target="/dev/ttyACM0",
                    router_label="lab-router",
                    peer_label="lab-peer",
                    channel_band="channel-6-2.4GHz",
                    test_command="remote.py test --csi-hardware",
                )

        self.assertEqual(evidence["schema"], 1)
        self.assertEqual(evidence["firmwareCommit"], "firmware-commit")
        self.assertEqual(evidence["idfRevision"], "idf-commit")
        self.assertEqual(evidence["target"], "esp32s3")
        self.assertEqual(evidence["board"]["id"], "sticks3")
        self.assertEqual(evidence["buildContext"]["id"], "sha256:context-id")
        self.assertTrue(evidence["buildContext"]["digest"].startswith("sha256:"))
        self.assertEqual(evidence["radio"]["router"], "lab-router")
        self.assertEqual(evidence["radio"]["peer"], "lab-peer")
        self.assertEqual(evidence["radio"]["channelBand"], "channel-6-2.4GHz")
        self.assertEqual(evidence["testCommand"], "remote.py test --csi-hardware")


if __name__ == "__main__":
    unittest.main()
