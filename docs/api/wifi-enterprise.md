# Wi-Fi enterprise

`wifi.enterprise` is Candidate, available with Wi-Fi and
`CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT`. Check module presence. It manages one
in-memory enterprise profile and one native control at a time. It does not
persist secrets or automatically start Wi-Fi, select an SSID, or associate.

- `capabilities()` returns `apiVersion:"wifi-enterprise/1"`, `stability:"candidate"`,
  target, supported `methods`, and build flags `domain`, `suiteB192` and
  `defaultCertificateBundle`. Limits are `maximumProfiles:2`,
  `maximumProfileBytes:65536`, `maximumRetainedBytes:131072`.
- `configure(options)` captures and validates a complete replacement. It returns
  `{configured:true,revision}`; revision is an exact decimal uint64 string. Both
  credential capture and this result allocation finish before the commit. A
  reentrant getter cannot overwrite a newer configuration. Configuration is
  rejected during control/closing or while any SDK binding remains.
- `enable(options?)` installs the configured credentials and enables EAP on an
  already started, unassociated Station with idle framework helpers. Only the
  matching application/Station/AP helper owners are admitted. A healthy existing
  binding makes repeated enable idempotent. The result does not prove AP
  association, authentication, IP readiness or RF delivery.
- `disable(options?)` disconnects this Station if an SDK binding exists, waits
  for the native reuse barrier, then retires SDK credentials. It preserves the
  configured profile. With no binding it does not disconnect a PSK connection.
- `clear(options?)` performs the same retirement and releases the configured
  profile only after successful cleanup. Failed cleanup preserves ownership.
- `status()` returns configuration/control metadata and non-secret diagnostics.

Enable, disable and clear are native-driver Future operations. Direct calls
cooperate with the runtime while waiting; `Future.call()` can schedule them.
Control options accept only integer `timeoutMs`, 1..60000, default 5000.
Cancellation observed before the queued worker begins prevents its SDK work. Once
the worker passes that check, cancellation cannot prevent later SDK entry or undo
its side effects: enabling may
finish after timeout, disabling may leave Station disconnected, and clear may
release the configuration. The public Future may end before native retirement;
control/profile storage remains alive and `busy` stays true until polling has
finished native cleanup. Do not automatically retry an uncertain operation.
After it settles, inspect status and explicitly disable/clear if needed.
A result-conversion OOM after SDK completion likewise does not undo the operation.

## Configuration

Only the fields below are accepted. `methods` is a nonempty array of distinct
`"tls"`, `"ttls"`, `"peap"`, `"fast"`; FAST is available only with the SDK internal
TLS implementation. Domain matching requires the mbedTLS client build.

| Field | Maximum bytes / contract |
| --- | --- |
| anonymousIdentity, username | 128 each |
| password, newPassword | 1024 each |
| caCertificate | 32768 |
| clientCertificate | 16384 |
| privateKey | 4096 |
| privateKeyPassword | 1024; embedded NUL rejected |
| pac | 16384; nonempty data must be at least 512 bytes |
| domain | 255; embedded NUL rejected |
| ttlsPhase2 | `"eap"`, `"mschapv2"` (default), `"mschap"`, `"pap"`, `"chap"` |
| checkCertificateTime | Boolean, default true |
| suiteB192, defaultCertificateBundle, okc | Booleans, default false; build gates apply |
| fast | Optional object described below; only with FAST |

Byte fields accept a UTF-8 string or ByteSource (ByteView or ArrayLike of integer
bytes 0..255). Empty or omitted fields are absent; null is rejected. The complete
profile allocation, including metadata and terminator padding, must fit 65536
bytes. Capture takes native copies; later caller mutation cannot change them.
Secrets are never returned by status or included in error details. Framework
profile memory is wiped on final release; SDK copies follow native retirement.
This does not wipe caller-owned JS strings or ByteSources.

TLS requires a certificate/key pair; either both are present or neither. PEAP
and TTLS require username. TLS/TTLS/PEAP require an explicit CA certificate or the
build's default certificate bundle. PEM terminators and DER lengths are handled
separately. `checkCertificateTime:false` explicitly disables certificate date
validation; it does not remove the CA requirement.

