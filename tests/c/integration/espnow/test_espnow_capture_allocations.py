"""Production open capture, pools and cleanup with deterministic SDK failures."""
from tests.support.fixtures import fixture_text
import pathlib
import re
import sys
import tempfile
import unittest
from tests.support.wireless_vm_fixture import ROOT, CORE, build, extract, run
SDK=fixture_text('espnow/test_espnow_capture_allocations/sdk.inc')
BOUNDARIES=fixture_text('espnow/test_espnow_capture_allocations/boundaries.inc')
MAIN=fixture_text('espnow/test_espnow_capture_allocations/main.inc')
class EspnowCaptureAllocations(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory();cls.addClassCleanup(cls.temp.cleanup)
        now=(ROOT/'components/esp32_mquickjs/src/modules/espnow/esp32_mquickjs_espnow.c').read_text()
        names=['espnow_clear_peer','espnow_reset_session_storage','espnow_begin_close','espnow_close_failure','espnow_restore_power_save','espnow_finish_close','espnow_close_native','espnow_allocate_receive_pool','espnow_allocate_tx_queue','espnow_open_release','espnow_closed_and_quiescent','espnow_open_capture']
        bodies='\n'.join(extract(now,n) for n in names)
        fields=set(re.findall(r'session->(\w+)',bodies))
        special={'interval_token':'esp32_mquickjs_wifi_interval_token_t ','rx_slots':'espnow_rx_slot_t *','rx_payloads':'uint8_t *','tx_payloads':'uint8_t *','tx_staging':'uint8_t *','tx_task_stack':'uint8_t *','tx_packets':'espnow_tx_packet_t *','tx_links':'esp32_mquickjs_espnow_tx_slot_link_t *','tx_queue':'esp32_mquickjs_espnow_tx_queue_t ','broadcast_rate_config':'espnow_peer_rate_config_t ','event_queue':'esp32_mquickjs_event_queue_t *','runtime':'void *','tx_task':'_Atomic(void *) ','active_send':'_Atomic(esp32_mquickjs_future_driver_state_t *) ','pending_tracked_send':'_Atomic(esp32_mquickjs_future_driver_state_t *) ','active_close':'_Atomic(esp32_mquickjs_future_driver_state_t *) ','rx_free':'esp32_mquickjs_native_pool_t ','radio_lease':'esp32_mquickjs_wifi_radio_lease_t ','cleanup_stage':'_Atomic(const char *) ','cleanup_scheduled':'atomic_bool '}
        native='typedef struct { uint8_t pmk[16];espnow_peer_slot_t peers[1];'+''.join(special.get(n,'atomic_uint ')+n+';' for n in sorted(fields-{'pmk','peers'}))+'} espnow_session_t;\n'
        secure=extract((CORE/'esp32_mquickjs_wireless_core.c').read_text(),'esp32_mquickjs_wireless_secure_zero')
        cls.binary=build(cls.temp.name,SDK+native+BOUNDARIES+(CORE/'esp32_mquickjs_native_pool.c').read_text()+(ROOT/'components/esp32_mquickjs/src/modules/espnow/esp32_mquickjs_espnow_tx_queue.c').read_text()+secure+bodies,MAIN)

    def test_all_open_allocations_queue_retain_radio_reservation_and_secret_cleanup(self):
        for capacity in (0,2):
            for gc in (0,1):
                with self.subTest(capacity=capacity,gc=gc):run([str(self.binary),str(capacity),str(gc)])

    def test_capture_rechecks_native_cleanup_reservation_after_option_getters(self):
        run([str(self.binary), '0', '0', 'parse-busy'])
