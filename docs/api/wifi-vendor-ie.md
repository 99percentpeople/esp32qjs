# Wi-Fi Vendor IE

Candidate configuration and observation API under the existing `wifi` feature.
`set`, `clear`, `status`, and `watch` use the sole v1.

## wifi.vendorIe.set(options)

Requires an initialized healthy Radio with the selected interface enabled in its
configured mode, either STARTED or fully STOPPED. It does not initialize, change
mode, start or associate. SoftAP frame types require SoftAP support. Options are strict; unknown keys, coercions and invalid combinations
are rejected before any Radio or SDK mutation.

| Field | Contract |
| --- | --- |
| interface | `station` or `access-point` |
| frame | Station: `probe-request`, `association-request`; AP: `beacon`, `probe-response`, `association-response` |
| index | Integer `0` or `1`, independently for each frame type |
| enabled | Required boolean |
| data | Required ByteSource when enabled; omitted when disabled. Complete single IE: `0xdd`, length byte, 3-byte OUI, OUI type, optional payload. Total 6..257 bytes, exactly length+2 |

Captures a native copy before entering the driver; array getters may throw or
trigger GC without retaining a stale source pointer. The SDK copies the element
before returning. No source or JS root remains retained after the call.

Returns `WiFiVendorIeStatus`. An enabled or uncertain slot must first be explicitly
cleared before enabling again, even for identical bytes. There is no implicit
replace, rollback or automatic retry. A failed SDK enable retains an uncertain
slot because failure alone does not prove absence. Result allocation can fail
after a successful write: read `status()` before deciding the next action.

One exact Radio lease per configured interface protects its occupied/uncertain
slots. They block stop, incompatible configuration and physical restart until
cleared. They do not select a channel or implicitly reopen a helper. SDK writes
participate in the Radio STOP-history invalidation boundary.

For pre-start data, configure the interfaces with `start: false`, set the IEs, then
call `wifi.start()` with the same mode and storage (or omit them). Start accepts
only known slots and exact framework owners; changing mode/storage or introducing
unrelated owners still fails admission. An exclusive start token temporarily owns
the parked slots while helper registrations are prepared. This path checks mode
without rewriting configuration. Interface leases are restored before native START,
so configured IEs are available to the first frames. No IE is cleared/reinstalled.
Identity capacity is checked before admission and again before START.

A start failure retains this token and its IE cleanup obligation. `wifi.stop()` or
runtime teardown removes its remaining slots before retiring the helpers/driver;
failed removal remains retryable with the same token. `clear()` cannot interfere
with an active lifecycle. During transfer `startPending` is true and the ordinary
`owners` count can be zero; the driver copies are still protected. Ordinary
configuration and physical restart cannot borrow this start-only reservation.

## wifi.vendorIe.clear(interface?)

Clears framework-owned slots for `station`, `access-point`, or both when omitted.
An empty selection is a no-op, including before initialization. Successful slots
are retired immediately; failures retain only unfinished slots and their lease.
Repeat `clear()` explicitly to retry those slots. Removal is allowed during an
unrelated driver fault without clearing that fault. Runtime teardown also attempts
this cleanup and records `vendor-ie-clear` on failure; a new runtime retries before
attaching its consumer. It never discards retained ownership merely to finish JS
teardown. These SDK slots are exclusively managed by the framework. Direct native
SDK writers sharing the same slots are unsupported: the SDK has no getter with
which to identify or preserve an external predecessor.

## wifi.vendorIe.watch(options?)

Returns `EventQueue<WiFiVendorIeEvent>`. Options are strict:

| Field | Contract |
| --- | --- |
| oui | Omit for all OUIs; otherwise one `xx:xx:xx` string or an array of 1..8 unique strings. Hex is case-insensitive; duplicate normalized OUIs, malformed strings and coercions are rejected |
| capacity | Integer 1..32, default 8. Drop newest on full |

There may be four active subscriptions, eight retained native queue handles, and
64 reserved event slots in total. Closing detaches the subscription immediately;
its capacity/handle charge remains until the queue's native destruction, including
pending receives, reaper work and retained closed JS handles. Repeated close/open
cannot bypass that charge. Release references and allow GC to reclaim closed
queues before opening more when the retained budget is full.

A watch needs an active runtime but does not initialize/start Wi-Fi, select a
channel, associate, enable promiscuous mode or hold an RF owner. It can be opened
before `wifi.start()`. The framework registers one physical callback during Radio
initialization, even with no subscribers. Queues survive ordinary Wi-Fi stop and
physical restart; delivery depends on what the current driver actually receives.
Runtime teardown detaches and closes all subscriptions.

