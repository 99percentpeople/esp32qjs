# Native Lifecycle Contracts

This document freezes the safety invariants shared by native modules. Product
policy belongs in JavaScript; these contracts remain mandatory regardless of
which JavaScript Library owns retry, recovery, pairing, or reconnect behavior.

## Object categories

| Category | Examples | Primary release | Native rule |
| --- | --- | --- | --- |
| Value | capabilities, status, discovery records | GC | No hardware or pool ownership. |
| Owned snapshot | owned `ByteView` | GC or idempotent `close()` | Bytes remain stable and immutable while open. |
| Reference | ESP-NOW peer, generation reference | GC | Finalization releases reference storage only; it performs no semantic delete. |
| Resource handle | EventQueue-backed transports, BLE connection, ESP-NOW session | explicit `close()` | Finalizer requests close; safe-point cleanup owns teardown. |
| Pool lease | camera frame and future pooled frames | explicit `close()` | A slot is returned exactly once after every derived retain is released. |
| Operation | Future driver state, pending TX/GATT work | terminal completion | Losing the JS Future does not free callback-visible native state. |

## Callback contract

An asynchronous driver, Wi-Fi, NimBLE, ESP event-loop, timer, or ISR callback
that runs outside the operation's owning runtime/worker task must not:

- allocate or free variable-sized payload storage;
- wait for a mutex, queue, Future, worker, or JS runtime;
- call JavaScript or create a JavaScript value;
- return a fixed-pool slot more than once.

Task callbacks use
`esp32_mquickjs_event_queue_try_send_from_callback()`. The v1 callback path is
`DROP_NEWEST` only and succeeds or fails immediately. The producer holds a
native retain or callback-active guard throughout every access to the queue,
pool, lease, or Future state. On rejection, the producer remains responsible
for returning its pool slot.

An ISR uses `esp32_mquickjs_event_queue_send_from_isr()` and propagates the
higher-priority-task wake flag. Normal runtime-task producers may use the
blocking `esp32_mquickjs_event_queue_send()` path.

WebSocket callbacks acquire a preallocated receive slot and publish to a fixed
internal queue. The runtime poller materializes an owned payload before it is
sent to the public EventQueue. Wi-Fi driver and timeout callbacks similarly
publish fixed records; state transitions and Future wake decisions run in the
runtime poller. `net.watch()` uses a zero-wait source-list lock and callback-safe
queue send, so contention drops a convergent notification instead of blocking.

A protocol callback invoked synchronously inside its already bounded native
worker operation, such as the HTTP response capture callback inside
`esp_http_client_perform()`, is worker implementation code rather than an
asynchronous producer. It may grow operation-owned response storage within the
request's configured limit, but it still cannot call JavaScript.

## Finalizer contract

A finalizer may atomically mark `orphaned`, `closed`, or `close_requested`, take
a bounded preallocated retain, register fixed-capacity reaper work, and notify
the runtime. It must not:

- wait or use `portMAX_DELAY`;
- run driver stop/deinit/rebuild operations;
- drain payloads or invoke arbitrary drop callbacks;
- allocate a JavaScript or variable-sized native object;
- report semantic success before callback-visible state is quiescent.

Explicit `close()` still attempts the operation immediately and reports its
result. A finalizer is only the leak fallback.

BLE finalizers use an adapter-scoped fallback: losing any live BLE resource
owner requests one conservative adapter cleanup. A runtime poller starts the
NimBLE teardown worker and releases pools only after native cleanup completes.
Finalizers never call NimBLE or wait themselves.

## EventQueue destruction ownership

EventQueue close rejects new producers before its source close callback runs.
Finalization holds one native reaper retain, and the runtime safe point invokes
the source close callback and drains queued payloads with a finite wait budget.

The following transition is indivisible:

```text
critical section:
  decrement final native retain, or mark dispose requested
  check receiver_registered == false
  check native_retain_count == 0
  check destroying == false
  set destroying = true and take destruction ownership
```

No caller may decrement the last retain, leave the critical section, and then
separately decide to destroy. `release()`, explicit dispose, and reaper
completion all use `event_queue_take_destroy_ownership_locked()` before
unlocking. Only the caller that changes `destroying` from false to true deletes
the queue, scratch storage, and send lock.

