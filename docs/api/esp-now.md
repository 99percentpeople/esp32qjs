# `espNow` Module

`espNow` exposes generic station-interface ESP-NOW transport when
`sys.info.features.espNow` is enabled. It provides peer management, bounded
receive delivery, direct and queued transmission, per-peer PHY configuration,
power-save control, and explicit timeout recovery.

- `espNow.capabilities()` returns v1/target/ESP-IDF identity, the configured
  peer, encrypted-peer, and payload limits. `peerRateConfig` is `true` and
  `broadcastRateConfig` is `true`. `session.addPeer()` accepts an optional
  explicit `{ phyMode, mcs,
  guardInterval, ersu?, dcm? }` rate configuration. HT accepts MCS 0..7; HE20
  is target-gated and accepts MCS 0..9. ERSU/DCM are HE20-only. The selected
  rate is immutable on the peer handle and is automatically restored after
  `peer.update()` and timeout-driven native rebuild; remove/close clears it.
- `espNow.open(options?)` opens the only session in the runtime through a native
  Future. Options are `interface: "station"`, `channel: "current" | 1..14`,
  `maxPayloadBytes`, `receiveCapacity`, `sendTimeoutMs`, an optional 16-byte
  `pmk`, optional `{ wakeWindowMs, wakeIntervalMs }` power-save settings,
  optional `broadcastRateConfig: { phyMode, mcs, guardInterval, ersu?, dcm? }`,
  and an optional fixed native transmit queue
  `{ txQueue: { capacityPackets, overflow? } }`. `overflow` is
  `"reject-newest"` by default or `"drop-oldest-batch"`.
  ESP32-C3/C5/S3 builds enable ESP-NOW v2 by default with a 1470-byte default
  and maximum payload. Set `maxPayloadBytes: 250` only for explicit v1 peer
  compatibility. The fixed internal receive pool reserves
  `receiveCapacity * maxPayloadBytes` payload bytes, so applications should
  choose the queue capacity with that allocation in mind. The transmit worker
  uses one reserve-checked internal staging packet; queued transmit payloads
  are PSRAM-backed when PSRAM is available and use a reserve-checked internal
  fallback otherwise.
  The broadcast rate is applied to the native broadcast peer after open and
  restored after explicit timeout recovery. Broadcast uses single-attempt
  fire-and-forget delivery.
- `session.receive(timeoutMs?)` and `session.stats()` expose its bounded
  DROP_NEWEST receive EventQueue. Each event contains normalized source and
  destination addresses, RSSI, channel, sequence, timestamp, broadcast flag,
  and an owned `ByteView`; close that view after use.
- `session.addPeer(options)`, `peer.update(options)`, and `peer.remove()` are
  native Future operations. Encrypted peers require both an explicit session
  PMK and a 16-byte LMK. Keys never appear in status, snapshots, or errors.
- `session.peer(address)` returns a generation-checked handle or `null`, while
  `session.peers()` returns key-free status snapshots.
- `peer.send(data, options?)` and `session.broadcast(data, options?)` share one
  FIFO transmit lane. Results contain address, bytes, completedAtUs and
  `completion`, following [shared TX completion](tx-completion.md). Its native
  domain is `esp_now_send_status_t`; retain code/name including unknown numbers.
  MAC failure returns `completion.status: "failed"` normally. Broadcast success
  provides no receiver count or application acknowledgement.
- `peer.enqueue(packet)`, `peer.enqueueBatch(packets)`,
  `session.enqueueBroadcast(packet)`, and
  `session.enqueueBroadcastBatch(packets)` are synchronous fire-and-forget
  admission calls available only when `txQueue` was configured. A packet is
  `{ data: ByteSource | ByteSpanSource }` or `{ parts: [...] }`; bytes are
  copied into fixed native slots before the call returns, so sources may be
  closed or reused immediately. Batch admission is all-or-none, packets from
  one batch remain contiguous, and `drop-oldest-batch` evicts only whole
  batches that have not started. Each admitted packet receives one native send
  attempt. `session.flushTx(timeoutMs?)` waits for this native queue to drain.
  `status().txQueue` reports capacity, depth,
  high-water, admission and eviction counters. `completedPackets` counts MAC
  callbacks; `succeededPackets`, `failedPackets` and `unknownPackets` split those
  callbacks. `submittedPackets` counts SDK acceptance; `submitRejectedPackets`
  counts admission/SDK failure after queue acceptance. `rejectedPackets` and
  `rejectedBatches` are queue admission rejection before a native attempt.
  `settledPackets` and `settledBatches` count the completion worker's settlements,
  including submit rejection; they exclude queued timeout/discard cleanup.
  `timedOutPackets` counts queue deadlines, not proven physical aborts.
  `lastCompletion` retains the latest observed MAC completion with its native
  enum; `lastError` separately retains a nullable esp_err_t NativeCode for the
  latest submit/timeout error. Historical observations persist until session reset. Each admitted packet makes at most one native `esp_now_send()`
  call. A native admission failure, including `ESP_ERR_ESPNOW_NO_MEM`, marks
  that packet submit-rejected immediately; the queue does not delay and resubmit it.
