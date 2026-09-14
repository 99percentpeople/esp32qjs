# Wi-Fi WAPI

Requires Wi-Fi and `CONFIG_ESP_WIFI_WAPI_PSK`. The pinned SDK supports WAPI-PSK on
C3/S3/C5; `wifi.wapi` is not registered when this option is disabled. This API is
currently **Candidate**; runtime and RF acceptance remain pending.

| Method | Behavior |
| --- | --- |
| `wifi.wapi.capabilities()` | Returns `wifi-wapi/1`, PSK support, native ownership and control boundaries |
| `wifi.wapi.status()` | Returns policy, actual native lifecycle and fault snapshots without secrets; does not initialize the Radio |
| `wifi.wapi.enable(options?)` | Allows the SDK supplicant to initialize native WAPI support |
| `wifi.wapi.disable(options?)` | Prevents the SDK supplicant from creating WAPI support at its next initialization; if already initialized, applies the policy immediately through a full rebuild |

`options` accepts only `timeoutMs`: an integer from 1..2147483647 ms, default 10000.
Unknown fields, non-integers and out-of-range values are rejected before mutation.
Control methods use the normal synchronous lifecycle wait budget; they are not Future methods.

The default policy is enabled, matching SDK builds with WAPI enabled. Before the
first Radio initialization, control methods only select the initialization policy
and do not implicitly start Wi-Fi; `policyApplied` is false at this point. Rebuilding
an existing native instance requires a healthy, fully stopped Radio with no owners.
The rebuild follows the `wifi.driver.restart` checkpoint, helper draining, physical
deinit/init, configuration replay and readback process. **On success, interfaces
start with the saved Station/AP/APSTA configuration**; call wifi.stop and close
other feature owners first. This causes a configured AP to broadcast again.
Requesting the same, already applied policy is idempotent and does not reinitialize it.

SDK WAPI init/deinit is called only through the existing supplicant lifecycle;
enable does not register a second callback table, and disable does not release
native authentication state during an active connection. The entire rebuild uses
the existing exclusive Radio lifecycle. Native and helper failures preserve completed
steps and pending cleanup. A timeout neither rolls back an accepted policy nor
guarantees that no physical operation occurred; inspect status and wifi.status().radio.
If native cleanup is uncertain, the original error is retained and a device restart
is required. A runtime restart does not prove recovery.

Status fields:

- `requestedEnabled`: the accepted policy; `policyRevision` increments on changes without wrapping.
- `enabled`: whether native WAPI is actually initialized; null during mutation or when uncertain.
- `supplicantActive`, `nativeGeneration`: observations of the actual SDK lifecycle; generations do not wrap.
- `policyApplied`: the current native instance has applied the requested policy; `busy` indicates a native lifecycle call in progress.
- `restartRequired`: native cleanup is uncertain; `runtimeCleanupPending`: configuration/helper cleanup is incomplete.
- `error`, `cleanupError`: original native errors, without passwords or keys.

WAPI-PSK connections continue to use `wifi.connect(ssid, {password, minimumAuthMode:"wapi", ...})`
and the existing Station credential owner. This module does not store another
password, accept certificate mode or support WAPI SoftAP. `minimumAuthMode` is the
SDK authentication threshold, **not a WAPI-only selector**; capabilities reports
`exactAuthSelection` as false. Check the actual authentication mode of the connection
result; neither the threshold nor an enabled module proves that a WAPI connection
was established. Disabling WAPI does not delete saved Station credentials.

Public controls, native lifecycle wrappers and types are implemented; see the Wi-Fi
implementation table for targeted build records. Native callback/TX retirement,
GC/OOM, fault injection, the full three-target matrix and WAPI peer authentication
acceptance remain scheduled for consolidated validation. Source inspection, partial
linking and ordinary WPA connections do not replace WAPI RF evidence.
