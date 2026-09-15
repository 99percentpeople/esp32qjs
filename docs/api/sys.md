# `sys` module

`sys` is the global namespace for platform identity, live diagnostics, system
time, immutable Build Context values, and runtime lifecycle control. Peripheral
operations remain in their own modules.

## Getter rules

`sys.info` contains facts that are stable for the current firmware and runtime
configuration. `sys.status` contains live measurements. Both are read-only lazy
namespace trees:

- Reading a scalar evaluates only that getter.
- Reading a structured leaf returns a fresh detached plain object.
- Re-reading a live leaf takes a new measurement.
- Mutating a returned object never changes native state.
- `Object.keys(...)` lists getters without sampling them.
- `JSON.stringify(sys.info)` or `JSON.stringify(sys.status)` samples every
  enumerable branch sequentially. The result is not an atomic snapshot; select
  only the leaves needed by diagnostics.

## Stable information

`sys.info` has the following exact tree. Object notation below describes getter
results; it does not mean the complete tree is allocated at once.

```text
sys.info.version = {
  framework: string,
  hostApi: 1,
  mquickjs: string,
  espIdf: string
}

sys.info.hardware = {
  hardwareId: string | null,
  target: string,
  chip: {
    model: string,
    revision: { raw: number, major: number, minor: number },
    cores: number,
    capabilities: {
      embeddedFlash: boolean,
      wifi: boolean,
      ble: boolean,
      bluetoothClassic: boolean,
      ieee802154: boolean,
      embeddedPsram: boolean
    }
  },
  cpu: { configuredFrequencyHz: number },
  flash: { sizeBytes: number },
  psram: { enabled: boolean, sizeBytes: number, mode: "none" | "quad" | "octal" }
}

sys.info.features = {
  fs: boolean, nvs: boolean, gpio: boolean, ledc: boolean,
  adc: boolean, dac: boolean, i2c: boolean, spi: boolean,
  uart: boolean, rmt: boolean, i2s: boolean, camera: boolean,
  net: boolean, usbSerial: boolean, socket: boolean, websocket: boolean,
  bitmap: boolean, bitmapJpeg: boolean, wifi: boolean, wifiCsi: boolean,
  espNow: boolean, ble: boolean, tls: boolean, http: boolean,
  httpServer: boolean, rpc: boolean, runtimeLogs: boolean
}

sys.info.runtime = {
  heap: { sizeBytes: number, region: "internal" | "psram" },
  task: {
    name: string,
    stackSizeBytes: number,
    priority: number,
    watchdogEnabled: boolean
  } | null,
  evalTimeoutMs: number,
  startup: { script: string, autorun: boolean, repl: boolean } | null,
  filesystem: {
    root: string,
    mount: boolean,
    required: boolean,
    readOnly: boolean,
    formatOnMountFail: boolean,
    secondary: { partition: string, root: string, required: boolean } | null
  } | null,
  control: {
    restartRuntime: boolean,
    reboot: boolean,
    restartTimeoutMs: number | null,
    restartFailureAction: "reboot" | "stop" | null
  }
}
```

`hardwareId` is `hw-` plus the lowercase factory eFuse Base MAC and is stable
across runtime restart, reboot, erase, and firmware replacement. Silicon
capabilities describe the chip; `sys.info.features` describes compiled
JavaScript capabilities. Disabled optional modules are absent from the global
object and have a `false` feature getter.

PSRAM always uses the same hardware shape. When unavailable it reports
`{ enabled: false, sizeBytes: 0, mode: "none" }`. Managed-runtime-only fields
are `null` when a low-level native embedder did not provide them; values are
never guessed.

## Live status

`sys.status` has the following exact tree:

