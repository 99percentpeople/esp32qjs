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
| BLE Future callback references | Runtime and NimBLE host callbacks | Both tasks | `s_ble.lock` plus atomic active-state publication; detached storage is freed only after its callback reference count reaches zero |
| BLE GATT Server values | Runtime and NimBLE host callbacks | Both tasks | Per-characteristic FreeRTOS critical section; remote access uses the native cache and never calls JavaScript |
| ESP-NOW session lifecycle, generation, counters, and active operations | Runtime, worker, and Wi-Fi callbacks | All three contexts | C11 atomics; peer and radio mutations remain serialized by the native Future resource lane |
| ESP-NOW receive payloads | Wi-Fi callback | Runtime event conversion | Fixed native pool; the queued event contains only generation and slot index, and drop/discard returns the slot |
| ESP-NOW EventQueue native pointer | Runtime, Wi-Fi callback, and close worker | Wi-Fi callback and runtime | A native retain is held from successful open until quiescent close, so JS disposal or finalization cannot free callback-visible storage |

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
