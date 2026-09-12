# Futures

In a build enabling Wi-Fi, BLE or ESP-NOW, the shared Future service reserves
storage in the `wireless.runtime` budget described in [system memory](sys.md).
This includes shared receive bookkeeping and non-wireless Future calls; individual
wireless drivers can have their own allocation owner. Combinator roots and public
handles remain charged until released. Worker stacks have boot lifetime and remain
in the ledger across runtime restart. The shared pool accepts work only after all
workers have been created; partial initialization retains created workers and
retries only the remaining creations. Runtime/RF validation is pending.

Native method registrations use stable chunks of 16 entries, allocated through
the same runtime control allocator as methods are registered, with a maximum of
256 registrations. Growing the registry does not move existing GC references;
small feature sets do not reserve the full maximum. Registration OOM or capacity
failure fails initialization visibly. Teardown removes roots and returns chunks
after active native Futures have retired.

- `Future.call(fn, thisValue?, args?)`
  Queue a callable without invoking it before return. It starts at the next
  scheduler idle point and remains runtime-owned through settlement. For a
  registered native driver, the call synchronously validates and captures
  immutable arguments, generations, buffers, and leases before returning, but
  does not start I/O or wait on hardware. Capture errors produce an already
  rejected Future.
- `Future.all(futures)`
  Fulfill with results in input order. Other inputs are not cancelled when one fails.
  The combinator handles rejection from every attached input.
- `Future.race(futures)`
  Settle as `{ index, value }` from the first input without cancelling the rest.
  All inputs retain a rejection handler after the race settles, so a losing
  input that rejects later is not reported as unobserved.
- `Future.sleep(ms)`
  Return a timer-backed Future.
- `Future.timeout(future, timeoutMs)`
  Apply an operation deadline and cancel the input if it expires. Input
  rejection is handled by the timeout wrapper even when it settles first.
- `future.status()` / `future.wait(timeoutMs?)` / `future.cancel()`
  Inspect, cooperatively wait for, or cancel a Future. A wait timeout does not
  cancel the operation. Cancelling queued work settles it immediately. A
  running native operation may reject cancellation (`false`), confirm it
  immediately (`true` and `"cancelled"`), or accept a request (`true`) while
  remaining `"pending"` until its driver reports that teardown is complete.
- `future.map(fn)`
  Transform a fulfilled value without flattening a returned Future. Rejections
  and cancellations propagate without invoking `fn`.
- `future.flatMap(fn)`
  Chain an operation whose callback returns another Future. Returning any other
  value rejects the chained Future.

Queued and pending operations occupy the bounded public Future table. On
settlement, status and result move to the JavaScript handle so the scheduler
slot is immediately reusable. Synchronous native adapters use a separate
reserved slot pool, keeping transport and cancellation paths responsive when
public Future capacity is full.

A native method can define an empty receive timeout as a successful `null`
result, or report its own structured timeout error. The shared scheduler roots
that result before cancellation/cleanup and preserves the native driver's
storage retirement rules. This does not change `Future.timeout()` or
`future.wait(timeoutMs)`: their deadlines retain the semantics described above.

A native driver may expose a non-null resource key. Operations with equal keys
enter a bounded FIFO lane: the core captures their arguments and leases at
`Future.call()` time, but starts only one operation for that resource at a
time. Waiting operations do not occupy worker tasks, while drivers with
different keys may run concurrently. The per-resource waiting limit is
`CONFIG_ESP32_MQUICKJS_FUTURE_RESOURCE_LANE_QUEUE_LEN`; exceeding it rejects
the new Future with `Future resource lane queue is full`. Cancelling a queued
Future releases its captured state without starting the driver.

Finite synchronous hardware operations are cooperative scheduler yield points.
While one is waiting for an interrupt, readiness event, or timeout, timer
callbacks and ready Futures may run before the hardware method returns. This is
single-threaded cooperative re-entry, not parallel JavaScript execution. Idle
application jobs are not started from a nested synchronous wait.

Examples:

```js
var scan = Future.call(wifi.scan, wifi, []);
var request = Future.call(http.fetch, http, ["https://example.com"]);
var values = Future.all([scan, request]).wait(10000);
print(values[0].length, values[1].status);
```
