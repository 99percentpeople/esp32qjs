# Wi-Fi CSI

`wifi.csi` is a bounded raw Channel State Information capture primitive, not a
presence/motion classifier. Call `wifi.csi.capabilities()` before constructing
the required capture object; do not infer a schema, PHY, sample encoding, or
channel set from a Board name. The only API version is `wifi-csi/1`.

### Capabilities

`wifi.csi.capabilities()` returns this detached object:

```text
{
  apiVersion: "wifi-csi/1",
  target: string,
  idfVersion: string,
  configSchema: "wifi-csi-legacy/1" | "wifi-csi-he/1",
  sources: ("associated" | "promiscuous")[],
  phyFormats: ("legacy" | "ht" | "vht" | "he-su" | "he-mu" |
               "he-er-su" | "he-tb" | "unknown")[],
  sampleEncodings: ("signed-int8" | "signed-int12-le" |
                    "signed-int12-packed")[],
  radio: {
    country: string | null,
    policy: "auto" | "manual" | null,
    bands: ({
      band: "2.4GHz" | "5GHz",
      allowedChannels: number[] | null
    })[]
  },
  limits: {
    maxFrameBytes: number,
    maxPoolCapacity: number,
    maxQueueCapacity: number,
    maxBatchFrames: number,
    scaleMinimum: number | null,
    scaleMaximum: number | null,
    shiftMinimum: number | null,
    shiftMaximum: number | null
  },
  supports: {
    fixedChannel: boolean,
    promiscuous: boolean,
    sourceMacFilter: boolean,
    destinationMacFilter: boolean,
    rssiFilter: boolean,
    nativeDecimation: boolean,
    nativeRateLimit: boolean,
    vht: boolean,
    he: boolean,
    heStbcSelection: boolean,
    manualScaling: boolean,
    lltfBitMode: boolean,
    layout: true
  }
}
```

`radio.allowedChannels: null` means ESP-IDF cannot enumerate the band
authoritatively; it never means unrestricted. Check `sources`, `supports`, and
the appropriate limit before requesting an optional control.

### Opening a session

`wifi.csi.open(options)` requires exactly one object with this shape and starts
the only session immediately:

```text
{
  source?: "associated" | "promiscuous",       // default "associated"
  channel?: "current" | number,                 // default "current"
  conflict?: "fail",                            // the only policy
  capture: LegacyCapture | HeCapture,            // required
  filter?: {
    sourceMac?: string | string[],
    destinationMac?: string | string[],
    minimumRssi?: number,
    sampleEvery?: number,
    maximumRateHz?: number,
    validOnly?: boolean
  },
  queue?: {
    capacity?: number,
    overflow?: "drop-newest"
  },
  powerSavePolicy?: "preserve" | "require-none"
}
```

The capture schema must equal `capabilities().configSchema`. A legacy capture
object is:

```text
{
  schema: "wifi-csi-legacy/1",
  lltf?: boolean,
  htLtf?: boolean,
  stbcHtLtf2?: boolean,
  ltfMerge?: boolean,
  adjacentSubcarrierFilter?: boolean,
  scale?: "auto" | { shiftBits: number },
  dumpAck?: boolean
}
```

An HE capture object is:

```text
{
  schema: "wifi-csi-he/1",
  enableLegacy?: boolean,
  forceLegacyLtf?: boolean,
  ht20?: boolean,
  ht40?: boolean,
  vht?: boolean,
  heSu?: boolean,
  heMu?: boolean,
  heDcm?: boolean,
  heBeamformed?: boolean,
  heStbcLtf?: "first" | "second" | "alternate",
  valueScale?: number,
  dumpAck?: boolean,
  lltfBits?: 8 | 12
}
```

Unknown fields and target-incompatible values are rejected. `validOnly` drops
every sample for which the target adapter exposes an invalid first word,
invalid channel estimate, or invalid callback data; its counters remain
separate in `stats()`.