The callback copies a complete 6..257-byte IE into each matching queue, without
retaining SDK pointers. It never waits for a subscriber: a busy subscription mutex
or full queue drops the new observation. There is no separate ingress pool. These
limits bound this module only; the cross-module W-09 memory budget remains pending.
`reservedCapacity * eventBytes` describes event-slot storage, not total heap usage:
EventQueue also owns queue metadata, receive/drain scratch and allocator overhead.

| Event field | Meaning |
| --- | --- |
| sequence | Boot-scoped exact integer, at most 2^53-1; allocated once per valid captured callback, shared by matching subscribers. Gaps include filtering and queue drops; busy/invalid callbacks do not allocate a sequence |
| timestampUs | Local `esp_timer` microseconds at capture; not the over-air timestamp |
| radioGeneration | Physical Radio generation that admitted this callback |
| frame | `beacon`, `probe-request`, `probe-response`, `association-request` or `association-response` |
| sourceMac | Lowercase colon-separated source MAC |
| rssi | SDK RSSI value |
| oui | Lowercase colon-separated three-byte OUI |
| vendorType | Byte following the OUI |
| data | Independent JS `number[]` containing the full IE, including `0xdd` and length byte |

The SDK callback provides neither a receiving interface nor a configured IE slot
index; neither field is inferred. A copied event queued before driver restart may
still be received afterward, carrying its original `radioGeneration`. Receive
conversion failure follows EventQueue consumption semantics; it does not retry or
retain a native payload. Sequence exhaustion stops new capture/open admission until
device reboot. Watch counters also persist across runtime restart.

Callback admission checks the exact non-reused Radio generation. Physical teardown
first stops acceptance, unregisters the callback, then requires entered callbacks
to retire before deinit. Failure retains diagnostics and the unfinished cleanup
suffix; a successful unregister is not repeated merely because draining is pending.
A rejected late callback does not dereference SDK data. Registration failure is
reported as Radio fault `vendor-ie-register`; teardown uses `vendor-ie-unregister`
or `vendor-ie-drain`. This shared callback slot is framework-owned; external native
callback writers are unsupported.

The pinned C3/S3/C5 SDK archives require a build-local context-field fix: the
original ioctl handler forwards the field address instead of its value. Build
configuration verifies exact reviewed archive hashes and rejects unreviewed SDK
inputs. See the [implementation evidence](../investigations/2026-09-09-w08-vendor-watch.md).

## wifi.vendorIe.status()

Returns `radioGeneration`, `owners`, `startPending`, `driverPayloadBytesUpperBound`
, `slots` and `watch`. `owners` counts active interface leases; `startPending` identifies
copies held by the exclusive startup token.
Each slot has `interface`, `frame`, `index`, `state` (`empty`, `enabled`, `uncertain`),
`byteLength` (0 when empty, null when uncertain) and `espCode` (null without a slot
error). Ten slots are reported, or four when SoftAP is disabled. The payload bound
uses the accepted length or 257 for an uncertain slot; it excludes SDK metadata,
allocator overhead and transient messages. No driver getter exists: these are
framework ownership records, not externally written settings or RF delivery proof.

`watch` contains `subscribers`, `retainedHandles`, `reservedCapacity`, `eventBytes`,
`capacityLimit` (64), `subscriberLimit` (4), `retainedHandleLimit` (8), `sequence`,
`sequenceExhausted`, `droppedBusy`, `invalid`, `filtered`, `droppedQueue`,
`radioGeneration` (0 before registration or after reset), `registered`, `registrationUncertain`,
`unregisterWritten`, `callbacksActive` and `registrationError` (null without error).
Counters saturate at 2^32-1; `filtered` and `droppedQueue` count per subscriber,
while `droppedBusy` and `invalid` count callbacks. Broker and subscription fields
are separate bounded snapshots, not an atomic observation of the entire driver.

Native failures throw `WiFiVendorIeError`: code `WIFI_VENDOR_IE_FAILED`, operation
`wifi.vendorIe.set` or `wifi.vendorIe.clear`, and details containing `espCode`,
`espName`, and a current `status` snapshot. Invalid JS inputs throw TypeError.

```js
wifi.configure({mode: "station", start: false});
var ie = [221, 4, 18, 52, 86, 1];
wifi.vendorIe.set({interface: "station", frame: "probe-request", index: 0,
                  enabled: true, data: ie});
wifi.start();
console.log(wifi.vendorIe.status());
wifi.vendorIe.clear("station");
```

No RF, peer receipt, callback reception, lifecycle, or soak acceptance is implied
by the Candidate API. Stage testing follows completion of the Wi-Fi API work.