```text
sys.status.boot = {
  bootId: string,
  uptimeMs: number,
  reset: { code: number, name: string },
  wakeup: { mask: number, names: string[] },
  softwareReason: string | null
}

sys.status.cpu = { frequencyHz: number | null }

HeapStatus = {
  totalBytes: number,
  freeBytes: number,
  allocatedBytes: number,
  minimumFreeBytes: number,
  largestFreeBlockBytes: number,
  allocatedBlocks: number,
  freeBlocks: number,
  totalBlocks: number
}

sys.status.memory = {
  default: HeapStatus,
  internal: HeapStatus,
  dma: HeapStatus,
  psram: HeapStatus | null,
  manager: {
    pressure: "normal" | "guarded" | "critical",
    internalReserveBytes: number,
    dmaLargestReserveBytes: number,
    managedInternalBytes: number,
    managedPsramBytes: number,
    pinnedBytes: number,
    driverPinnedBytes: number,
    stagingPinnedBytes: number,
    dmaStagingPools: number,
    pendingDmaReservationBytes: number,
    movableIdleBytes: number,
    migrationCount: number,
    migrationBytes: number,
    evictionCount: number,
    allocationFailures: number,
    wireless: {
      internal: WirelessMemoryRegion,
      psram: WirelessMemoryRegion,
      rejectedReservations: number
    },
    allocations: Array<{
      owner: string,
      class: "pinned-internal" | "dma-internal" | "dma-external" |
             "external" | "hot-movable" | "cold-movable" | "cache-evictable",
      region: "internal" | "psram",
      bytes: number,
      blocks: number
    }>
  }
}

sys.status.rtos = {
  name: "FreeRTOS",
  schedulerState: "not-started" | "running" | "suspended",
  tickRateHz: number,
  taskCount: number,
  runtimeTask: {
    name: string,
    priority: number,
    currentCore: number,
    stackSizeBytes: number | null,
    stackHighWaterMarkBytes: number,
    watchdogEnabled: boolean,
    watchdogRegistered: boolean
  },
  taskSnapshotSupported: boolean,
  taskSnapshotLimit: number
}

sys.status.runtime = {
  state: "created" | "starting" | "running" | "quiescing" |
         "restarting" | "stopping" | "stopped" | "failed",
  generation: number,
  uptimeMs: number,
  restartCount: number,
  lastRestartReason: string | null,
  pendingControl: {
    action: "restart-runtime" | "reboot",
    reason: string,
    requestedAtMs: number,
    dueAtMs: number
  } | null,
  filesystem: {
    root: string,
    mounted: boolean,
    readOnly: boolean,
    secondaryMounted: boolean
  },
  resources: {
    timers: { active: number, capacity: number },
    futures: {
      queued: number,
      pending: number,
      capacity: number,
      userCapacity: number,
      internalReserve: number
    },
    eventQueues: {
      open: number, dropped: number, queued: number,
      capacity: number, highWater: number
    },
    asyncPollers: { registered: number, capacity: number },
    orphans: { pending: number, capacity: number }
  },
  watchdog: {
    systemEnabled: boolean,
    systemRegistered: boolean,
    jsEnabled: boolean,
    jsRegistered: boolean,
    timeoutMs: number,
    lastOuterHeartbeatAgeMs: number
  },
  startup: {
    phase: "armed" | "stabilizing" | "healthy" | "safe-mode",
    safeMode: 0 | 1 | 2,
    failureCount: number,
    failureLimit: number,
    healthyAfterMs: number,
    lastFailureReason: string | null
  }
}
```

`bootId` identifies one physical firmware boot and remains constant across
`restartRuntime()`. A runtime restart increments `generation`; a full reboot
creates a new `bootId` and starts at generation `1`.

`sys.status.memory` itself is a lazy tree. To compare two points in time,
read each region getter at the measurement point, for example:

```js
var before = { internal: sys.status.memory.internal, psram: sys.status.memory.psram,
  manager: sys.status.memory.manager };
// Run the workload and release its resources.
gc();
var after = { internal: sys.status.memory.internal, psram: sys.status.memory.psram,
  manager: sys.status.memory.manager };
print(after.internal.freeBytes - before.internal.freeBytes);
```

Saving the tree itself keeps live getters; it does not preserve earlier values.
Regions are sampled consecutively, not atomically.

Heap views are overlapping ESP-IDF capability views and should be interpreted
independently. `default` can be dominated by PSRAM. Use `internal`, `dma`, and
`psram`—especially `largestFreeBlockBytes` and `minimumFreeBytes`—to diagnose
allocation failures or fragmentation.

