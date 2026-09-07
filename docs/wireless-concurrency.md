# Wireless Concurrency and Teardown

Status: implementation contract for the BLE and ESP-NOW v1 native adapters.

JavaScript runs on the ESP32QJS runtime task, while NimBLE and ESP-NOW invoke
callbacks from ESP-IDF-owned tasks. Callback code must not allocate JavaScript
values or retain pointers to JavaScript-owned storage. It may copy data into a
bounded native pool, update synchronized native state, enqueue a fixed-size
event, and wake an already registered Future.

## State ownership

| State | Writer | Reader | Synchronization and lifetime |
| --- | --- | --- | --- |
| BLE adapter lifecycle and generation | Runtime and close worker | Runtime and NimBLE host callbacks | C11 atomics; callbacks accept only opening, active, or closing generations |
| BLE scan/advertise lifecycle and counters | Runtime and NimBLE host callbacks | Runtime status and event conversion | C11 atomics; event payloads are copied into fixed native pools before enqueue |
| BLE connection scalar status | Runtime and NimBLE host callbacks | Runtime, Future drivers, and callbacks | C11 atomics for allocation, generation, handle, role, MTU, RSSI, and open/close state |
| BLE peer address and security state | Runtime and NimBLE host callbacks | Runtime status and security-event conversion | `s_ble.lock`; readers copy one native snapshot under the lock, then perform NVS and JavaScript work after unlocking |
| BLE Future callback references | Runtime and NimBLE host callbacks | Both tasks | `s_ble.lock` protects registry lookup/retain/removal and active-state publication; native ownership holds a reference independently of callback execution and JS roots; detached storage is freed only after both native and callback references reach zero |
| BLE GATT Server values | Runtime and NimBLE host callbacks | Both tasks | Per-characteristic FreeRTOS critical section; remote access uses the native cache and never calls JavaScript |
| ESP-NOW session lifecycle, generation, counters, and active operations | Runtime, worker, and Wi-Fi callbacks | All three contexts | C11 atomics; peer and radio mutations remain serialized by the native Future resource lane |
| ESP-NOW receive payloads | Wi-Fi callback | Runtime event conversion | Fixed native pool; the queued event contains only generation and slot index, and drop/discard returns the slot |
| ESP-NOW EventQueue native pointer | Runtime, Wi-Fi callback, and close worker | Wi-Fi callback and runtime | A native retain is held from successful open until quiescent close, so JS disposal or finalization cannot free callback-visible storage |
| Wi-Fi CSI pool slots | Wi-Fi callback, EventQueue, and runtime objects | All three contexts | Publish creates the sole event owner; dequeue transfers it once to a Frame or Batch; drop/discard and public close release it once. Views and sources add native retains, while owned copies do not retain slots. |
| Wi-Fi CSI fixed channel | Shared radio service | Station, SoftAP, ESP-NOW, and CSI clients | The owner is an exact lease identity, not merely a client kind. Repeating the same channel on that lease is idempotent; every other lease conflicts without mutating radio state. |

## Teardown invariants

BLE adapter close changes the lifecycle to closing, requests GAP procedures and
connections to stop, stops and deinitializes the NimBLE host in a worker, and
only then releases callback-visible pools and queues. Reopen is delayed until
the prior native deinitialization quiescence window has elapsed.

ESP-NOW close has two phases:

1. Mark the session closing, close its EventQueue, and unregister receive and
   send callbacks. No callback-visible storage is freed in this phase.
2. Wait until the atomic active-callback count reaches zero, deinitialize
ESP-NOW, release the Wi-Fi radio lease, discard queued payloads, free native
pools, and release the EventQueue native retain.

Wi-Fi CSI close first stops admission, disables CSI, unregisters the callback,
and waits for the active callback count to reach zero. It then discards queued
events through the same event-owner release path. An event already dequeued by
a receive Future is dropped by Future destruction if JS conversion never ran.
The old pool is destroyed only after callback, event/public owner, and all
View/Source native retains have reached zero; generations are never reused.

If an explicit ESP-NOW close exceeds its Future deadline, cleanup continues in
the worker and the session remains closing. The caller receives
`ESPNOW_CLEANUP_PENDING`; it must not treat the handle as closed and immediately
reuse callback-visible storage. Runtime teardown and finalizer fallback use the
same two-phase path.

An ESP-NOW send timeout follows the same callback-quiescence rule. The timeout
path unregisters both callbacks, marks recovery pending, and returns
`ESPNOW_RECOVERY_PENDING` to JavaScript while a Future-owned worker waits for
the active callback count to reach zero. Only then may it deinitialize and
restore ESP-NOW, mark the native driver complete, and release the send state.

## Review rules

- Do not replace atomics or critical sections with `volatile`.
- Do not create JavaScript values, call JS functions, or allocate variable-size
  payloads from wireless callbacks.
- Do not free or recycle a pool, queue, Future state, or handle while a native
  callback can still reference it.
- Any new callback-visible field must be added to this ownership table and use
  the existing synchronization domain, or introduce one explicitly.
- Queue overflow and cancellation paths must return native pool slots exactly
  once.

BLE scan reports and notification values use the shared pooled-event ISR
publisher. A successful enqueue transfers the slot to the EventQueue drop/event
converter; any rejected enqueue returns it immediately. Host saturation tests
exercise this same production helper for 100 publishes and verify all 98
rejected slots are reusable.


## First-stage Radio and BLE operation isolation

Radio uses a boot-lived task mutex for lease validation, driver mutation, and
release, plus a short critical section for snapshots. Driver calls never run
under the snapshot critical section. The live lease registry is bounded at 16
entries and IDs never wrap; fixed-channel ownership is still exclusive. A failed
once initialization/mode/start records its original stage/error and prohibits
repeat initialization. Runtime restart does not clear this fault.

BLE GATT callbacks pass a boot-scoped operation ID through NimBLE's `arg`.
The bounded native registry lookup and callback retain occur under `s_ble.lock`.
Removing JS roots neither releases the native reference nor makes the ATT lane
available. A detached discovery callback aborts its original procedure before
accessing the cache. Native termination removes the ID; stale IDs cannot resolve
a later operation even when registry entries or connection slots are reused.
Successful Host stop/deinit drains remaining native references before pool release.
Pair/close and indication paths without per-request cookies retain their lane until
native termination. Indication submission is not confirmation; pending recipients
are tracked individually and synchronous submission callbacks cannot finish a
broadcast partway through its submission loop.

Wi-Fi ESP event-loop callbacks commit native control state and Future results
without traversing the lossy deferred-driver queue. Timer callbacks only publish a
native timeout obligation; the runtime poller executes its driver mutation. None
of these callback paths executes JS. CSI still admits only one unfreed pool;
retained data blocks reopen, not public session close.
