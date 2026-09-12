# Event queues

`EventQueue` represents a bounded stream of externally produced events. Each
queue supports at most one pending receiver.

- `queue.receive(timeoutMs?)`
  Return the next event. With no argument it waits cooperatively without a
  deadline; `0` performs a non-blocking check. A closed and drained queue
  returns `null`.
- `queue.stats()`
  Return `{ open, queued, capacity, dropped, highWater, receiverPending }`.
  `highWater` is the maximum number of queued events since creation or the last
  `wifi.diagnostics.resetFrameworkCounters()` affecting this runtime. Enqueue,
  dequeue and reset serialize the observation, including callback/ISR producers.
  Drop callbacks still run outside that lock. `dropped` saturates at UINT32_MAX;
  reset clears drops and starts the peak at the current queue depth, without
  removing events or changing a pending receiver. Closing retains history.

  `sys.status.resources.eventQueues` and Wi-Fi diagnostics aggregate registered
  queues in the active runtime: open, dropped, queued, capacity and highWater.
  The last field sums each queue's peak; it is not a simultaneous runtime peak.
  Aggregate integer counters saturate at UINT32_MAX. Unregistered orphan queues
  pending native release are outside this registered-queue summary/reset.
- `queue.close()`
  Stop the source and wake a pending receiver. Closing does not discard events
  already queued; they remain receivable until the queue is drained.

Cancelling a Future created from `queue.receive` does not close the source.
Each native producer declares whether overflow drops the newest or oldest
event, and events that own native payloads are released on drop and finalizer
drain paths.