`memory.manager` describes allocations known to the framework memory
manager. `pinnedBytes` includes managed pinned blocks, registered driver DMA
payloads, and reusable staging pools. Opaque ESP-IDF metadata and unregistered
third-party allocations remain in the physical heap views. `allocations` is
sorted by owner, class, and actual region.

`memory.manager.wireless` is the boot-scoped admission ledger shared by registered
Wi-Fi and BLE allocations. Each `WirelessMemoryRegion` contains `limitBytes`,
`controlReserveBytes`, `reservedBytes`, `highWaterBytes`, and `roles` with byte
counts for `control`, `pool`, `retiredPool`, `queue`, `tx`, `stack`, and `copy`.
The same snapshot is available in `wifi.diagnostics.snapshot().memory.wireless`.
The `CONFIG_ESP32_MQUICKJS_WIRELESS_INTERNAL_BUDGET_BYTES`,
`CONFIG_ESP32_MQUICKJS_WIRELESS_PSRAM_BUDGET_BYTES`, and
`CONFIG_ESP32_MQUICKJS_WIRELESS_CONTROL_RESERVE_BYTES` Build Context settings select
the limits. Wireless-enabled contexts must specify all three as nonnegative
decimal integers, with `0 < control reserve < internal limit`. The context
loader and CMake reject missing/duplicate/out-of-range quotas, PSRAM mode/size
contradictions, quota above physical PSRAM, and resolved SDK quota/target/PSRAM
values that disagree with immutable inputs. Recreate stale Build Context or
configuration outputs; do not silently accept Kconfig clamping.

The configured Future worker stack payload must fit the internal data quota;
a target C assertion also includes actual static TCB sizes. This is a necessary
lower bound, not a guarantee that all configured pools, metadata and queues fit
concurrently. Per-module size/overflow checks and shared runtime admission still
apply. Allocator headers, SDK/JS/TLS memory and whole-device peaks require their
own accounting and warmed-device measurements. Check resolved inputs directly
with `python scripts/validate_wireless_budget.py --build-context PATH --sdkconfig BUILD/config/sdkconfig.json`.
Firmware CI
contexts explicitly select 128 KiB internal, 16 KiB control, and 4 MiB PSRAM when
configured; these are software-check inputs, not measured board capacity.

Admission reserves requested payload bytes plus the memory manager's tracking
node before either allocator call. Metadata keeps its PSRAM preference; its
bytes consume the same role as the payload in its own region. Failed allocations
roll back after freeing partial storage. Final release returns quota only after
both allocator frees return. Consequently `reservedBytes` may exceed live
published payload counts; `highWaterBytes` includes subsequently failed attempts.
`rejectedReservations` counts denied region-pair attempts, including fallback
attempts, and saturates at UINT32_MAX. Runtime restart preserves outstanding
fixed allocations and these counters. Explicit
`wifi.diagnostics.resetFrameworkCounters()` clears denial/allocation-failure,
migration and eviction histories, and restarts wireless peaks at current
reservations. It does not change owner, DMA, pending alloc/free or role accounting.
Registered runtime EventQueue peaks/drops are also reset; the runtime aggregate
peak is a sum of individual queue peaks, not a simultaneous global high-water.
Retiring a pool reclassifies
its bytes without releasing quota; retained View/Source storage remains charged.

Current coverage includes CSI pool/control storage, Monitor pool and Session
control, ESP-NOW RX pools/copies and TX queue/staging/tracked payload/worker
stack, and existing BLE scan/subscription pools. Wireless EventQueues also charge
their native control, drain/overflow scratch, static RTOS mutex and complete
static RTOS queue/control storage to `queue`. Their pending receive capture state
and one event buffer use `control`, so data reservations cannot consume that
headroom. Deleting the RTOS objects precedes freeing their owned allocation;
closing a queue retains its charge until all native references are released.

