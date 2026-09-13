"""Production RF ingress/TX dispatch with controlled Radio/SDK boundaries."""
from tests.support.fixtures import fixture_text
import unittest
from tests.c.integration.wifi.station.test_wifi_scan_lifecycle import extract, ROOT
from tests.support.native_compile import compile_run
CSI = ROOT/'components/esp32_mquickjs/src/modules/wifi_csi/esp32_mquickjs_wifi_csi.c'
NOW = ROOT/'components/esp32_mquickjs/src/modules/espnow/esp32_mquickjs_espnow.c'
COMMON = fixture_text('wireless/test_wireless_channel_events/common.inc')
CSI_SDK = fixture_text('wireless/test_wireless_channel_events/csi_sdk.inc')
TX_SDK = fixture_text('wireless/test_wireless_channel_events/tx_sdk.inc')
def csi_code():
    source=CSI.read_text()
    helper=extract(source,'wifi_csi_channel_admit') if 'static bool wifi_csi_channel_admit(' in source else ''
    return COMMON+CSI_SDK+helper+extract(source,'wifi_csi_rx_callback')
def tx_code():
    source=NOW.read_text()
    helper=extract(source,'espnow_channel_admit') if 'static esp_err_t espnow_channel_admit(' in source else ''
    return COMMON+TX_SDK+helper+extract(source,'espnow_start_tracked_send')+extract(source,'espnow_start_queued_send')

class WirelessChannelEvents(unittest.TestCase):
    def test_csi_conflict_stops_callback_publication(self):
        compile_run(self,csi_code()+fixture_text('wireless/test_wireless_channel_events/test_csi_conflict_stops_callback_publication.inc'))

    def test_queued_send_conflict_produces_native_failure_without_tx(self):
        compile_run(self,tx_code()+fixture_text('wireless/test_wireless_channel_events/test_queued_send_conflict_produces_native_failure_without_tx.inc'))

    def test_csi_following_and_unknown_observation(self):
        compile_run(self,csi_code()+fixture_text('wireless/test_wireless_channel_events/test_csi_following_and_unknown_observation.inc'))

    def test_csi_fault_latches_without_overwriting_close(self):
        compile_run(self,csi_code()+fixture_text('wireless/test_wireless_channel_events/test_csi_fault_latches_without_overwriting_close.inc'))

    def test_tracked_send_conflict_finishes_without_native_callback(self):
        compile_run(self,tx_code()+fixture_text('wireless/test_wireless_channel_events/test_tracked_send_conflict_finishes_without_native_callback.inc'))

    def test_following_send_updates_channel_and_keeps_normal_completion(self):
        compile_run(self,tx_code()+fixture_text('wireless/test_wireless_channel_events/test_following_send_updates_channel_and_keeps_normal_completion.inc'))

    def test_native_observation_failure_retains_raw_tx_error(self):
        compile_run(self,tx_code()+fixture_text('wireless/test_wireless_channel_events/test_native_observation_failure_retains_raw_tx_error.inc'))

    def test_csi_status_refresh_follows_channel_and_preserves_cleanup_error(self):
        compile_run(self,csi_code()+extract(CSI.read_text(),'wifi_csi_refresh_channel')+fixture_text('wireless/test_wireless_channel_events/test_csi_status_refresh_follows_channel_and_preserves_cleanup_error.inc'))

    def test_csi_packet_channel_rejects_before_home_event_arrives(self):
        compile_run(self,csi_code()+fixture_text('wireless/test_wireless_channel_events/test_csi_packet_channel_rejects_before_home_event_arrives.inc'))
