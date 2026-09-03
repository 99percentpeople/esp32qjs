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
  FIFO transmit lane. `macDelivered` is the MAC result and is not an
  application acknowledgement.
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
  high-water, admission, eviction, completion, failure, and last ESP-IDF error
  counters. Each admitted packet makes at most one native `esp_now_send()`
  call. A native admission failure, including `ESP_ERR_ESPNOW_NO_MEM`, marks
  that packet failed immediately; the queue does not delay and resubmit it.
- `session.setPowerSave(options)` updates the station wake window and interval;
  `{ enabled: false }` restores the ESP-IDF always-awake/default interval
  settings. Closing an enabled session also restores those defaults before
  native deinitialization.
- `session.recover()` explicitly rebuilds a faulted native session after send
  timeout cleanup has reached callback quiescence. Each call makes one explicit
  rebuild attempt while preserving the shared Wi-Fi service and other radio
  owners.
- `session.close()` closes receive delivery, unregisters callbacks, deinitializes
  ESP-NOW, releases the shared radio lease, clears keys, and invalidates peers.

`EspNowSession` is the generation-checked session handle returned by `open()`;
`EspNowPeer` is the generation-checked peer handle returned by `addPeer()` or
`peer()`. Their callable surface is `receive()`, `stats()`, `status()`,
`addPeer()`, `peer()`, `peers()`, `broadcast()`, `setPowerSave()`, `recover()`,
`close()`, `send()`, enqueue/batch/flush operations, `update()`, and `remove()`
as described above.

Use `channel: "current"` when Wi-Fi is connected. A conflicting explicit
channel returns `ESPNOW_CHANNEL_CONFLICT` while preserving the active shared
radio channel. A send callback timeout reports
`ESPNOW_RECOVERY_PENDING` while its native Future state and transmit lane remain
retained. A worker waits for callbacks to become quiescent, deinitializes the
native driver, and only then completes the Future with a timeout. The session
remains faulted with `recoveryRequired: true` until `session.recover()` performs
one native ESP-NOW rebuild. When native admission returns
`ESP_ERR_ESPNOW_NO_MEM`, a tracked send completes with `ESPNOW_SEND_FAILED` and
a queued packet is recorded as failed in `txQueue`. Single-attempt delivery
leaves backoff, acknowledgement, and retransmission policy to the application.
