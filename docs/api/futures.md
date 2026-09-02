# Futures

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