Registered Wi-Fi/ESP-NOW/BLE drivers charge per-call argument-root arrays, public
Future handles, and their native capture states to `wireless.future` / `control`.
Each allocation follows its own lifetime: releasing argument roots does not
release the public handle or native operation state. Raw TX one-shot copies,
broker/quarantine data and queued/periodic payloads use `tx`; Session/periodic
control and handles use `control`, slot arrays use `queue`, and temporary batch
arrays use `copy`, under `wifi.raw-tx`. Existing PSRAM payload policies remain.

Module-owned controls, public handles, configuration/credential snapshots,
scan/discovery results, FTM/RRM storage and protocol timer/worker contexts now
participate in the same admission ledger. Existing BLE server definitions,
attribute/event pools and read/write/notification copies are included. Secret
storage keeps its existing secure-zero-before-release behavior.

CSI/Monitor Frame, Batch, retained references, ByteView and ByteSpanSource wrappers
use `control`; wire-directory buffers and copied bytes use `copy`. A Source's
read-lease wrapper inherits its wireless owner. Closing a ByteView while a read
lease exists keeps both wrapper and retained payload charged until the last
read releases them. Source close remains busy until its read leases finish.
BLE/ESP-NOW array-like input conversion copies also use `copy`; borrowing an
existing ByteView does not charge its data a second time. Non-wireless users of
the generic byte factories retain their existing allocation policy.

Wi-Fi helper mutex/event-group and native completion queues use `control`;
watch ingress uses `queue`. Static RTOS control and queue payload bytes are
reserved together, and SDK deletion precedes freeing their storage. Producers,
waiters and queued callbacks must already be retired by the owning lifecycle.

Builds enabling Wi-Fi, BLE or ESP-NOW also admit the shared Future service as
`wireless.runtime`: runtime/driver registry, slot table, dispatch/ready queues,
EventQueue runtime registry, common per-call handles/roots/combinator inputs,
and the boot worker pool. Common controls include calls for non-wireless drivers
and shared EventQueue receive aliases; registered wireless drivers retain their
explicit per-call owner. No extra JS receiver/property read is required.
Worker queue uses `control`, and the fixed stack/TCB allocation uses `stack`.
Created workers have boot lifetime, including across runtime restart. A partial
creation failure keeps created tasks, queue and admitted storage; the next
initialization retries only the uncreated suffix. Failure before any worker
exists frees the allocation. These retained boot bytes belong in warmed baselines.
Builds disabling all three wireless features retain unbudgeted shared allocation.

Opaque SDK/NimBLE timer/task/storage, allocator headers/alignment, JavaScript,
TLS, other modules' own queues and unrelated heaps are outside this admission
ledger. Static firmware sections are part of linked RAM usage, not heap admission.
They must be reported separately when assessing total device memory, as required
by the wireless budget contract. The outer
`managedInternalBytes`/`managedPsramBytes` count managed payloads, excluding tracking
nodes. Logical control headroom does not guarantee a physical heap allocation.

## Configuration and lightweight helpers

- `sys.config()` returns a fresh object containing every immutable Build
  Context value.
- `sys.config(key)` returns one string, number, or boolean, or `undefined` when
  absent. Registered driver defaults use `ESP32QJS_*`; application-owned keys
  should use a separate prefix such as `APP_*`.
- `sys.millis()` and `sys.micros()` return monotonic time since boot.
- `sys.freeHeap()` is the low-allocation equivalent of
  `sys.status.memory.default.freeBytes`; it is not proof that an internal or DMA
  allocation can succeed.
- `sys.randomHex(byteLength)` returns 1–64 cryptographically strong random
  bytes as lowercase hexadecimal text.
- `sys.withTimeout(timeoutMs, callback)` runs a callback with a 1–60000 ms
  wall-clock deadline. A nested call may shorten but never extend its caller's
  deadline, and the deadline remains active across cooperative native waits.

## System time

- `sys.time.status()` returns
  `{ synchronized: boolean, synchronizing: boolean, unixTimeMs: number | null }`.
  `unixTimeMs` remains `null` until the clock is valid for certificate-date
  checks.
- `sys.time.sync({ servers, timeoutMs? })` uses the active network interface
  and returns `{ synchronized: true, unixTimeMs }`. `servers` contains one to
  four caller-selected SNTP names. `timeoutMs` is an integer from 1 through 60000
  and defaults to 15000. A concurrent synchronization is rejected with
  `error.code === "TIME_SYNC_BUSY"`; calls are not merged or queued.