FAST accepts `provisioning` (`"disabled"` default, `"unauthenticated"`,
`"authenticated"`), `maximumPacEntries` (integer 0..99) and `binaryPac` (boolean).
Absent PAC requires explicit non-disabled provisioning. Unauthenticated
provisioning is opt-in. Capability presence is not server compatibility proof.

## Status and lifecycle

Status contains `configured`, decimal `revision`, `busy`, `closing`,
`revisionExhausted`, `identityExhausted`, `binding`, `enabled`, `cleanupPending`,
`profiles`, `reservedBytes`, `nativeObserved`, `nativeResources`, `error`,
`cleanupError`, `controlError`, and `stage`. No credentials are exposed.
`checkCertificateTime` reads the SDK policy on the same Wi-Fi task as the owned
native resource snapshot. It is `null` without a native binding, during a busy
control or when the read is unavailable. It does not echo a pending profile,
change the policy, or prove that a certificate or the device clock is valid.
`profiles/reservedBytes` account for framework allocations, including retained
profiles, metadata and padding; SDK copies and allocator overhead are excluded.

During control, `binding/enabled/cleanupPending` are null and native diagnostics
are unobserved: status avoids waiting on the worker's Radio mutex. With a
retained failed binding, enabled can also be null; `cleanupPending:true` requires
explicit retirement. Error/stage fields are current observed binding diagnostics,
not a durable history. Once binding is gone, native resource/error fields are
null. Revision and operation identities never wrap; exhaustion rejects reuse.

`WIFI_ENTERPRISE_FAILED` / `WIFI_ENTERPRISE_TIMEOUT` carry
`{stage,espCode,nativePending}`. Argument/getter and allocation failures can use
standard JS exceptions. Native cleanup failure retains the SDK binding and
profile. Some SDK ownership faults require device restart; runtime restart is
not promised to recover them. Runtime exit rejects new controls, drains the
active Future, disconnects and retires SDK ownership before releasing secrets.

A binding pins helper leases and blocks new Radio owners. Normal scan/connect
can use a healthy installed binding. `wifi.stop()` accepts an idle, disconnected
Station with an Enterprise binding: it first reserves the exact lifecycle, then
retires SDK credentials before releasing helper leases and stopping the driver.
It retains the configured profile. It continues to reject a connected Station,
in-flight controls, wake locks and foreign owners; disconnect explicitly first.
Cleanup failure retains the same lifecycle and config control for a later stop
or runtime-exit retry. Completed SDK/AP-release steps are not repeated when a
later driver or netif cleanup fails. Status reports busy until the whole stop
transaction completes. The stop timeout does not preempt an entered SDK call.

`wifi.driver.restart()` also accepts a healthy, started, unassociated Enterprise
Station with exactly its managed Application/Station/AP owners. An active SoftAP
requires `allowApRestart:true`. Admission checks the exact installed profile,
actual native enabled state and identity capacity before changing the driver.
The config control retains that same profile throughout SDK credential removal,
STOP, helper retirement, rebuild and restoration. Credentials and the original
certificate/OKC policy are reinstalled before publishing replacement leases or
discarding the restart snapshot. Station is left unassociated; AP clients are
not restored. No security policy is relaxed for restart.

A failed reinstall keeps any native credential borrower owned by the same
lifecycle. An explicit restart retry clears that borrower before STOP/deinit;
completed SDK clear and old AP-release steps are not repeated. Rebuild retries
require the original complete checkpoint. An incomplete capture is retained for
`wifi.stop()` or runtime cleanup and cannot be recaptured as a new predecessor.
The configured profile stays busy until restart or cleanup finishes. Runtime
closing prevents reinstallation and uses the same cleanup suffix. Native calls
already entered are not interrupted by the caller's timeout.

After an ordinary successful stop, configuration remains available but EAP is
disabled. Later start or stopped-driver restart does not implicitly enable it;
explicitly call enable before connecting. Broader Monitor/Raw TX/CSI/ESP-NOW
coexistence, authentication, timeout/GC/concurrency and real-device verification
belong to the Wi-Fi test stage; this API remains Candidate.