- `session.setPowerSave(options)` borrows the shared Radio interval and sets the
  ESP-NOW wake window. `{ enabled: false }` restores the always-awake window and
  the captured Radio interval; it does not unconditionally overwrite the interval
  with zero. Close uses the same restoration suffix, including after failed open
  or partial configuration. Successful window restoration is not repeated if
  interval restoration fails. Interval ownership survives native ESP-NOW deinit.
  Errors use `ESPNOW_POWER_SAVE_FAILED` with the original `details.native` code. If a native
  setting is uncertain, the Session becomes failed and must close; `recover()`
  cannot repair this policy fault. Admission rejection before mutation preserves
  a healthy Session. `status().powerSave.faulted` and `restorePending` describe
  these obligations; the enabled/window/interval fields describe the last accepted
  request, not actual hardware state after failure.
- `session.recover()` explicitly rebuilds a faulted native session after send
  timeout cleanup has reached callback quiescence and successful native deinitialization. Each call makes one explicit
  rebuild attempt while preserving the shared Wi-Fi service and other radio
  owners.
- `session.close()` closes receive delivery, unregisters callbacks, deinitializes
  ESP-NOW, releases the shared radio lease, clears keys, and invalidates peers.
  It reports success only after those native steps finish. A failed unregister,
  power-save cleanup, deinit or Radio release reports `ESPNOW_CLEANUP_PENDING`
  with the underlying `details.native`; native state, queue retention and the
  unfinished cleanup suffix remain owned. Successful earlier steps are not
  repeated. The session stays closing, blocks normal operations/reopen, and a
  later `close()` can retry. The retained handle is retired only after closure;
  garbage collection requests native reaping without pretending cleanup succeeded.
  Background reaping and explicit close share one cleanup reservation. If another
  cleaner already owns it or the worker queue is full, close reports pending and
  preserves that cleaner. Reopen/runtime initialization also waits for the old
  cleanup reservation, close Future pointer and reaper to retire. Public Future
  timeout still ends waiting, not native
  ownership. Native callback drain follows successful unregister; deinit failure
  does not clear `now_initialized` or free queue/session storage.

`EspNowSession` is the generation-checked session handle returned by `open()`;
`EspNowPeer` is the generation-checked peer handle returned by `addPeer()` or
`peer()`. Their callable surface is `receive()`, `stats()`, `status()`,
`addPeer()`, `peer()`, `peers()`, `broadcast()`, `setPowerSave()`, `recover()`,
`close()`, `send()`, enqueue/batch/flush operations, `update()`, and `remove()`
as described above.

Use `channel: "current"` when Wi-Fi is connected. A conflicting explicit
channel returns `ESPNOW_CHANNEL_CONFLICT` while preserving the active shared
radio channel. Observed channel drift latches a fixed owner's conflict:
`status().channelSynchronized` becomes false, receive publication is rejected,
and new send/enqueue admission fails with the existing channel-mismatch error.
Tracked sends and queued packets recheck the Radio immediately before their
native submission. A conflict completes them as failed without calling
`esp_now_send()`; already submitted sends still finish through their original
callback/timeout path. Queue/flush accounting therefore remains bounded and
complete. A raw channel-observation failure retains its native error for TX.
Close and reopen a fixed session to explicitly revalidate its channel; driver
movement back to the old channel does not clear the conflict.

A `"current"` session updates its channel and channelGeneration on receive,
status, send/enqueue admission and worker dispatch. A peer's explicit channel
constraint still applies. These checks cannot make RF channel movement atomic
with an already accepted native send and do not establish over-the-air delivery.

A send callback timeout reports
`ESPNOW_RECOVERY_PENDING` while its native Future state and transmit lane remain
retained. A worker waits for callbacks to become quiescent, deinitializes the
native driver, and only then completes the Future with a timeout. The session
remains faulted with `recoveryRequired: true` until `session.recover()` performs
one native ESP-NOW rebuild. When native admission returns
`ESP_ERR_ESPNOW_NO_MEM`, a tracked send completes with `ESPNOW_SEND_FAILED` and
a queued packet is recorded as failed in `txQueue`. Single-attempt delivery
leaves backoff, acknowledgement, and retransmission policy to the application.

Session `sendSuccesses`, `sendFailures` and `sendUnknowns` count only correlated
MAC callbacks. `sendRejections` counts admission/SDK rejection of a native attempt;
`sendTimeouts` counts waiting deadlines separately. These diagnostics are live
observations, not a single atomic snapshot of all send and queue activity.