Associated capture preserves the current Station radio channel. Promiscuous
capture requires `supports.promiscuous`. A numeric channel additionally
requires `supports.fixedChannel` and takes an exclusive shared-radio lease.
Regulatory and coexistence conflicts fail without disconnecting Station,
moving SoftAP, or changing ESP-NOW. `powerSavePolicy: "preserve"` reports
power-save-dependent timestamp accuracy when modem sleep is active;
`"require-none"` rejects instead of changing Wi-Fi power save.

Use an explicit promiscuous request when capture must not depend on association:

```js
var caps = wifi.csi.capabilities();
if (!caps.supports.promiscuous || !caps.supports.fixedChannel) {
  throw new Error("This Artifact does not support fixed-channel promiscuous CSI");
}
var capture = caps.configSchema === "wifi-csi-he/1"
  ? { schema: "wifi-csi-he/1", enableLegacy: true, ht20: true, heSu: true }
  : { schema: "wifi-csi-legacy/1", lltf: true, htLtf: true, scale: "auto" };
var session = wifi.csi.open({
  source: "promiscuous",
  channel: 6,
  conflict: "fail",
  capture: capture,
  filter: { minimumRssi: -85, validOnly: true },
  queue: { capacity: 16, overflow: "drop-newest" },
  powerSavePolicy: "preserve"
});
```

### `WiFiCsiSession`, status, and statistics

- `status()` returns `{ generation, state, requested, effective, lastError }`.
  `state` is `"running"`, `"stopped"`, `"stopping"`, `"faulted"`, or
  `"closed"`. `requested` is the complete open object. `effective` contains
  `{ source, channel, secondaryChannel, radioGeneration, configSchema,
  maxFrameBytes, queueCapacity, poolCapacity, powerSave, timestampAccuracy }`.
  `secondaryChannel` is `"none"`, `"above"`, or `"below"`, and
  `timestampAccuracy` is `"normal"` or `"power-save-dependent"`.
- `stats()` returns `{ callbacks, accepted, deliveredFrames, deliveredBatches,
  filteredMac, filteredRssi, filteredDecimation, filteredRateLimit,
  filteredFirstWordInvalid, filteredChannelEstimateInvalid,
  invalidCallbackData, droppedPoolFull, droppedQueueFull,
  droppedFrameTooLarge, droppedClosing, receivedBytes, leasedFrames,
  freePoolSlots, queue }`. `queue` is the normal EventQueue statistics object.
- `receive(timeoutMs?)` returns one `WiFiCsiFrame` or `null`.
- `receiveBatch(options?)` accepts only an object with optional
  `{ maximumFrames, minimumFrames, timeoutMs, maximumLatencyMs }`.
  `maximumFrames` defaults to the Build Context limit, `minimumFrames` to 1,
  omitted `timeoutMs` waits indefinitely for the first frame, and
  `maximumLatencyMs` defaults to 0. Values must satisfy
  `1 <= minimumFrames <= maximumFrames <= caps.limits.maxBatchFrames`; time
  values are non-negative integers no greater than `INT32_MAX`.
  After the first frame the method drains the queue and returns at the minimum
  or maximum. When below the minimum it waits at most `maximumLatencyMs`, then
  returns a partial batch. First-frame timeout/stop/close returns `null`; a
  stop/close after at least one frame returns the partial batch.
- `stop()` returns status after disabling capture. While stopped,
  `configure(options)` replaces the complete open object and `start()` resumes.
  `close()` is idempotent.

### `WiFiCsiFrame` and `WiFiCsiBatch` data

`frame.info` and `batch.info(index)` return:

