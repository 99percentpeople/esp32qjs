# TX completion and native codes

Raw TX, Action TX, ESP-NOW and NAN follow-up sends share the sole v1 completion
contract. Each result retains its module-specific identity, frame/payload metadata
and lifecycle fields. A completion records a correlated native TX observation;
it is not proof that a peer application received or processed the payload.

```js
// Example result fragment, not an additional send API.
var observation = {
  completion: {
    status: "failed",
    native: { domain: "wifi_tx_status_t", code: 1, name: "WIFI_SEND_FAIL" }
  }
};
console.log(observation.completion.status, observation.completion.native.name);
```

`completion.status` is `success`, `failed` or `unknown`. `completion.native` is
`{domain, code, name}` when the native interface exposes a numeric status. Preserve
unknown numeric values with `name: null`. An unknown primary completion enum
also yields `status: "unknown"`. Raw TX can expose a finer descriptor code while
its public SDK completion still establishes success/failure; an unknown descriptor
name does not erase that outcome. A boolean-only observation has `native: null`;
do not fabricate an enum for it.

| Source | Native domain | Known TX names |
| --- | --- | --- |
| Raw TX one-shot / Session, descriptor observed | `esp_wifi_tx_descriptor_status` | Framework observation labels: `success`, `frame-exchange`, `discarded` |
| Raw TX, descriptor unavailable | `wifi_tx_status_t` | `WIFI_SEND_SUCCESS`, `WIFI_SEND_FAIL` |
| ESP-NOW send / broadcast / queue | `esp_now_send_status_t` | `ESP_NOW_SEND_SUCCESS`, `ESP_NOW_SEND_FAIL` |
| Action TX / NAN USD | `wifi_action_tx_status_type_t` | `WIFI_ACTION_TX_DONE`, `WIFI_ACTION_TX_FAILED` |
| Synchronous NAN follow-up | No numeric observation at this bridge | `native: null` |

Raw TX selects one native observation in the existing `{domain, code, name}`
envelope. Always interpret code together with domain: descriptor code 1 means
success, while `wifi_tx_status_t` code 1 means failure. Descriptor names are
reviewed internal observations, not public Espressif enum symbols. See
[Raw TX completion details](wifi-raw-tx.md) for the mapping and evidence limits.

No completion observation is represented by `completion: null`, not by unknown
or zero. Raw TX and ESP-NOW normal send results always have a completion. NAN
send errors can contain null; Action can finish its residency without a TX
observation and then returns null. A malformed or uncorrelated callback is an
operational fault, not an ordinary unknown enum.

Known failed completion returns a result normally. SDK submission errors,
timeouts, cancellation and correlation/lifecycle failures keep their operation's
error semantics. Raw TX send/Session/periodic errors, Action send errors,
ESP-NOW errors and NAN follow-up send errors use `details.native` for their
`esp_err_t` code and name. Error `code` remains the framework discriminator;
`stage`, identity and retained ownership explain where available what failed.
Recovery and other non-send APIs retain their documented error details.
A numeric namespace identifies how to decode a value; an adapter-generated
ESP_ERR_INVALID_STATE does not prove that an SDK call returned that value.

A generic TX failure does not distinguish missing ACK, address filtering, retry exhaustion
or another internal cause. Do not infer `no-ack`, peer absence, or non-transmission
from a generic failure. Action's `noAck` is request policy, not observed receipt.
Use a separate protocol with sequence correlation and application ACKs when the
application needs delivery confirmation; native send performs no automatic retry.

Queue admission proves only that a packet entered the queue. `completed` counts
MAC terminal observations including success, failure and unknown; `failed` counts
only failed MAC completion. Submission rejection, drop, abort and waiting timeout
have separate counters. Raw TX Session snapshots satisfy:

```text
completed = succeeded + failed + unknown
settled = completed + rejected + aborted + dropped
pending = admitted - settled
```

Periodic scheduling opportunities are separate from queue admission. ESP-NOW
exposes admission/eviction counters and separate worker settlement and timeout
observations; consult its API for that scope. In particular, a timed-out packet
is not claimed to be physically aborted. Independently sampled diagnostics are
not an atomic transaction across driver, queue and scheduler.

A completed Future, completed TX, and retired native owner are distinct. Preserve
cleanup/quarantine/generation and NAN bufferRetired diagnostics. An ended wait
or allocation failure constructing a result cannot undo an already submitted
transmission. Inspect retained ownership before retrying an uncertain operation.

Platform AI and external MCP consume these same firmware fields and this document.
Successful tool/RPC execution means the call returned; inspect completion.status
for the TX outcome. Transport success is not an additional wireless receipt.
