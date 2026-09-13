"""Exercise production incoming-connection queue registration and drop paths."""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
import pathlib
import sys
import unittest

from tests.support.c_source import extract as function
from tests.support.native_compile import compile_run
BLE=TEST_ROOT/'components/esp32_mquickjs/src/modules/ble/esp32_mquickjs_ble.c'
FIXTURE=fixture_text('ble/test_ble_incoming_queue_regression/fixture.inc')
class BleIncomingQueueRegression(unittest.TestCase):
    def production(self):
        source=BLE.read_text()
        return "\n".join(function(source,name) for name in (
            "ble_terminate_unclaimed_connection","ble_advertiser_event_drop",
            "ble_publish_incoming_connection",
            "ble_discard_advertiser_connections","ble_release_advertiser"))

    def factory(self):
        source=BLE.read_text()
        capture=function(source,'ble_advertise_capture')
        start=capture.index('    queue = esp32_mquickjs_event_queue_new_wireless(')
        call=capture[start:capture.index(';',start)+1]
        return 'static void create(void) { void *ctx=NULL,*queue;unsigned capacity=2;ble_adapter_t *adapter=&s_ble;ble_advertiser_t *advertiser=&s_ble.advertiser;'+call+' (void)queue; }\n'

    def test_production_queue_registers_native_owner_drop(self):
        compile_run(self,FIXTURE+self.production()+self.factory()+r'''
int main(void) { create();assert(registered_drop!=NULL && registered_opaque==&s_ble.advertiser); }
''')

    def test_drop_is_idempotent_and_preserves_other_generations(self):
        compile_run(self,FIXTURE+self.production()+self.factory()+fixture_text('ble/test_ble_incoming_queue_regression/test_drop_is_idempotent_and_preserves_other_generations.inc'))

    def test_close_claims_popped_receive_event_but_not_transferred_connections(self):
        compile_run(self,FIXTURE+self.production()+fixture_text('ble/test_ble_incoming_queue_regression/test_close_claims_popped_receive_event_but_not_transferred_connections.inc'))

    def test_closed_connection_and_stopped_host_need_no_native_mutation(self):
        compile_run(self,FIXTURE+self.production()+fixture_text('ble/test_ble_incoming_queue_regression/test_closed_connection_and_stopped_host_need_no_native_mutation.inc'))

    def converter(self):
        # Object allocation and property APIs are fault boundaries; ownership
        # decisions execute the actual production converter and drop callback.
        return fixture_text('ble/test_ble_incoming_queue_regression/converter.inc')+function(BLE.read_text(),'ble_advertiser_event_to_js')

    def test_conversion_failures_drop_only_unclaimed_owner_and_retire_partial_handle(self):
        fixture=FIXTURE.replace('static void ble_advertiser_event_to_js(void) {}','')
        # Factory is not used here; its converter prototype is an SDK boundary.
        compile_run(self,fixture+self.production()+self.converter()+fixture_text('ble/test_ble_incoming_queue_regression/test_conversion_failures_drop_only_unclaimed_owner_and_retire_partial_handle.inc'))

    def test_late_receive_finish_after_close_cannot_transfer_or_discard_new_owner(self):
        fixture=FIXTURE.replace('static void ble_advertiser_event_to_js(void) {}','')
        compile_run(self,fixture+self.production()+self.converter()+fixture_text('ble/test_ble_incoming_queue_regression/test_late_receive_finish_after_close_cannot_transfer_or_discard_new_owner.inc'))

    def test_saturated_queue_terminates_only_its_unclaimed_incoming_connection(self):
        compile_run(self,FIXTURE+self.production()+fixture_text('ble/test_ble_incoming_queue_regression/test_saturated_queue_terminates_only_its_unclaimed_incoming_connection.inc'))

    def queue_future(self):
        source=(BLE.parents[2]/'core/esp32_mquickjs_event_queue.c').read_text()
        return fixture_text('ble/test_ble_incoming_queue_regression/queue_future.inc')+function(source,'event_queue_future_finish')+function(source,'event_queue_future_destroy')

    def test_production_receive_destroy_drops_dequeued_native_owner_once(self):
        fixture=FIXTURE.replace('static void ble_advertiser_event_to_js(void) {}','')
        compile_run(self,fixture+self.production()+self.converter()+self.queue_future()+fixture_text('ble/test_ble_incoming_queue_regression/test_production_receive_destroy_drops_dequeued_native_owner_once.inc'))

    def test_production_receive_finish_failure_owns_cleanup_before_destroy(self):
        fixture=FIXTURE.replace('static void ble_advertiser_event_to_js(void) {}','')
        compile_run(self,fixture+self.production()+self.converter()+self.queue_future()+fixture_text('ble/test_ble_incoming_queue_regression/test_production_receive_finish_failure_owns_cleanup_before_destroy.inc'))