```text
{
  sequence: number,
  timestampUs: number,
  rxSequence: number,
  generation: number,
  sourceMac: string,
  destinationMac: string,
  rssi: number,
  noiseFloor: number | null,
  channel: number,
  secondaryChannel: "none" | "above" | "below",
  antenna: number | null,
  phy: {
    format: "legacy" | "ht" | "vht" | "he-su" | "he-mu" |
            "he-er-su" | "he-tb" | "unknown",
    bandwidthMHz: 20 | 40 | null,
    mcs: number | null,
    stbc: boolean | null
  },
  validity: {
    firstWordInvalid: boolean,
    channelEstimateValid: boolean | null,
    truncated: false
  },
  layout: {
    schema: string,
    componentOrder: "imaginary-real",
    sampleEncoding: "signed-int8" | "signed-int12-le" |
                    "signed-int12-packed" | "unknown",
    sampleBits: 8 | 12 | null,
    byteLength: number,
    iqPairCount: number,
    trailingPaddingBytes: number,
    segments: ({
      type: "lltf" | "ht-ltf" | "stbc-ht-ltf2" | "vht-ltf" |
            "he-ltf1" | "he-ltf2" | "mixed" | "unknown",
      offsetBytes: number,
      lengthBytes: number,
      iqPairCount: number,
      subcarrierRanges: ({ start: number, end: number })[],
      nullSubcarriers: number[]
    })[]
  }
}
```

`timestampUs` is boot-relative driver time extended across 32-bit wraps and
represented as a JavaScript number; it is not UTC. Firmware preserves raw
imaginary-real IQ order. Ambiguous PHY/configuration/length combinations use an
explicit unknown layout; the framework does not invent subcarrier positions.

`frame.samples()` returns a retained pool-backed `ByteView`,
`frame.copySamples()` returns an independent owned `ByteView`, and
`frame.source()` returns a one-shot retained `ByteSpanSource`. A batch exposes
`frameCount`, `info(index)`, `samples(index)`, and
`source({ format: "esp32qjs-csi/1" })`. Close every frame, batch, retained view,
and source deterministically. Outstanding leases may safely outlive session
close and keep only their old pool generation alive.

The batch source is the sole little-endian `esp32qjs-csi/1` format and carries
uint64 timestamps plus complete Layout/Segment metadata. Read
`doc://framework/wifi-csi-protocol` for exact offsets and enum values. Native
firmware does not calculate FFT, magnitude, phase, recognition, storage, or
upload policy.

Operational failures use `error.operation === "wifi.csi"` and one of:
`WIFI_CSI_NOT_COMPILED`, `WIFI_CSI_NOT_SUPPORTED`, `WIFI_CSI_ALREADY_OPEN`,
`WIFI_CSI_NOT_OPEN`, `WIFI_CSI_NOT_RUNNING`, `WIFI_CSI_CLOSING`,
`WIFI_CSI_CONFIG_SCHEMA_MISMATCH`, `WIFI_CSI_CONFIG_UNSUPPORTED`,
`WIFI_CSI_CONFIG_INVALID`, `WIFI_CSI_RADIO_CONFLICT`,
`WIFI_CSI_REGULATORY_CONFLICT`, `WIFI_CSI_PROMISCUOUS_CONFLICT`,
`WIFI_CSI_POWER_SAVE_CONFLICT`, `WIFI_CSI_RESOURCE_EXHAUSTED`,
`WIFI_CSI_FRAME_TOO_LARGE`, `WIFI_CSI_STALE_FRAME`,
`WIFI_CSI_DRIVER_ERROR`, or `WIFI_CSI_CLEANUP_PENDING`. `error.details` may
contain `{ stage, espCode, espName, requestedChannel, effectiveChannel,
radioGeneration, configSchema }`; `status().lastError` retains the same
structured information.

```js
var caps = wifi.csi.capabilities();
var capture = caps.configSchema === "wifi-csi-he/1"
  ? { schema: "wifi-csi-he/1", enableLegacy: true, ht20: true, heSu: true }
  : { schema: "wifi-csi-legacy/1", lltf: true, htLtf: true, scale: "auto" };
var session = wifi.csi.open({ capture: capture });
try {
  var frame = session.receive(1000);
  if (frame) {
    try { print(frame.info.rssi, frame.info.layout.byteLength); }
    finally { frame.close(); }
  }
} finally {
  session.close();
}
```

Target compilation and host ownership tests do not substitute for RF hardware
qualification. Treat PHY metadata, actual frame sizes, rate/throughput, and
long-duration coexistence as hardware-pending until recorded per target.
