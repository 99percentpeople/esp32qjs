# Timers

These timer Global Helpers use generation-checked opaque handles.

- `setTimeout(fn, ms)`
  Run `fn` once after `ms` milliseconds and return an opaque timer handle.
- `clearTimeout(handle)`
  Cancel a timeout only when the handle still identifies that timer. A stale
  handle cannot cancel a newer timer that reused the same native slot.
- `setInterval(fn, ms)`
  Run `fn` repeatedly and return an opaque timer handle. Periods below
  `CONFIG_ESP32_MQUICKJS_MIN_INTERVAL_MS` are clamped to that configured
  minimum. An interval is canceled automatically if its callback throws or
  exceeds the JavaScript callback deadline. Each scheduler turn consumes only
  the timer events that were ready when that turn began; events that become
  ready during a callback run on a later turn so an overdue interval cannot
  starve Futures, EventQueues, or runtime idle work.
- `clearInterval(handle)`
  Cancel an interval using the same generation-checked handle semantics.

Example:

```js
var handle = setInterval(function () { print("tick"); }, 500);
clearInterval(handle);
```