## Task diagnostics

`sys.tasks(options?)` returns:

```text
{
  total: number,
  truncated: boolean,
  tasks: Array<{
    id: number,
    name: string,
    state: "running" | "ready" | "blocked" | "suspended" | "deleted" | "invalid",
    priority: number,
    basePriority: number,
    core: number | null,
    stackHighWaterMarkBytes: number
  }>
}
```

`options.limit` is an integer from 1 through
`sys.status.rtos.taskSnapshotLimit` and defaults to that limit. The operation
returns detached task records sorted by task ID. It throws an `InternalError`
when snapshots are disabled or the system task count exceeds the configured
native snapshot capacity; use the cheap `taskCount` getter when a complete
snapshot is unnecessary.

## Lifecycle control and safe mode

`sys.restartRuntime(options?)` replaces only the current JavaScript generation.
`sys.reboot(options?)` schedules a full software reboot. Both accept exactly:

```text
{ reason?: string, delayMs?: number }
```

`reason` defaults to `"javascript"`, must contain 1–64 UTF-8 bytes, and may not
contain ASCII control characters. `delayMs` is an integer from 0 through 60000
and defaults to zero. Unknown keys are rejected.

Success returns an acceptance receipt:

```text
{
  action: "restart-runtime" | "reboot",
  reason: string,
  generation: number,
  requestedAtMs: number,
  dueAtMs: number
}
```

The runtime acts after the delay expires and the outer JavaScript turn unwinds.
The receipt confirms acceptance; observe a new runtime generation or boot ID to
confirm completion. The lifecycle queue holds one pending request.

`sys.safeMode` is a read-only number matching
`sys.status.runtime.startup.safeMode`:

| Value | Behavior |
| --- | --- |
| `0` | Normal startup. |
| `1` | Soft recovery: the startup script still runs and the application decides what to skip. |
| `2` | Hard recovery: the runtime skips the entire configured startup script. |

Every `failureLimit` abnormal resets (two by default) raise the level by one,
up to `2`. Watchdog, panic, CPU lockup, and firmware failure reboots count,
including faults after the startup observation window. An uncaught startup
exception followed by a software reboot is counted once, on the next boot.
The guard persists the abnormal-reset count and last reason in its private NVS.
Healthy execution does not clear that count or lower the recovery level; the
healthy window controls startup phase reporting only.

An operator `sys.reboot()`, external RESET, or power cycle clears the level,
count, and last reason on the next boot. Automatic fault reboots continue the
same chain. No property assignment is needed. `sys.restartRuntime()` replaces
only the JavaScript generation and retains the current level; failure of that
restart followed by a firmware reboot counts as an abnormal reset.

The framework owns hard recovery without depending on an Agent or application.
A product whose Agent is part of the startup script has no Agent RPC in hard
recovery and must use physical reset or ROM recovery to restart or reflash.

```js
sys.reboot({ reason: "safe-mode-repaired", delayMs: 250 });
```

Trusted remote execution may invoke the same function in soft recovery.
The delay allows its acceptance receipt to reach the caller before the device
disconnects. After reconnection, verify a new `sys.status.boot.bootId` and
`sys.safeMode === 0`; do not treat the scheduling receipt as completed recovery.

## Errors

Shape and type errors use `TypeError`; numeric and string bounds use
`RangeError`. Task-snapshot capability errors, lifecycle-control errors,
duplicate control requests, and failed getter allocation use catchable
`InternalError`. Missing PSRAM and unavailable CPU-frequency observations are
represented by the documented `null` values.

## Example

```js
(function () {
    var chip = sys.info.hardware.chip;
    var heap = sys.status.memory.internal;
    return {
        hardwareId: sys.info.hardware.hardwareId,
        model: chip.model,
        revision: chip.revision,
        freeBytes: heap.freeBytes,
        largestFreeBlockBytes: heap.largestFreeBlockBytes,
        wifi: sys.info.features.wifi,
        uptimeMs: sys.status.boot.uptimeMs
    };
})()
```
