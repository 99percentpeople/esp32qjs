# BLE Future timeout and native cleanup

An operation still queued when its deadline expires performs no native I/O.
A started operation reports `BLE_TIMEOUT`; timeout can have the following
side effects. `receive(timeoutMs)` is a separate queue wait and returns `null`.

| Started operation | Timeout action and completion boundary | Affected resource |
| --- | --- | --- |
| Scan / scanner close | Request `ble_gap_disc_cancel`; successful cancel resets Host discovery state; failed cleanup retains storage for adapter cleanup | This scanner |
| Advertise / advertiser close | Request `ble_gap_adv_stop`; failed cleanup retains storage | This advertiser |
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