## Operation and timeout contract

Future driver storage remains native-owned until the operation is terminal and
all callbacks have exited. A user-visible timeout does not override this rule.
If callback quiescence is pending, the Future state and its resource lane stay
retained. Recovery may unregister callbacks and enter a faulted or recovering
state, but it cannot deinit/rebuild the driver or complete the Future after a
finite callback wait expires.

ESP-NOW follows the explicit v1 sequence:

```text
send timeout
  -> unregister send callback
  -> wait for callbacks_active == 0 in background cleanup
  -> deinit native session
  -> complete timeout Future and expose recoveryRequired
  -> JavaScript policy may call session.recover()
```

Each packet makes one native send attempt. `ESP_ERR_ESPNOW_NO_MEM` and other
native admission errors complete that operation without internal backoff or
retry. Firmware does not reset shared Wi-Fi or disconnect another radio owner
implicitly. Explicit recovery never retransmits an accepted or timed-out
message.

## Pool and lease contract

Pools are fixed-capacity and use an atomic free-set. Acquisition owns exactly
one slot; failed publication returns it immediately. A lease records its pool,
slot, generation, retain count, close request, and exactly-once return state.
Stale generation use fails instead of touching a recycled slot.

`ByteView` is an owned stable snapshot. Streaming or producer-backed data uses
`ByteSpanSource` or a dedicated frame/batch lease whose derived views retain the
owner.

## Reaper contract

The runtime orphan registry is fixed at
`ESP32_MQUICKJS_REAPER_CAPACITY`. Registration does not allocate. Polling is
round-robin, attempts at most `ESP32_MQUICKJS_REAPER_BATCH_LIMIT` entries per
turn, and uses `ESP32_MQUICKJS_REAPER_RETRY_MS` as the maximum individual wait.
A reaper never calls JavaScript. It returns true only when its cleanup ownership
can leave the registry safely; outstanding resource retains may complete final
storage destruction later through the same critical-section invariant.

One registry entry is reserved for the EventQueue runtime dispatcher. The
remaining entries are general-purpose module slots. The dispatcher owns active
and orphan EventQueue lists, so general reaper saturation cannot discard an
EventQueue finalizer request.

`sys.status().runtime.resources.orphans` exposes pending and capacity.

## Representative pre-refactor baseline

The following ESP-IDF 6.1 representative-profile artifacts were captured on
2026-08-29 before this refactor. `Total image` and static RAM are reported by
`esp_idf_size`; application binary sizes come from the CI partition check.

| Target | App binary | Total image | Static executable RAM |
| --- | ---: | ---: | ---: |
| ESP32-C3 | `0x20e110` | 2,169,911 B | 230,287 B DRAM |
| ESP32-C5 | `0x247250` | 2,403,655 B | 254,877 B HP SRAM |
| ESP32-S3 | `0x201f80` | 2,120,980 B | 241,350 B DIRAM + 16,384 B IRAM |

The comparison is a regression guard, not a promise that internal layout or
exact byte counts remain stable. Functional validation still requires host
tests, enabled/disabled target builds, and relevant hardware tests.

## Post-refactor local comparison

The same representative profiles were rebuilt on 2026-08-30 after the
lifecycle refactor. This is local build evidence for the uncommitted workspace,
not a substitute for CI or hardware validation.

| Target | App binary | App delta | Total image | Total delta | Static executable RAM |
| --- | ---: | ---: | ---: | ---: | ---: |
| ESP32-C3 | `0x213940` | +22,576 B (+1.05%) | 2,176,961 B | +7,050 B | 230,375 B DRAM (+88 B) |
| ESP32-C5 | `0x24ca50` | +22,528 B (+0.94%) | 2,410,689 B | +7,034 B | 254,973 B HP SRAM (+96 B) |
| ESP32-S3 | `0x2075b0` | +22,064 B (+1.05%) | 2,127,168 B | +6,188 B | 241,446 B DIRAM (+96 B) + 16,384 B IRAM |

All eight local firmware configurations passed: C3/C5/S3 minimal, C3/C5/S3
representative, S3 representative with PSRAM, and S3 with the component
disabled. The largest representative binary above still leaves at least 23%
of its 3 MiB application partition free.
