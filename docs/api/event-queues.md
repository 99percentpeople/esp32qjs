# Event queues

`EventQueue` represents a bounded stream of externally produced events. Each
queue supports at most one pending receiver.

- `queue.receive(timeoutMs?)`
  Return the next event. With no argument it waits cooperatively without a
  deadline; `0` performs a non-blocking check. A closed and drained queue
  returns `null`.
- `queue.stats()`
  Return `{ open, queued, capacity, dropped, receiverPending }` for this queue.
  `dropped` is cumulative for the queue lifetime.
- `queue.close()`
  Stop the source and wake a pending receiver. Closing does not discard events
  already queued; they remain receivable until the queue is drained.

Cancelling a Future created from `queue.receive` does not close the source.
Each native producer declares whether overflow drops the newest or oldest
event, and events that own native payloads are released on drop and finalizer
drain paths.
