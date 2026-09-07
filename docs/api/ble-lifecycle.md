# BLE Future timeout and native cleanup

An operation still queued when its deadline expires performs no native I/O.
A started operation reports `BLE_TIMEOUT`; timeout can have the following
side effects. `receive(timeoutMs)` is a separate queue wait and returns `null`.

| Started operation | Timeout action and completion boundary | Affected resource |
| --- | --- | --- |
| Scan / scanner close | Request `ble_gap_disc_cancel`; successful cancel closes callback admission and cleanup waits for entered callbacks; failed cleanup retains storage for adapter cleanup | This scanner |
| Advertise / advertiser close | Request `ble_gap_adv_stop`; close advertiser callback admission and drain entered callbacks before releasing the queue; failed stop retains storage | This advertiser |
| Connect | Request `ble_gap_conn_cancel`; keep the reserved slot until the native result; a late successful connection is terminated | This attempted connection |
| Discover / read / response write / subscribe / subscription close / exchange MTU | Request `ble_gap_terminate`; native GATT callback or successful Host stop/deinit ends native ownership | Matching connection generation |
| Pair / connection close | Request termination; keep the lane until the relevant security/disconnect terminal or Host teardown | Matching connection generation |
| Read RSSI / write without response | Native call is synchronous; if an already-started timeout path is reached, it has the same matching-connection termination protection | Matching connection generation |
| Server indication | Terminate only originally submitted connections still awaiting confirmation; keep the no-cookie indication lane until native terminals or Host teardown | Pending recipients of this operation |
| Adapter close | Cleanup continues; retry only the unfinished Host stop/deinit suffix | This adapter |

`status=0` from NimBLE indication TX is submission, not peer confirmation.
The Future waits for `BLE_HS_EDONE` or a terminal error for each submitted
connection. Duplicate terminals and unrelated connection events cannot decrement
another recipient's pending count. Broadcast indication timeout does not terminate
connections that were not recipients or have already completed.

After public timeout/destruction, JS roots may be removed while native operation
storage remains retained. New GATT work on that connection returns `BLE_BUSY`
until the old native operation ends; other connections retain their own lanes.
GATT callbacks carry a boot-scoped identity rather than reading the slot's current
Future. Identity exhaustion fails without reuse. A late callback never wakes a
replacement runtime. Timeout does not lower MITM/Secure Connections requirements
and does not automatically reconnect. Numeric comparison needs an explicit user
confirmation; absent confirmation or an expired request must be rejected.

Each scan has a boot-scoped callback identity. A callback copied by the Host before
cancel cannot deliver reports to, or stop, a reopened scanner. Scanner close and
failed result conversion drain entered callbacks before releasing the scan pool.
Identity exhaustion returns `BLE_BUSY` without starting discovery; a device
restart is required to reset identity allocation. Runtime restart does not reset it.

Advertising uses a separate boot-scoped identity with the same exhaustion rule.
Late completion cannot stop a reopened advertiser. An incoming connection copied
before advertiser close is rejected if it has not already been registered to a
live connection owner. Existing connections keep receiving their inherited GAP
events after advertiser close. Failed rejection is recorded in
`adapter.status().rejectedConnectionError`; native ownership remains with the
adapter until disconnect or explicit adapter close, without automatic adapter
shutdown. A queued advertising start revalidates adapter and advertiser generation
before touching native configuration.

A connected native slot transfers to JavaScript only after its handle/result is
constructed successfully. An incoming connection belongs to its advertiser queue
until `receive()` finishes conversion. Queue overflow, discard, conversion failure,
or advertiser close terminates only that unclaimed connection. Close also claims
an event already dequeued by an unfinished receive Future; a later finish rejects
it as stale. Successfully transferred connections survive advertiser close.

Failed termination retains the slot and records `rejectedConnectionError`, including
outgoing connections whose Future result could not be constructed. A successful
termination request still waits for disconnect; `BLE_HS_ENOTCONN` confirms absence.
Explicit adapter close or runtime teardown retries unfinished termination before
waiting for pending connect callbacks. Failure retains native storage and a failed
lifecycle. It does not imply runtime restart can recover a still-failing driver.

Connection submission and termination validate identity while serialized with GAP
callbacks. Closing an already disconnected connection does not modify a reused
native handle. Pairing responses revalidate the connection, request and deadline
immediately before native submission. A failed adapter continues consuming native
cleanup terminals; it cannot accept new incoming connection owners. Disconnect and
pairing Futures settle before their observation events are published, including
when the observation queue is full.
