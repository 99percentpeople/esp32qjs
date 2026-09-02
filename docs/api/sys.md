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
    eventQueues: { open: number, dropped: number },
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
    safeModeActive: boolean,
    safeModeRequested: boolean,
    failureCount: number,
    failureLimit: number,
    healthyAfterMs: number,
    lastFailureReason: string | null
  }
}
```

`bootId` identifies one physical firmware boot and does not change across
`restartRuntime()`. A runtime restart increments `generation`; a full reboot
creates a new `bootId` and starts at generation `1`.

Heap views are overlapping ESP-IDF capability views and must not be added
together. `default` can be dominated by PSRAM. Use `internal`, `dma`, and
`psram`—especially `largestFreeBlockBytes` and `minimumFreeBytes`—to diagnose
allocation failures or fragmentation.

`memory.manager` describes only allocations known to the framework memory
manager. `pinnedBytes` includes managed pinned blocks, registered driver DMA
payloads, and reusable staging pools. Opaque ESP-IDF metadata and unregistered
third-party allocations are intentionally excluded. `allocations` is sorted by
owner, class, and actual region and never exposes addresses.

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
sorts by task ID and exposes no task handles or stack addresses. It throws an
`InternalError` when snapshots are disabled or the system task count exceeds
the configured native snapshot capacity; use the cheap `taskCount` getter when
a complete snapshot is unnecessary.

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

The runtime acts only after the delay expires and the outer JavaScript turn
unwinds. The receipt does not prove completion, only that the request was
accepted. Only one request may be pending. Do not invoke either control unless
the user explicitly requested it; a transport can disconnect before an
ordinary `exec` result arrives, leaving an uncertain mutation.

`sys.safeMode` is the persistent operator latch. Assigning `false` clears the
startup failure count and latch for the next boot; it does not load workspace
code in the current generation. Application startup code must not modify it.

```js
sys.safeMode = false;
sys.reboot({ reason: "safe-mode-repaired" });
```

## Errors

Shape and type errors use `TypeError`; numeric and string bounds use
`RangeError`. Unavailable task snapshots, lifecycle controls, duplicate control
requests, or failed getter allocation use catchable `InternalError`. Missing
PSRAM and unavailable CPU-frequency observations return `null` as documented
instead of fabricated values.

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
