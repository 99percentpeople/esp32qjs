declare global {
namespace ESP32QJS {
  /**
   * JSON-like value supported by `Response.json(...)` and the HTTP helpers.
   */
  type JsonPrimitive = string | number | boolean | null;
  type JsonValue = JsonPrimitive | JsonValue[] | { [key: string]: JsonValue };

  type HeaderRecord = Record<string, string>;
  type HeadersInit = Headers | HeaderRecord;
  type RequestBody = string | Stream | ByteView | ByteSpanSource | null | undefined;

  /** Stable base shape for recoverable native operational failures. */
  interface NativeError extends Error {
    code: string;
    operation: string;
    details: object;
  }

  /** Numeric namespace and exact native value. Unknown symbolic names are null. */
  interface NativeCode<Domain extends string = string, Name extends string = string> {
    domain: Domain;
    code: number;
    name: Name | null;
  }
  type EspNativeCode = NativeCode<"esp_err_t">;
  /** A correlated driver observation; never an application receipt. */
  interface TxCompletion<Native extends NativeCode | null = NativeCode | null> {
    status: "success" | "failed" | "unknown";
    native: Native;
  }
  /** Prefer the reviewed descriptor observation; fall back to the public SDK enum. */
  type WiFiRawTxCompletion = TxCompletion<
    NativeCode<"esp_wifi_tx_descriptor_status", "success" | "frame-exchange" | "discarded"> |
    NativeCode<"wifi_tx_status_t", "WIFI_SEND_SUCCESS" | "WIFI_SEND_FAIL">
  >;
  type WiFiActionNativeCode = NativeCode<"wifi_action_tx_status_type_t", "WIFI_ACTION_TX_DONE" | "WIFI_ACTION_TX_FAILED">;
  type EspNowCompletion = TxCompletion<NativeCode<"esp_now_send_status_t", "ESP_NOW_SEND_SUCCESS" | "ESP_NOW_SEND_FAIL">>;

  /** Opaque generation-checked token returned by the timer globals. */
  type TimerHandle = number & { readonly __timerHandleBrand: never };

  type FutureStatus =
    | "queued"
    | "pending"
    | "fulfilled"
    | "rejected"
    | "cancelled";

  interface Future<T> {
    status(): FutureStatus;
    wait(timeoutMs?: number): T;
    cancel(): boolean;
    map<U>(fn: (value: T) => U): Future<U>;
    flatMap<U>(fn: (value: T) => Future<U>): Future<U>;
  }

  interface FutureFactory {
    call<T>(fn: (...args: any[]) => T, receiver?: unknown, args?: unknown[]): Future<T>;
    all<T>(futures: Future<T>[]): Future<T[]>;
    race<T>(futures: Future<T>[]): Future<{ index: number; value: T }>;
    sleep(ms: number): Future<void>;
    timeout<T>(future: Future<T>, timeoutMs: number): Future<T>;
  }

  interface EventQueue<T> {
    receive(timeoutMs?: number): T | null;
    stats(): EventQueueStats;
    close(): boolean;
  }

  interface EventQueueStats {
    open: boolean;
    queued: number;
    capacity: number;
    dropped: number;
    /** Maximum queued events since creation or the last framework counter reset. */
    highWater: number;
    receiverPending: boolean;
  }

  /**
   * Byte payload accepted by low-level transports.
   *
   * Array-like lengths must be finite integers in 0..2147483647 and elements
   * must be numeric integers in 0..255; coercion and wraparound are rejected.
   * Plain array-like values are copied by the transport. Native `ByteView`
   * values returned by modules such as `bitmap.readRect(...)` can be
   * passed directly without first converting them to JavaScript arrays.
   */
  type ByteSource = ArrayLike<number> | ByteView;

  /**
   * Native byte view containing a stable, immutable owned snapshot.
   *
   * Backing bytes do not change while the view is open. Transports retain an
   * in-flight read lease. `close()` is idempotent and requests deterministic
   * early release; an active transport lease delays the native free without
   * making `close()` fail.
   */
  interface ByteView {
    readonly length: number;
    readonly byteLength: number;
    getUint8(offset: number): number;
    toArray(): number[];
    close(): boolean;
  }

  /**
   * Retained native byte span source.
   *
   * Transports open byte spans on demand from this opaque capability.
   * Producer-specific controls live on subtypes such as
   * `BitmapSpanSource`.
   */
  interface ByteSpanSource {
    readonly __byteSpanSourceBrand: never;
    /** Total bytes the one-shot producer will yield. */
    readonly byteLength: number;
    /** Close the source and release its producer-owned resources. Idempotent. */
    close(): boolean;
  }

  /** Retained Bitmap byte span source. */
  interface BitmapSpanSource extends ByteSpanSource {
    setRect(x: number, y: number, width: number, height: number): this;
  }

  type BitmapFormat = "mono1" | "gray4" | "gray8" | "rgb565" | "rgb888";
  type BitmapLayout = "linear" | "page-y8";
  type BitmapStorage = "auto" | "internal" | "psram" | "dma";
  type BitmapByteOrder = "be" | "le";
  type BitmapBitOrder = "lsb" | "msb";
  type BitmapRotation = 0 | 90 | 180 | 270;
  type BitmapFilter = "nearest" | "bilinear";
  type BitmapDither = "none" | "bayer4x4";
  type DisplayByteOrder = "be" | "le";
  type DisplayColor = number;
  type DisplayPoint = { x: number; y: number } | readonly [number, number];
  type DisplayPointList = ArrayLike<number> | ArrayLike<DisplayPoint>;

  interface DisplayDirtyRect {
    x: number;
    y: number;
    width: number;
    height: number;
  }

  interface DisplayTextMetrics {
    width: number;
    height: number;
    lines: number;
  }

  interface BitmapCreateOptions {
    width: number;
    height: number;
    format: BitmapFormat;
    layout?: BitmapLayout;
    storage?: BitmapStorage;
    stride?: number;
    pageHeight?: number;
    chunkBytes?: number;
    foreground?: DisplayColor;
    background?: DisplayColor;
  }

  interface BitmapRect {
    x: number;
    y: number;
    width: number;
    height: number;
  }

  interface BitmapDescriptor {
    width: number;
    height: number;
    format: BitmapFormat;
    pixels: ByteSource;
    stride?: number;
    /** `page-y8` is accepted only for `mono1`; other formats are linear. */
    layout?: BitmapLayout;
    /** Accepted only for `rgb565`. */
    byteOrder?: BitmapByteOrder;
    /** Accepted only for `mono1`. Defaults to `lsb`. */
    bitOrder?: BitmapBitOrder;
  }

  type BitmapSource = Bitmap | CameraFrame | BitmapDescriptor;

  interface EncodedImageChunks {
    /** Byte sources whose useful bytes form one compressed image in order. */
    chunks: ArrayLike<ByteSource>;
    /** Useful leading bytes in each source; trailing transport metadata is ignored. */
    lengths: ArrayLike<number>;
    /** Exact sum of `lengths`. */
    byteLength: number;
  }

  type EncodedImageSource = ByteSource | EncodedImageChunks;

  interface BitmapDecodeOptions {
    /** Requires the optional `bitmap_jpeg` native feature. */
    codec: "jpeg";
    /** Defaults to `(0, 0)` at the JPEG's natural dimensions. */
    destinationRect?: BitmapRect;
  }

  interface BitmapDecodeResult {
    codec: "jpeg";
    engine: "rom-tjpgd" | "software-tjpgd";
    width: number;
    height: number;
    inputBytes: number;
    outputBytes: number;
  }

  interface BitmapTransformOptions {
    /** Crop first. Defaults to the complete source. */
    sourceRect?: BitmapRect;
    /** Clockwise rotation, applied after cropping. */
    rotation?: BitmapRotation;
    /** Applied in the rotated coordinate system. */
    flipX?: boolean;
    /** Applied in the rotated coordinate system. */
    flipY?: boolean;
    filter?: BitmapFilter;
    /** Available only for grayscale outputs. */
    normalize?: boolean;
    /** Available only for `mono1` outputs. Defaults to 128. */
    threshold?: number;
    /** Available only for `mono1` outputs. */
    dither?: BitmapDither;
  }

  interface BitmapConvertOptions extends BitmapTransformOptions {
    format: BitmapFormat;
    /** Defaults to the rotated natural width. */
    width?: number;
    /** Defaults to the rotated natural height. */
    height?: number;
    layout?: BitmapLayout;
    storage?: BitmapStorage;
    stride?: number;
  }

  interface BitmapBlitOptions extends BitmapTransformOptions {
    /** Defaults to `(0, 0)` with the rotated natural dimensions. */
    destinationRect?: BitmapRect;
  }

  interface BitmapBlitOperation {
    source: BitmapSource;
    options?: BitmapBlitOptions;
  }

  interface BitmapReadRectOptions {
    byteOrder?: DisplayByteOrder;
  }

  interface BitmapReadRectChunksOptions extends BitmapReadRectOptions {
    chunkBytes?: number;
  }

  interface DisplaySpanSourceOptions extends BitmapReadRectOptions {
    chunkBytes?: number;
  }

  interface DisplayCommandBufferOptions {
    commandCapacity?: number;
    textBytes?: number;
  }

  interface DisplayCommandBufferPackedOptions {
    /**
     * Concatenated encoded text payload referenced by packed text commands.
     */
    text?: string;
    /**
     * Native font used by packed text commands in this append batch.
     */
    font?: DisplayFont;
  }

  interface DisplayCommandBufferStats {
    count: number;
    capacity: number;
    textBytes: number;
    textCapacity: number;
  }

  interface DisplayBezierOptions {
    /** Segment count is clamped by the runtime to the supported range. */
    segments?: number;
  }

  interface DisplayMask {
    width: number;
    height: number;
    pixels: ArrayLike<number>;
  }

  interface DisplayMaskOptions {
    color?: DisplayColor;
    /** Omit or pass `null` to keep off pixels transparent. */
    background?: DisplayColor | null;
  }

  interface DisplayTextOptions {
    /** Native font returned by `bitmap.loadFont(path)`. */
    font: DisplayFont;
    color?: DisplayColor;
    /** Omit or pass `null` to keep glyph backgrounds transparent. */
    background?: DisplayColor | null;
    spacing?: number;
  }

  /**
   * Retained native draw-command list that can be replayed into a
   * `Bitmap` once per frame.
   */
  class DisplayCommandBuffer {
    private constructor();
    reset(): this;
    close(): boolean;
    clear(color?: DisplayColor): this;
    fill(color?: DisplayColor): this;
    fillRect(
      x: number,
      y: number,
      width: number,
      height: number,
      color?: DisplayColor,
    ): this;
    drawRect(
      x: number,
      y: number,
      width: number,
      height: number,
      color?: DisplayColor,
    ): this;
    drawLine(
      x0: number,
      y0: number,
      x1: number,
      y1: number,
      color?: DisplayColor,
    ): this;
    drawRoundRect(
      x: number,
      y: number,
      width: number,
      height: number,
      radius: number,
      color?: DisplayColor,
    ): this;
    fillRoundRect(
      x: number,
      y: number,
      width: number,
      height: number,
      radius: number,
      color?: DisplayColor,
    ): this;
    drawText(
      x: number,
      y: number,
      text: string,
      options: DisplayTextOptions,
    ): this;
    appendPacked(bytes: ByteSource, options?: DisplayCommandBufferPackedOptions): this;
    replay(target: Bitmap): this;
    stats(): DisplayCommandBufferStats;
  }

  /**
   * HTTP header collection.
   *
   * @example
   * ```js
   * var headers = new Headers({ "content-type": "text/plain" });
   * print(headers.get("content-type"));
   * ```
   */
  class Headers {
    constructor(init?: HeadersInit);
    get(name: string): string | null;
    set(name: string, value: string): void;
    has(name: string): boolean;
    delete(name: string): boolean;
    entries(): Array<[string, string]>;
    toObject(): HeaderRecord;
  }

  /**
   * Request initialization options.
   *
   * @example
   * ```js
   * var req = new Request("https://example.com", {
   *   method: "POST",
   *   headers: { "content-type": "application/json" },
   *   body: JSON.stringify({ hello: "world" })
   * });
   * ```
   */
  interface RequestInit {
    method?: string;
    headers?: HeadersInit;
    body?: RequestBody;
  }

  /**
   * HTTP request object shared by `http.fetch(...)` and `http.server(...)`.
   *
   * @example
   * ```js
   * var req = new Request("https://example.com/test?a=1");
   * print(req.path, req.query.a);
   * ```
   */
  class Request {
    constructor(input: string | Request, init?: RequestInit);
    method: string;
    url: string;
    path: string;
    route: string;
    mountPath: string;
    relativePath: string;
    queryString: string;
    query: Record<string, string>;
    headers: Headers;
    body: Stream;
    text(): string;
    bytes(maxBytes?: number): ByteView;
    json<T = unknown>(): T;
  }

  /**
   * Response initialization options.
   */
  interface ResponseInit {
    status?: number;
    statusText?: string;
    headers?: HeadersInit;
  }

  /**
   * HTTP response object shared by `http.fetch(...)` and `http.server(...)`.
   *
   * @example
   * ```js
   * var res = Response.text("pong", {
   *   headers: { "content-type": "text/plain; charset=utf-8" }
   * });
   * print(res.status, res.ok);
   * ```
   */
  class Response {
    constructor(body?: RequestBody, init?: ResponseInit);
    readonly ok: boolean;
    status: number;
    statusText: string;
    url: string;
    headers: Headers;
    body: Stream;
    text(): string;
    bytes(maxBytes?: number): ByteView;
    json<T = unknown>(): T;
    static text(text: string, init?: ResponseInit): Response;
    static json(value: JsonValue, init?: ResponseInit): Response;
    static stream(stream: Stream, init?: ResponseInit): Response;
    static bytes(body: ByteView | ByteSpanSource, init?: ResponseInit): Response;
  }

  /**
   * Unified stream interface used by files, requests, and responses.
   *
   * @example
   * ```js
   * var stream = fs.open("_sys/display.js", "r");
   * print(stream.tell());
   * print(JSON.stringify(stream.read(32)));
   * stream.close();
   * ```
   */
  class Stream {
    private constructor();
    static readonly SEEK_SET: 0;
    static readonly SEEK_CUR: 1;
    static readonly SEEK_END: 2;

    readonly kind: string;
    readonly path: string;
    readonly mode: string;
    readonly readable: boolean;
    readonly writable: boolean;

    /** Text modes assume trusted text; binary modes return an owned ByteView. */
    read(size?: number): string | ByteView | null;
    write(data: string | ByteSource | ByteSpanSource): number;
    flush(): boolean;
    close(): boolean;
    seek(offset: number, whence?: number): number;
    tell(): number;
    eof(): boolean;
  }

  type FsOpenMode = "r" | "rb" | "w" | "wb" | "a" | "ab" | "r+" | "w+" | "a+";

  /**
   * File-system entry returned by `fs.list()` and `fs.stat()`.
   */
  interface FsEntry {
    name: string;
    path: string;
    isDir: boolean;
    size: number;
  }

  interface FsInfo {
    root: string;
    /** True when the underlying mounted filesystem rejects mutations. */
    readOnly: boolean;
    totalBytes: number;
    usedBytes: number;
    freeBytes: number;
  }

  interface FsReadTextOptions {
    /** Per-call bound, capped by CONFIG_ESP32_MQUICKJS_FS_READ_TEXT_MAX_BYTES. */
    maxBytes?: number;
  }

  interface FsWatchOptions {
    /** EventQueue capacity in 1..64. Defaults to 8. */
    capacity?: number;
  }

  interface FsChangeEvent {
    sequence: number;
    timestampUs: number;
    type: "write" | "remove" | "rename" | "mkdir";
    path: string;
    toPath?: string;
  }

  /**
   * LittleFS helpers restricted to the active mounted root.
   *
   * @example
   * ```js
   * fs.writeText("notes.txt", "hello\\n");
   * print(fs.readText("notes.txt"));
   * print(JSON.stringify(fs.list(".")));
   * fs.remove("notes.txt");
   * ```
   */
  interface FsVolume {
    readonly ROOT: string;
    volume(root: string): FsVolume;
    info(): FsInfo;
    watch(options?: FsWatchOptions): EventQueue<FsChangeEvent>;
    open(path: string, mode?: FsOpenMode): Stream;
    list(path?: string): FsEntry[];
    stat(path: string): FsEntry;
    exists(path: string): boolean;
    readText(path: string, options?: FsReadTextOptions): string;
    writeText(path: string, text: string): number;
    appendText(path: string, text: string): number;
    mkdir(path: string): boolean;
    rename(fromPath: string, toPath: string): boolean;
    remove(path: string): boolean;
  }

  interface FrameworkModule {
    load(path: string): unknown;
  }

  interface NVSStatus {
    initialized: boolean;
    encrypted: boolean;
    maxValueBytes: number;
  }

  /** Bounded atomic string storage in the default NVS partition. */
  interface NVSModule {
    readonly MAX_VALUE_BYTES: number;
    getString(namespace: string, key: string): string | null;
    setString(namespace: string, key: string, value: string): number;
    erase(namespace: string, key: string): boolean;
    clear(namespace: string): boolean;
    status(): NVSStatus;
  }

  type GpioMode =
    | "disabled"
    | "input"
    | "output"
    | "inputOutput"
    | "outputOpenDrain"
    | "inputOutputOpenDrain";

  type GpioPullMode = "floating" | "pullup" | "pulldown" | "pullupPulldown";

  type GpioDriveStrength = 0 | 1 | 2 | 3;

  type GpioInterruptMode = "change" | "rising" | "falling" | "low" | "high";

  interface GpioInterruptEvent {
    sequence: number;
    timestampUs: number;
    pin: number;
    level: boolean;
    mode: GpioInterruptMode;
  }

  interface GpioStatus {
    pin: number;
    valid: boolean;
    outputCapable: boolean;
    mode: GpioMode;
    pull: GpioPullMode;
    level: boolean;
    inputEnabled: boolean;
    outputEnabled: boolean;
    openDrain: boolean;
    pullup: boolean;
    pulldown: boolean;
    driveStrength: GpioDriveStrength;
    held: boolean;
    functionSelect: number;
    signalOut: number;
    outputControlledByPeripheral: boolean;
    outputEnableInverted: boolean;
    sleepEnabled: boolean;
    interruptAttached: boolean;
    interruptMode: GpioInterruptMode | null;
    interruptDropped: number;
  }

  interface GpioConfigureOptions {
    mode?: GpioMode;
    pull?: GpioPullMode;
    level?: boolean | number;
    driveStrength?: GpioDriveStrength;
    hold?: boolean | number;
  }

  /**
   * GPIO helpers bound to the optional wiring profile.
   *
   * @example
   * ```js
   * var pin = gpio.USER_LED_PIN >= 0 ? gpio.USER_LED_PIN : gpio.LED_BUILTIN;
   *
   * // Basic output control.
   * gpio.pinMode(pin, gpio.OUTPUT);
   * gpio.digitalWrite(pin, true);
   * gpio.toggle(pin);
   *
   * var interrupts = gpio.watch(pin, gpio.CHANGE);
   * Future.call(interrupts.receive, interrupts, []).map(function (event) {
   *   if (event) print(event.pin, event.mode, event.level);
   * });
   * ```
   */
  interface GpioModule {
    readonly DISABLED: "disabled";
    readonly INPUT: "input";
    readonly OUTPUT: "output";
    readonly INPUT_OUTPUT: "inputOutput";
    readonly OUTPUT_OPEN_DRAIN: "outputOpenDrain";
    readonly INPUT_OUTPUT_OPEN_DRAIN: "inputOutputOpenDrain";
    readonly FLOATING: "floating";
    readonly PULLUP: "pullup";
    readonly PULLDOWN: "pulldown";
    readonly PULLUP_PULLDOWN: "pullupPulldown";
    readonly CHANGE: "change";
    readonly RISING: "rising";
    readonly FALLING: "falling";
    readonly LOW: 0;
    readonly HIGH: 1;
    readonly DRIVE_0: 0;
    readonly DRIVE_1: 1;
    readonly DRIVE_2: 2;
    readonly DRIVE_3: 3;
    readonly LED_BUILTIN: number;
    readonly USER_LED_PIN: number;
    readonly USER_LED_ACTIVE_LOW: boolean;
    isValid(pin: number): boolean;
    isOutputCapable(pin: number): boolean;
    pinMode(pin: number, mode: GpioMode): number;
    setPull(pin: number, mode: GpioPullMode): number;
    status(pin: number): GpioStatus;
    configure(pin: number, options: GpioConfigureOptions): GpioStatus;
    digitalWrite(pin: number, value: boolean): boolean;
    digitalRead(pin: number): boolean;
    toggle(pin: number): boolean;
    getDriveStrength(pin: number): GpioDriveStrength;
    setDriveStrength(
      pin: number,
      strength: GpioDriveStrength,
    ): GpioDriveStrength;
    hold(pin: number, enabled: boolean): boolean;
    watch(pin: number, mode?: GpioInterruptMode | 0 | 1): EventQueue<GpioInterruptEvent>;
    reset(pin: number): number;
    led(value: boolean): boolean;
  }

  type LedcClock = "auto" | "apb" | "xtal" | "rcFast";
  type LedcSleepMode = "noAliveNoPd" | "noAliveAllowPd" | "keepAlive";

  interface LedcTimerStatus {
    timer: number;
    configured: boolean;
    paused: boolean;
    freqHz: number;
    dutyResolution: number;
    maxDuty: number;
    clock: LedcClock;
  }

  interface LedcChannelStatus {
    channel: number;
    configured: boolean;
    pin: number;
    timer: number;
    duty: number;
    hpoint: number;
    maxDuty: number;
    outputInvert: boolean;
    sleepMode: LedcSleepMode;
  }

  interface LedcTimerSetupOptions {
    freqHz: number;
    dutyResolution: number;
    clock?: LedcClock;
    deconfigure?: false;
  }

  interface LedcTimerDeconfigureOptions {
    deconfigure: true;
    freqHz?: never;
    dutyResolution?: never;
    clock?: never;
  }

  type LedcTimerConfigOptions =
    | LedcTimerSetupOptions
    | LedcTimerDeconfigureOptions;

  interface LedcChannelConfigOptions {
    pin?: number;
    timer?: number;
    duty?: number;
    hpoint?: number;
    outputInvert?: boolean;
    sleepMode?: LedcSleepMode;
    deconfigure?: boolean;
  }

  /**
   * LEDC PWM timer/channel helpers.
   *
   * @example
   * ```js
   * ledc.timerConfig(0, { freqHz: 5000, dutyResolution: 8 });
   * ledc.channelConfig(0, { pin: gpio.LED_BUILTIN, timer: 0, duty: 128 });
   * ledc.setDutyAndUpdate(0, 64);
   * ```
   */
  interface LedcModule {
    readonly AUTO_CLOCK: "auto";
    readonly APB_CLOCK: "apb";
    readonly XTAL_CLOCK: "xtal";
    readonly RC_FAST_CLOCK: "rcFast";
    readonly SLEEP_NO_ALIVE_NO_PD: "noAliveNoPd";
    readonly SLEEP_NO_ALIVE_ALLOW_PD: "noAliveAllowPd";
    readonly SLEEP_KEEP_ALIVE: "keepAlive";
    readonly CHANNEL_COUNT: number;
    readonly TIMER_COUNT: number;
    readonly MAX_DUTY_RESOLUTION_BITS: number;
    timerConfig(
      timer: number,
      options: LedcTimerConfigOptions,
    ): LedcTimerStatus;
    channelConfig(
      channel: number,
      options: LedcChannelConfigOptions,
    ): LedcChannelStatus;
    setDuty(channel: number, duty: number): LedcChannelStatus;
    setDutyWithHpoint(
      channel: number,
      duty: number,
      hpoint: number,
    ): LedcChannelStatus;
    setDutyAndUpdate(
      channel: number,
      duty: number,
      hpoint?: number,
    ): LedcChannelStatus;
    getDuty(channel: number): number;
    getHpoint(channel: number): number;
    updateDuty(channel: number): LedcChannelStatus;
    setFreq(timer: number, freqHz: number): LedcTimerStatus;
    getFreq(timer: number): number;
    bindChannelTimer(channel: number, timer: number): LedcChannelStatus;
    stop(channel: number, idleLevel?: boolean): LedcChannelStatus;
    timerPause(timer: number): LedcTimerStatus;
    timerResume(timer: number): LedcTimerStatus;
    timerStatus(timer: number): LedcTimerStatus;
    channelStatus(channel: number): LedcChannelStatus;
  }

  type AdcUnit = 1 | 2;
  type AdcAtten = 0 | 1 | 2 | 3;
  type AdcBitwidth = 0 | 9 | 10 | 11 | 12 | 13;

  interface AdcChannelInfo {
    channel: number;
    configured: boolean;
    atten: AdcAtten | null;
    bitwidth: AdcBitwidth | null;
    pin: number | null;
    calibrated: boolean;
  }

  interface AdcStatus {
    unit: AdcUnit;
    opened: boolean;
    channelCount: number;
    channels: AdcChannelInfo[];
  }

  interface AdcConfigureOptions {
    atten?: AdcAtten;
    bitwidth?: AdcBitwidth;
  }

  interface AdcChannelRef {
    unit: AdcUnit;
    channel: number;
  }

  /**
   * ADC oneshot helpers and GPIO/channel mapping.
   *
   * @example
   * ```js
   * var ref = adc.ioToChannel(0);
   * adc.open(ref.unit);
   * adc.configure(ref.unit, ref.channel, { atten: adc.ATTEN_DB_12, bitwidth: adc.BITWIDTH_12 });
   * print(adc.read(ref.unit, ref.channel));
   * ```
   */
  interface AdcModule {
    readonly UNIT_1: 1;
    readonly UNIT_2: 2;
    readonly ATTEN_DB_0: 0;
    readonly ATTEN_DB_2_5: 1;
    readonly ATTEN_DB_6: 2;
    readonly ATTEN_DB_12: 3;
    readonly BITWIDTH_DEFAULT: 0;
    readonly BITWIDTH_9: 9;
    readonly BITWIDTH_10: 10;
    readonly BITWIDTH_11: 11;
    readonly BITWIDTH_12: 12;
    readonly BITWIDTH_13: 13;
    readonly UNIT_COUNT: number;
    readonly MAX_CHANNEL_COUNT: number;
    open(unit: AdcUnit): AdcStatus;
    close(unit: AdcUnit): boolean;
    status(unit: AdcUnit): AdcStatus;
    configure(
      unit: AdcUnit,
      channel: number,
      options: AdcConfigureOptions,
    ): AdcStatus;
    read(unit: AdcUnit, channel: number): number;
    readMilliVolts(unit: AdcUnit, channel: number): number;
    ioToChannel(pin: number): AdcChannelRef | null;
    channelToIo(unit: AdcUnit, channel: number): number | null;
  }

  type DacChannel = 0 | 1;

  interface DacChannelRef {
    channel: DacChannel;
    pin: number;
  }

  interface DacChannelStatus {
    channel: DacChannel;
    opened: boolean;
    pin: number;
    resolutionBits: number;
    maxValue: number;
    lastValue: number;
  }

  /**
   * DAC oneshot helpers and GPIO/channel mapping.
   *
   * @example
   * ```js
   * dac.open(dac.CHANNEL_0);
   * dac.write(dac.CHANNEL_0, 128);
   * print(JSON.stringify(dac.status(dac.CHANNEL_0)));
   * dac.close(dac.CHANNEL_0);
   * ```
   */
  interface DacModule {
    readonly CHANNEL_0: 0;
    readonly CHANNEL_1: 1;
    readonly CHANNEL_COUNT: number;
    readonly RESOLUTION_BITS: number;
    readonly MAX_VALUE: number;
    open(channel: DacChannel): DacChannelStatus;
    close(channel: DacChannel): boolean;
    status(): DacChannelStatus[];
    status(channel: DacChannel): DacChannelStatus;
    write(channel: DacChannel, value: number): DacChannelStatus;
    ioToChannel(pin: number): DacChannelRef | null;
    channelToIo(channel: DacChannel): number;
  }

  /**
   * Native EQF1 fixed bitmap font loaded by `bitmap.loadFont(...)`.
   */
  interface DisplayFont {
    name: string;
    width: number;
    height: number;
    advance: number;
    lineHeight: number;
  }

  /**
   * Runtime class value for native fonts. Direct construction throws; use
   * `bitmap.loadFont(path)`.
   */
  interface DisplayFontConstructor {
    readonly prototype: DisplayFont;
  }

  /**
   * Native pixel buffer for raw image transforms and low-level display drivers.
   *
   * Pixel colors are packed numeric values: mono1 uses 0/1, gray4 uses 4-bit
   * intensity, gray8 uses 8-bit intensity, rgb565 uses 16-bit RGB565, and
   * rgb888 uses 0xRRGGBB. Drawing
   * methods mutate the buffer, mark dirty bounds, and return the same Bitmap.
   */
  class Bitmap {
    private constructor();
    readonly width: number;
    readonly height: number;
    readonly format: BitmapFormat;
    readonly layout: BitmapLayout;
    readonly stride: number;
    readonly pageHeight: number;
    readonly byteLength: number;

    close(): boolean;
    clear(color?: DisplayColor): this;
    fill(color?: DisplayColor): this;
    setPixel(x: number, y: number, color: DisplayColor): this;
    getPixel(x: number, y: number): DisplayColor;
    fillRect(
      x: number,
      y: number,
      width: number,
      height: number,
      color?: DisplayColor,
    ): this;
    drawCircle(
      cx: number,
      cy: number,
      radius: number,
      color?: DisplayColor,
    ): this;
    fillCircle(
      cx: number,
      cy: number,
      radius: number,
      color?: DisplayColor,
    ): this;
    drawEllipse(
      cx: number,
      cy: number,
      rx: number,
      ry: number,
      color?: DisplayColor,
    ): this;
    fillEllipse(
      cx: number,
      cy: number,
      rx: number,
      ry: number,
      color?: DisplayColor,
    ): this;
    drawRect(
      x: number,
      y: number,
      width: number,
      height: number,
      color?: DisplayColor,
    ): this;
    drawRoundRect(
      x: number,
      y: number,
      width: number,
      height: number,
      radius: number,
      color?: DisplayColor,
    ): this;
    fillRoundRect(
      x: number,
      y: number,
      width: number,
      height: number,
      radius: number,
      color?: DisplayColor,
    ): this;
    drawLine(
      x0: number,
      y0: number,
      x1: number,
      y1: number,
      color?: DisplayColor,
    ): this;
    drawPolyline(points: DisplayPointList, color?: DisplayColor): this;
    drawPolygon(points: DisplayPointList, color?: DisplayColor): this;
    fillPolygon(points: DisplayPointList, color?: DisplayColor): this;
    drawTriangle(
      x0: number,
      y0: number,
      x1: number,
      y1: number,
      x2: number,
      y2: number,
      color?: DisplayColor,
    ): this;
    fillTriangle(
      x0: number,
      y0: number,
      x1: number,
      y1: number,
      x2: number,
      y2: number,
      color?: DisplayColor,
    ): this;
    drawQuadraticBezier(
      x0: number,
      y0: number,
      cx: number,
      cy: number,
      x1: number,
      y1: number,
      color?: DisplayColor,
      options?: DisplayBezierOptions,
    ): this;
    drawCubicBezier(
      x0: number,
      y0: number,
      c1x: number,
      c1y: number,
      c2x: number,
      c2y: number,
      x1: number,
      y1: number,
      color?: DisplayColor,
      options?: DisplayBezierOptions,
    ): this;
    drawMask(
      x: number,
      y: number,
      mask: DisplayMask,
      options?: DisplayMaskOptions,
    ): this;
    blit(source: BitmapSource, options?: BitmapBlitOptions): this;
    /** Apply 1..16 ordered transforms through one native worker operation. */
    blitBatch(operations: readonly BitmapBlitOperation[]): this;
    /** Decode one baseline JPEG when `sys.info.features.bitmapJpeg` is true. */
    decode(source: EncodedImageSource, options: BitmapDecodeOptions): BitmapDecodeResult;
    drawText(
      x: number,
      y: number,
      text: string,
      options: DisplayTextOptions,
    ): this;
    measureText(text: string, options: DisplayTextOptions): DisplayTextMetrics;
    getDirty(): DisplayDirtyRect | null;
    clearDirty(): this;
    markDirty(x: number, y: number, width: number, height: number): this;
    readRect(
      x: number,
      y: number,
      width: number,
      height: number,
      options?: BitmapReadRectOptions,
    ): ByteView;
    readRectChunks(
      x: number,
      y: number,
      width: number,
      height: number,
      options?: BitmapReadRectChunksOptions,
    ): ByteView[];
    createSpanSource(options?: DisplaySpanSourceOptions): BitmapSpanSource;
    createCommandBuffer(options?: DisplayCommandBufferOptions): DisplayCommandBuffer;
  }

  /**
   * Native bitmap module. Exposed only when
   * `sys.info.features.bitmap` is enabled.
   */
  interface BitmapModule {
    readonly MONO1: "mono1";
    readonly GRAY4: "gray4";
    readonly GRAY8: "gray8";
    readonly RGB565: "rgb565";
    readonly RGB888: "rgb888";
    create(options: BitmapCreateOptions): Bitmap;
    convert(source: BitmapSource, options: BitmapConvertOptions): Bitmap;
    loadFont(path: string): DisplayFont;
  }

  type SysPsramMode = "none" | "quad" | "octal";
  type SysSchedulerState = "not-started" | "running" | "suspended";
  type SysTaskState =
    | "running"
    | "ready"
    | "blocked"
    | "suspended"
    | "deleted"
    | "invalid";
  type SysRuntimeState =
    | "created"
    | "starting"
    | "running"
    | "quiescing"
    | "restarting"
    | "stopping"
    | "stopped"
    | "failed";
  type SysControlAction = "restart-runtime" | "reboot";
  type SysRestartFailureAction = "reboot" | "stop";

  interface SysVersionInfo {
    readonly framework: string;
    readonly hostApi: 1;
    readonly mquickjs: string;
    readonly espIdf: string;
  }

  interface SysFeatures {
    readonly fs: boolean;
    readonly nvs: boolean;
    readonly gpio: boolean;
    readonly ledc: boolean;
    readonly adc: boolean;
    readonly dac: boolean;
    readonly i2c: boolean;
    readonly spi: boolean;
    readonly uart: boolean;
    readonly rmt: boolean;
    readonly i2s: boolean;
    readonly camera: boolean;
    readonly net: boolean;
    readonly usbSerial: boolean;
    readonly socket: boolean;
    readonly websocket: boolean;
    readonly bitmap: boolean;
    readonly bitmapJpeg: boolean;
    readonly wifi: boolean;
    readonly wifiCsi: boolean;
    readonly espNow: boolean;
    readonly ble: boolean;
    readonly tls: boolean;
    readonly http: boolean;
    readonly httpServer: boolean;
    readonly rpc: boolean;
    readonly runtimeLogs: boolean;
  }

  interface SysChipRevision {
    raw: number;
    major: number;
    minor: number;
  }

  interface SysChipCapabilities {
    embeddedFlash: boolean;
    wifi: boolean;
    ble: boolean;
    bluetoothClassic: boolean;
    ieee802154: boolean;
    embeddedPsram: boolean;
  }

  interface SysChipInfo {
    model: string;
    revision: SysChipRevision;
    cores: number;
    capabilities: SysChipCapabilities;
  }

  interface SysCpuInfo {
    configuredFrequencyHz: number;
  }

  interface SysFlashInfo {
    sizeBytes: number;
  }

  interface SysPsramInfo {
    enabled: boolean;
    sizeBytes: number;
    mode: SysPsramMode;
  }

  interface SysHardwareInfo {
    readonly hardwareId: string | null;
    readonly target: string;
    readonly chip: SysChipInfo;
    readonly cpu: SysCpuInfo;
    readonly flash: SysFlashInfo;
    readonly psram: SysPsramInfo;
  }

  interface SysRuntimeHeapInfo {
    sizeBytes: number;
    region: "internal" | "psram";
  }

  interface SysRuntimeTaskInfo {
    name: string;
    stackSizeBytes: number;
    priority: number;
    watchdogEnabled: boolean;
  }

  interface SysRuntimeStartupInfo {
    script: string;
    autorun: boolean;
    repl: boolean;
  }

  interface SysRuntimeSecondaryFilesystemInfo {
    partition: string;
    root: string;
    required: boolean;
  }

  interface SysRuntimeFilesystemInfo {
    root: string;
    mount: boolean;
    required: boolean;
    readOnly: boolean;
    formatOnMountFail: boolean;
    secondary: SysRuntimeSecondaryFilesystemInfo | null;
  }

  interface SysRuntimeControlInfo {
    restartRuntime: boolean;
    reboot: boolean;
    restartTimeoutMs: number | null;
    restartFailureAction: SysRestartFailureAction | null;
  }

  interface SysRuntimeInfo {
    readonly heap: SysRuntimeHeapInfo;
    readonly task: SysRuntimeTaskInfo | null;
    readonly evalTimeoutMs: number;
    readonly startup: SysRuntimeStartupInfo | null;
    readonly filesystem: SysRuntimeFilesystemInfo | null;
    readonly control: SysRuntimeControlInfo;
  }

  interface SysInfo {
    readonly version: SysVersionInfo;
    readonly hardware: SysHardwareInfo;
    readonly features: SysFeatures;
    readonly runtime: SysRuntimeInfo;
  }

  interface SysResetStatus {
    code: number;
    name: string;
  }

  interface SysWakeupStatus {
    mask: number;
    names: string[];
  }

  interface SysBootStatus {
    readonly bootId: string;
    readonly uptimeMs: number;
    readonly reset: SysResetStatus;
    readonly wakeup: SysWakeupStatus;
    readonly softwareReason: string | null;
  }

  interface SysCpuStatus {
    readonly frequencyHz: number | null;
  }

  interface SysHeapStatus {
    totalBytes: number;
    freeBytes: number;
    allocatedBytes: number;
    minimumFreeBytes: number;
    largestFreeBlockBytes: number;
    allocatedBlocks: number;
    freeBlocks: number;
    totalBlocks: number;
  }

  type SysMemoryPressure = "normal" | "guarded" | "critical";

  type SysMemoryAllocationClass =
    | "pinned-internal"
    | "dma-internal"
    | "dma-external"
    | "external"
    | "hot-movable"
    | "cold-movable"
    | "cache-evictable";

  interface SysMemoryAllocationStatus {
    owner: string;
    class: SysMemoryAllocationClass;
    region: "internal" | "psram";
    bytes: number;
    blocks: number;
  }

  interface SysWirelessMemoryRegion {
    limitBytes: number;
    controlReserveBytes: number;
    /** Requested payload and tracking bytes, including pending alloc/free. */
    reservedBytes: number;
    /** Peak since boot/last framework counter reset; includes rolled-back attempts. */
    highWaterBytes: number;
    roles: {
      control: number;
      pool: number;
      retiredPool: number;
      queue: number;
      tx: number;
      stack: number;
      copy: number;
    };
  }

  interface SysWirelessMemoryBudget {
    internal: SysWirelessMemoryRegion;
    psram: SysWirelessMemoryRegion;
    /** Saturating count of denied reservation attempts, including fallbacks. */
    rejectedReservations: number;
  }

  interface SysMemoryManagerStatus {
    /** Registered wireless allocations only; excludes SDK/JS/unmanaged heaps. */
    wireless: SysWirelessMemoryBudget;
    pressure: SysMemoryPressure;
    internalReserveBytes: number;
    dmaLargestReserveBytes: number;
    managedInternalBytes: number;
    managedPsramBytes: number;
    /** Managed pinned bytes plus driver DMA payloads and staging pools. */
    pinnedBytes: number;
    /** Driver-owned DMA payload bytes registered with the memory manager. */
    driverPinnedBytes: number;
    /** Internal DMA bytes committed to reusable driver staging pools. */
    stagingPinnedBytes: number;
    /** Number of committed reusable DMA staging pools. */
    dmaStagingPools: number;
    /** Internal DMA bytes admitted but not yet committed by a driver. */
    pendingDmaReservationBytes: number;
    movableIdleBytes: number;
    migrationCount: number;
    migrationBytes: number;
    evictionCount: number;
    allocationFailures: number;
    allocations: SysMemoryAllocationStatus[];
  }

  interface SysMemoryStatus {
    readonly default: SysHeapStatus;
    readonly internal: SysHeapStatus;
    readonly dma: SysHeapStatus;
    readonly psram: SysHeapStatus | null;
    readonly manager: SysMemoryManagerStatus;
  }

  interface SysRuntimeTaskStatus {
    name: string;
    priority: number;
    currentCore: number;
    stackSizeBytes: number | null;
    stackHighWaterMarkBytes: number;
    watchdogEnabled: boolean;
    watchdogRegistered: boolean;
  }

  interface SysRtosStatus {
    readonly name: "FreeRTOS";
    readonly schedulerState: SysSchedulerState;
    readonly tickRateHz: number;
    readonly taskCount: number;
    readonly runtimeTask: SysRuntimeTaskStatus;
    readonly taskSnapshotSupported: boolean;
    readonly taskSnapshotLimit: number;
  }

  interface SysTimerResourceStatus {
    active: number;
    capacity: number;
  }

  interface SysFutureResourceStatus {
    queued: number;
    pending: number;
    capacity: number;
    userCapacity: number;
    internalReserve: number;
  }

  interface SysEventQueueResourceStatus {
    open: number;
    dropped: number;
    queued: number;
    capacity: number;
    /** Sum of registered queues' individual peaks; not a simultaneous global peak. */
    highWater: number;
  }

  interface SysAsyncPollerResourceStatus {
    registered: number;
    capacity: number;
  }

  interface SysOrphanResourceStatus {
    pending: number;
    capacity: number;
  }

  interface SysRuntimeResourcesStatus {
    timers: SysTimerResourceStatus;
    futures: SysFutureResourceStatus;
    eventQueues: SysEventQueueResourceStatus;
    asyncPollers: SysAsyncPollerResourceStatus;
    orphans: SysOrphanResourceStatus;
  }

  interface SysRuntimeFilesystemStatus {
    root: string;
    mounted: boolean;
    readOnly: boolean;
    secondaryMounted: boolean;
  }

  interface SysRuntimeWatchdogStatus {
    systemEnabled: boolean;
    systemRegistered: boolean;
    jsEnabled: boolean;
    jsRegistered: boolean;
    timeoutMs: number;
    lastOuterHeartbeatAgeMs: number;
  }

  interface SysRuntimeStartupStatus {
    phase: "armed" | "stabilizing" | "healthy" | "safe-mode";
    safeModeActive: boolean;
    safeModeRequested: boolean;
    failureCount: number;
    failureLimit: number;
    healthyAfterMs: number;
    lastFailureReason: string | null;
  }

  interface SysPendingControl {
    action: SysControlAction;
    reason: string;
    requestedAtMs: number;
    dueAtMs: number;
  }

  interface SysRuntimeStatus {
    readonly state: SysRuntimeState;
    readonly generation: number;
    readonly uptimeMs: number;
    readonly restartCount: number;
    readonly lastRestartReason: string | null;
    readonly pendingControl: SysPendingControl | null;
    readonly filesystem: SysRuntimeFilesystemStatus;
    readonly resources: SysRuntimeResourcesStatus;
    readonly watchdog: SysRuntimeWatchdogStatus;
    readonly startup: SysRuntimeStartupStatus;
  }

  interface SysStatus {
    readonly boot: SysBootStatus;
    readonly cpu: SysCpuStatus;
    readonly memory: SysMemoryStatus;
    readonly rtos: SysRtosStatus;
    readonly runtime: SysRuntimeStatus;
  }

  interface SysTaskOptions {
    limit?: number;
  }

  interface SysTaskInfo {
    id: number;
    name: string;
    state: SysTaskState;
    priority: number;
    basePriority: number;
    core: number | null;
    stackHighWaterMarkBytes: number;
  }

  interface SysTaskSnapshot {
    total: number;
    truncated: boolean;
    tasks: SysTaskInfo[];
  }

  interface SysControlOptions {
    reason?: string;
    delayMs?: number;
  }

  interface SysControlReceipt {
    action: SysControlAction;
    reason: string;
    generation: number;
    requestedAtMs: number;
    dueAtMs: number;
  }

  interface SysTimeSyncOptions {
    /** Caller-owned SNTP server names. Firmware does not select a provider. */
    servers: string[];
    /** Integer timeout from 1 through 60000 milliseconds. Defaults to 15000. */
    timeoutMs?: number;
  }

  interface SysTimeSyncResult {
    synchronized: true;
    unixTimeMs: number;
  }

  interface SysTimeStatus {
    /** Whether the wall clock is valid for certificate-date checks. */
    synchronized: boolean;
    /** Whether the single native SNTP operation is currently active. */
    synchronizing: boolean;
    /** Current Unix epoch in milliseconds, or null before the clock is valid. */
    unixTimeMs: number | null;
  }

  interface SysTimeModule {
    status(): SysTimeStatus;
    /**
     * Synchronize the system wall clock over the active network interface.
     * Available when the selected firmware includes networking support.
     * Rejects with error code TIME_SYNC_BUSY if another operation is active.
     */
    sync(options: SysTimeSyncOptions): SysTimeSyncResult;
  }

  /**
   * Lazy system information, live status, diagnostics, and lifecycle controls.
   *
   * @example
   * ```js
   * print(sys.info.hardware.chip.model);
   * print(sys.status.memory.internal.freeBytes);
   * ```
   */
  interface SysModule {
    readonly info: SysInfo;
    readonly status: SysStatus;
    readonly time: SysTimeModule;
    /** Persistent boot choice. Assignment affects the next startup only. */
    safeMode: boolean;
    /** Return a fresh snapshot of every immutable selected hardware-profile value. */
    config(): { [key: string]: string | number | boolean };
    /**
     * Read an immutable value from the selected hardware profile. Returns
     * `undefined` when the key is not present.
     */
    config(key: string): string | number | boolean | undefined;
    tasks(options?: SysTaskOptions): SysTaskSnapshot;
    restartRuntime(options?: SysControlOptions): SysControlReceipt;
    reboot(options?: SysControlOptions): SysControlReceipt;
    millis(): number;
    micros(): number;
    freeHeap(): number;
    /** Return 1..64 cryptographically strong random bytes as lowercase hexadecimal. */
    randomHex(byteLength: number): string;
    /**
     * Run a callback under a scoped deadline. Nested calls may only shorten an
     * already active runtime deadline; they never extend it.
     */
    withTimeout<T>(timeoutMs: number, callback: () => T): T;
  }

  type RuntimeLogSource = "runtime" | "exception" | "esp-idf" | "javascript";

  interface RuntimeLogEntry {
    sequence: number;
    uptimeMs: number;
    source: RuntimeLogSource;
    text: string;
  }

  interface RuntimeLogReadResult {
    bootId: string;
    entries: RuntimeLogEntry[];
    dropped: number;
  }

  interface RuntimeLogsModule {
    read(
      afterSequence: number,
      limit: number,
      maxBytes: number,
    ): RuntimeLogReadResult;
  }

  /**
   * I2C bus state.
   */
  interface I2CBusStatus {
    opened: boolean;
    controller: number;
    sda: number;
    scl: number;
    freqHz: number;
    timeoutMs: number;
    internalPullup: boolean;
    deviceCount: number;
  }

  /**
   * I2C open options.
   */
  interface I2COpenBusOptions {
    sda?: number;
    scl?: number;
    freqHz?: number;
    timeoutMs?: number;
    internalPullup?: boolean;
  }

  interface I2CBatchResult {
    chunks: number;
    bytes: number;
    totalUs: number;
  }

  /**
   * Open I2C bus handle.
   *
   * @example
   * ```js
   * var bus = i2c.openBus({ sda: 5, scl: 6, freqHz: 400000 });
   * print(JSON.stringify(bus.scan()));
   * ```
   */
  interface I2CBus {
    close(): boolean;
    status(): I2CBusStatus;
    scan(): number[];
    openDevice(options: I2CDeviceOptions): I2CDevice;
  }

  interface I2CDeviceOptions {
    address: number;
    freqHz?: number;
    timeoutMs?: number;
  }

  interface I2CDeviceStatus {
    opened: boolean;
    controller: number;
    address: number;
    freqHz: number;
    timeoutMs: number;
  }

  interface I2CDevice {
    close(): boolean;
    status(): I2CDeviceStatus;
    /** Execute one I2C transaction containing one byte source. */
    write(data: ByteSource): number;
    /** Send native segments within one START/STOP transaction. */
    writeSegments(segments: ArrayLike<ByteSource>): number;
    /** Execute byte sources as independent, ordered transactions. */
    writeBatch(chunks: ArrayLike<ByteSource>): I2CBatchResult;
    read(length: number): ByteView;
    writeRead(writeData: ByteSource, readLength: number): ByteView;
  }

  /**
   * I2C factory and constants.
   */
  interface I2CModule {
    readonly DEFAULT_SDA: number;
    readonly DEFAULT_SCL: number;
    readonly DEFAULT_FREQ_HZ: number;
    readonly DEFAULT_TIMEOUT_MS: number;
    openBus(options?: I2COpenBusOptions): I2CBus;
  }

  /**
   * SPI bus state.
   */
  interface SPIBusStatus {
    opened: boolean;
    host: number;
    sclk: number;
    mosi: number;
    miso: number;
    maxTransferSize: number;
    dmaStagingBytes: number;
    deviceCount: number;
  }

  /**
   * SPI device state.
   */
  interface SPIDeviceStatus {
    opened: boolean;
    host: number;
    cs: number;
    mode: 0 | 1 | 2 | 3;
    requestedFreqHz: number;
    actualFreqHz: number;
    queueSize: number;
    csHigh: boolean;
    lsbFirst: boolean;
    directExternalDma: boolean;
    timeoutMs: number;
    dmaStagingBytes: number;
    faulted: boolean;
    lastErrorCode: SPIErrorCode | null;
  }

  /**
   * SPI bus open options.
   */
  interface SPIOpenBusOptions {
    host?: number;
    sclk?: number;
    mosi?: number;
    miso?: number;
    maxTransferSize?: number;
    /** Size of each of the two reusable internal DMA staging slots. */
    dmaStagingBytes?: number;
  }

  /**
   * SPI device open options.
   */
  interface SPIOpenDeviceOptions {
    cs?: number;
    mode?: 0 | 1 | 2 | 3;
    freqHz?: number;
    queueSize?: number;
    csHigh?: boolean;
    lsbFirst?: boolean;
    directExternalDma?: boolean;
    timeoutMs?: number;
  }

  interface SPIOperationOptions {
    timeoutMs?: number;
  }

  interface SPIReadOptions extends SPIOperationOptions {
    fillByte?: number;
  }

  interface SPIWriteOptions {
    /** Number of queued transactions to keep in flight. */
    queueDepth?: number;
    timeoutMs?: number;
  }

  type SPIDmaPath =
    | "direct-internal"
    | "direct-external"
    | "staged-internal"
    | "mixed";

  type SPIErrorCode =
    | "DMA_STAGING_NO_MEMORY"
    | "DMA_TX_UNDERFLOW"
    | "DMA_RX_OVERFLOW"
    | "DMA_TRANSFER_TIMEOUT"
    | "DMA_DEVICE_FAULTED";

  interface SPIError extends NativeError {
    code: SPIErrorCode;
    operation: "openBus" | "write" | "transfer" | "read" | "writeChunks" | "writeSource" | "unknown";
    details: {
      espCode: number;
      espName: string;
      completedBytes: number;
      path: SPIDmaPath;
      requestedFreqHz: number;
      actualFreqHz: number;
    };
  }

  /**
   * Timing and transfer counters returned by SPI bulk-write helpers.
   */
  interface SPIWriteStats {
    bytes: number;
    sourceSpans: number;
    transactions: number;
    path: SPIDmaPath;
    stagedBytes: number;
    copyUs: number;
    queueUs: number;
    waitUs: number;
    transferUs: number;
    totalUs: number;
    queueDepth: number;
  }

  /**
   * Open SPI bus handle.
   *
   * @example
   * ```js
   * var bus = spi.openBus();
   * var dev = bus.openDevice({ cs: spi.DEFAULT_CS, mode: 0, freqHz: 1000000 });
   * print(JSON.stringify(dev.transfer([0x9f])));
   * dev.close();
   * bus.close();
   * ```
   */
  interface SPIBus {
    close(): boolean;
    status(): SPIBusStatus;
    openDevice(options?: SPIOpenDeviceOptions): SPIDevice;
  }

  /**
   * Open SPI device handle.
   */
  interface SPIDevice {
    close(): boolean;
    status(): SPIDeviceStatus;
    transfer(data: ByteSource, options?: SPIOperationOptions): ByteView;
    write(data: ByteSource, options?: SPIOperationOptions): number;
    /**
     * Write an array-like list of byte sources, reusing queued SPI
     * transactions for larger display flushes.
     */
    writeChunks(
      chunks: ArrayLike<ByteSource>,
      options?: SPIWriteOptions,
    ): SPIWriteStats;
    /**
     * Write spans opened from a retained native source without materializing a
     * JavaScript chunk array in the flush loop.
     */
    writeSource(
      source: ByteSpanSource,
      options?: SPIWriteOptions,
    ): SPIWriteStats;
    read(length: number, options?: SPIReadOptions): ByteView;
  }

  /**
   * SPI factory and constants.
   */
  interface SPIModule {
    readonly HOST_2: 2;
    readonly HOST_3?: 3;
    readonly DEFAULT_HOST: number;
    readonly DEFAULT_SCLK: number;
    readonly DEFAULT_MOSI: number;
    readonly DEFAULT_MISO: number;
    readonly DEFAULT_CS: number;
    readonly DEFAULT_FREQ_HZ: number;
    readonly DEFAULT_QUEUE_SIZE: number;
    readonly DEFAULT_MAX_TRANSFER_SIZE: number;
    openBus(options?: SPIOpenBusOptions): SPIBus;
  }

  type UARTParity = "none" | "even" | "odd";
  type UARTStopBits = 1 | 1.5 | 2;

  /**
   * UART port state.
   */
  interface UARTStatus {
    opened: boolean;
    port: number;
    tx: number;
    rx: number;
    baud: number;
    dataBits: 5 | 6 | 7 | 8;
    parity: UARTParity;
    stopBits: UARTStopBits;
    rxBufferSize: number;
    txBufferSize: number;
    timeoutMs: number;
  }

  /**
   * UART open options.
   */
  interface UARTOpenOptions {
    port?: number;
    tx?: number;
    rx?: number;
    baud?: number;
    dataBits?: 5 | 6 | 7 | 8;
    parity?: UARTParity;
    stopBits?: UARTStopBits;
    rxBufferSize?: number;
    txBufferSize?: number;
    timeoutMs?: number;
  }

  interface UARTWriteStats {
    chunks: number;
    bytes: number;
    totalUs: number;
  }

  interface UARTWatchOptions {
    /** Readable threshold. Defaults to 1. */
    minBytes?: number;
    /** Emit idle readiness after this many quiet milliseconds; zero disables it. */
    idleMs?: number;
    /** EventQueue capacity in 1..64. Defaults to 8. */
    capacity?: number;
    /** Include UART hardware error events. Defaults to true. */
    includeErrors?: boolean;
  }

  interface UARTReadableEvent {
    type: "readable";
    sequence: number;
    timestampUs: number;
    availableBytes: number;
    reason: "threshold" | "idle";
  }

  interface UARTErrorEvent {
    type: "error";
    sequence: number;
    timestampUs: number;
    code: "fifoOverflow" | "bufferFull" | "break" | "parity" | "frame";
    droppedBytes?: number;
  }

  type UARTEvent = UARTReadableEvent | UARTErrorEvent;

  /**
   * Cooperative Future-backed UART port handle.
   */
  interface UARTPort {
    close(): boolean;
    status(): UARTStatus;
    write(data: ByteSource): number;
    writeChunks(chunks: ArrayLike<ByteSource>): UARTWriteStats;
    writeSource(source: ByteSpanSource): UARTWriteStats;
    read(length: number, timeoutMs?: number): ByteView | null;
    available(): number;
    flush(timeoutMs?: number): boolean;
    clearRx(): boolean;
    watch(options?: UARTWatchOptions): EventQueue<UARTEvent>;
  }

  /**
   * UART factory and constants.
   */
  interface UARTModule {
    readonly DEFAULT_PORT: number;
    readonly DEFAULT_TX: number;
    readonly DEFAULT_RX: number;
    readonly DEFAULT_BAUD: number;
    readonly DEFAULT_RX_BUFFER_SIZE: number;
    readonly DEFAULT_TX_BUFFER_SIZE: number;
    readonly DEFAULT_TIMEOUT_MS: number;
    open(options?: UARTOpenOptions): UARTPort;
  }

  type RMTDirection = "rx" | "tx";

  interface RMTSymbol {
    duration0Ticks: number;
    level0: boolean;
    duration1Ticks: number;
    level1: boolean;
  }

  interface RMTChannelOpenOptions {
    direction: RMTDirection;
    pin: number;
    resolutionHz: number;
    memorySymbols?: number;
    dma?: boolean;
    invert?: boolean;
  }

  interface RMTTransmitOptions {
    loopCount?: number;
    endLevel?: boolean | 0 | 1;
    timeoutMs?: number;
  }

  interface RMTReceiveOptions {
    minPulseNs?: number;
    idleThresholdNs: number;
    timeoutMs?: number;
  }

  interface RMTTransmitResult {
    symbols: number;
    loopCount: number;
  }

  interface RMTReceiveResult {
    length: number;
    truncated: boolean;
    timestampUs: number;
  }

  interface RMTChannelStatus extends RMTChannelOpenOptions {
    memorySymbols: number;
    dma: boolean;
    invert: boolean;
    running: boolean;
    busy: boolean;
  }

  interface RMTCapabilities {
    rx: true;
    tx: true;
    dma: boolean;
    minMemorySymbols: number;
    maxSymbols: 4096;
    maxDurationTicks: 32767;
    finiteLoops: true;
  }

  class RMTSymbolBuffer {
    private constructor();
    readonly capacity: number;
    readonly length: number;
    push(duration0Ticks: number, level0: boolean | 0 | 1,
      duration1Ticks: number, level1: boolean | 0 | 1): number;
    get(index: number): RMTSymbol;
    set(index: number, duration0Ticks: number, level0: boolean | 0 | 1,
      duration1Ticks: number, level1: boolean | 0 | 1): boolean;
    clear(): boolean;
    close(): boolean;
  }

  class RMTChannel {
    private constructor();
    start(): boolean;
    stop(): boolean;
    transmit(symbols: RMTSymbolBuffer,
      options?: RMTTransmitOptions): RMTTransmitResult;
    receive(symbols: RMTSymbolBuffer,
      options: RMTReceiveOptions): RMTReceiveResult | null;
    status(): RMTChannelStatus;
    close(): boolean;
  }

  interface RMTModule {
    capabilities(): RMTCapabilities;
    createSymbols(capacity: number): RMTSymbolBuffer;
    open(options: RMTChannelOpenOptions): RMTChannel;
  }

  type I2SMode = "standard" | "pdm";
  type I2SDirection = "rx" | "tx" | "duplex";
  type I2SDataBits = 8 | 16 | 24 | 32;
  type I2SSlotMode = "mono" | "stereo";
  type I2SSlotMask = "left" | "right" | "both";
  type I2SStandardFormat = "philips" | "msb" | "pcmShort" | "pcmLong";

  interface I2SDmaOptions {
    /** Number of DMA descriptors. */
    descriptorCount?: number;
    /** PCM frames held by one descriptor. The descriptor must fit in 4092 bytes. */
    framesPerDescriptor?: number;
  }

  interface I2SStandardPins {
    bclk: number;
    ws: number;
    din?: number;
    dout?: number;
    mclk?: number;
  }

  interface I2SPdmPins {
    clk?: number;
    din?: number;
  }

  interface I2SOpenOptionsBase {
    port?: "auto" | number;
    sampleRateHz?: number;
    dma?: I2SDmaOptions;
    timeoutMs?: number;
  }

  interface I2SStandardOpenOptions extends I2SOpenOptionsBase {
    direction: I2SDirection;
    mode: "standard";
    pins: I2SStandardPins;
    dataBits?: I2SDataBits;
    slotBits?: I2SDataBits;
    slotMode?: I2SSlotMode;
    slotMask?: I2SSlotMask;
    format?: I2SStandardFormat;
  }

  interface I2SPdmOpenOptions extends I2SOpenOptionsBase {
    direction: "rx";
    mode: "pdm";
    /** May be omitted when the selected hardware constants provide both pins. */
    pins?: I2SPdmPins;
  }

  type I2SOpenOptions = I2SStandardOpenOptions | I2SPdmOpenOptions;

  interface I2SReadResult {
    /** Owned signed PCM bytes. PDM output is always 16-bit little-endian mono. */
    data: ByteView;
    frames: number;
    byteLength: number;
    timestampUs: number;
    sequence: number;
    overruns: number;
  }

  interface I2SStatus {
    port: number;
    running: boolean;
    direction: I2SDirection;
    mode: I2SMode;
    overruns: number;
    sendQueueOverflows: number;
    readBusy: boolean;
    writeBusy: boolean;
    pcm: {
      sampleRateHz: number;
      dataBits: I2SDataBits;
      slotBits: I2SDataBits;
      channels: 1 | 2;
      signed: true;
      endianness: "little";
    };
    dma: {
      descriptorCount: number;
      framesPerDescriptor: number;
      storage: "internal";
      /** Actual bytes in each driver DMA buffer after ESP-IDF alignment. */
      bufferBytes: number;
      /** Actual driver DMA buffer bytes across every channel direction. */
      totalBufferBytes: number;
    };
  }

  interface I2SCapabilities {
    ports: number[];
    standard: true;
    standardRx: true;
    standardTx: true;
    standardDuplex: true;
    pdm: boolean;
    dataBits: I2SDataBits[];
    limits: {
      maxDescriptorBytes: 4092;
      maxReadBytes: 65536;
      maxWriteBytes: 65536;
    };
  }

  interface I2SWriteResult {
    frames: number;
    byteLength: number;
    timestampUs: number;
  }

  /** Explicitly started standard I2S or receive-only PDM channel. */
  class I2SChannel {
    private constructor();
    start(): boolean;
    stop(): boolean;
    read(frameCount: number, timeoutMs?: number): I2SReadResult | null;
    write(data: ByteSource | ByteSpanSource, timeoutMs?: number): I2SWriteResult;
    status(): I2SStatus;
    close(): boolean;
  }

  interface I2SModule {
    capabilities(): I2SCapabilities;
    open(options: I2SOpenOptions): I2SChannel;
  }

  type CameraPixelFormat = "jpeg" | "grayscale" | "rgb565";
  type CameraFrameSize =
    | "96x96"
    | "qqvga"
    | "128x128"
    | "qcif"
    | "hqvga"
    | "qvga"
    | "cif"
    | "vga"
    | "svga"
    | "xga"
    | "sxga"
    | "uxga";
  type CameraGrabMode = "whenEmpty" | "latest";
  type CameraBufferLocation = "psram" | "dram";
  type CameraSensorModel = "ov2640" | "ov3660" | "ov5640";

  interface CameraPins {
    pwdn?: number;
    reset?: number;
    xclk?: number;
    sccbSda?: number;
    sccbScl?: number;
    d0?: number;
    d1?: number;
    d2?: number;
    d3?: number;
    d4?: number;
    d5?: number;
    d6?: number;
    d7?: number;
    vsync?: number;
    href?: number;
    pclk?: number;
  }

  interface CameraOpenOptions {
    pixelFormat?: CameraPixelFormat;
    frameSize?: CameraFrameSize;
    jpegQuality?: number;
    frameBuffers?: 1 | 2;
    grabMode?: CameraGrabMode;
    bufferLocation?: CameraBufferLocation;
    /** Enables direct camera DMA into PSRAM instead of internal DMA staging. */
    psramDma?: boolean;
    xclkFreqHz?: number;
    timeoutMs?: number;
    /** Overrides selected hardware constants; the resolved map must be complete. */
    pins?: CameraPins;
  }

  interface CameraCapabilities {
    target: string;
    psram: boolean;
    psramBytes: number;
    sensorDrivers: CameraSensorModel[];
    pixelFormats: CameraPixelFormat[];
    frameSizes: CameraFrameSize[];
  }

  interface CameraStatus {
    opened: true;
    capturePending: boolean;
    frameLeased: boolean;
    pixelFormat: CameraPixelFormat;
    frameSize: CameraFrameSize;
    jpegQuality: number;
    frameBuffers: 1 | 2;
    grabMode: CameraGrabMode;
    bufferLocation: CameraBufferLocation;
    psramDma: boolean;
    sensor: {
      model: CameraSensorModel;
      pid: number;
    };
  }

  interface CameraControls {
    frameSize: CameraFrameSize;
    jpegQuality: number;
    brightness: number;
    contrast: number;
    saturation: number;
    horizontalMirror: boolean;
    verticalFlip: boolean;
  }

  interface CameraFrameSourceOptions {
    /** Span size in bytes, from 1 through 32768. */
    chunkBytes?: number;
  }

  /** One leased camera framebuffer. Close it explicitly unless its source consumes it. */
  class CameraFrame {
    private constructor();
    readonly width: number;
    readonly height: number;
    readonly format: CameraPixelFormat;
    readonly byteLength: number;
    readonly timestampUs: number;
    readonly sequence: number;
    source(options?: CameraFrameSourceOptions): ByteSpanSource;
    read(offset?: number, limit?: number): ByteView;
    close(): boolean;
  }

  /** Singleton camera driver handle. */
  class Camera {
    private constructor();
    capture(timeoutMs?: number): CameraFrame | null;
    status(): CameraStatus;
    controls(): CameraControls;
    setControl(name: "frameSize", value: CameraFrameSize): CameraControls;
    setControl(name: "jpegQuality" | "brightness" | "contrast" | "saturation", value: number): CameraControls;
    setControl(name: "horizontalMirror" | "verticalFlip", value: boolean): CameraControls;
    close(): boolean;
  }

  interface CameraModule {
    capabilities(): CameraCapabilities;
    open(options?: CameraOpenOptions): Camera;
  }

  type SocketProtocol = "tcp" | "udp";

  interface SocketOpenOptions {
    localPort?: number;
    /** Secure outbound TCP using the system CA certificate bundle. */
    tls?: boolean;
  }

  interface TCPListenOptions {
    localPort: number;
    backlog?: number;
  }

  interface TCPConnectOptions {
    timeoutMs?: number;
  }

  interface SocketStatus {
    protocol: SocketProtocol;
    secure: boolean;
    connected: boolean;
    listening: boolean;
    peerClosed: boolean;
    localIp: string;
    localPort: number;
    remoteHost: string;
    remotePort: number;
    sentBytes: number;
    receivedBytes: number;
  }

  interface TCPSocket {
    connect(
      remoteHost: string,
      remotePort: number,
      options?: TCPConnectOptions,
    ): boolean;
    send(data: ByteSource | ByteSpanSource, timeoutMs?: number): number;
    /** Receive one currently available TCP stream chunk, not a framed message. */
    recv(maxBytes?: number, timeoutMs?: number): ByteView | null;
    status(): SocketStatus;
    close(): boolean;
  }

  interface TCPListener {
    accept(timeoutMs?: number): TCPSocket | null;
    status(): SocketStatus;
    close(): boolean;
  }

  interface SocketUdpDatagram {
    data: ByteView;
    remoteHost: string;
    remotePort: number;
  }

  interface UDPSocket {
    sendTo(
      remoteHost: string,
      remotePort: number,
      data: ByteSource,
    ): number;
    receiveFrom(
      maxBytes?: number,
      timeoutMs?: number,
    ): SocketUdpDatagram | null;
    status(): SocketStatus;
    close(): boolean;
  }

  interface SocketModule {
    openTCP(options?: SocketOpenOptions): TCPSocket;
    listenTCP(options: TCPListenOptions): TCPListener;
    openUDP(options?: SocketOpenOptions): UDPSocket;
    readonly MAX_TRANSFER_BYTES: number;
  }

  interface USBSerialTextOptions {
    mode?: "text";
    maxFrameBytes?: number;
  }

  interface USBSerialBinaryOptions {
    mode: "binary";
    chunkBytes?: number;
  }

  interface USBSerialStatus {
    open: boolean;
    connected: boolean;
    maxFrameBytes: number;
    receivedFrames: number;
    sentFrames: number;
    overflowFrames: number;
    droppedFrames: number;
  }

  interface USBSerialCapabilities {
    readonly apiVersion: "v1";
    readonly target: string;
    readonly idfVersion: string;
    readonly supported: true;
    readonly maxFrameBytes: number;
    readonly modes: readonly ["text", "binary"];
  }

  interface USBSerialTextHandle extends EventQueue<string> {
    receive(timeoutMs?: number): string | null;
    stats(): EventQueueStats;
    send(text: string): number;
    status(): USBSerialStatus;
    close(): boolean;
  }

  interface USBSerialBinaryHandle extends EventQueue<ByteView> {
    receive(timeoutMs?: number): ByteView | null;
    stats(): EventQueueStats;
    send(data: ByteSource | ByteSpanSource): number;
    status(): USBSerialStatus;
    close(): boolean;
  }

  /** Headless USB Serial/JTAG NDJSON transport; mutually exclusive with the REPL. */
  interface USBSerialModule {
    capabilities(): USBSerialCapabilities;
    open(options?: USBSerialTextOptions): USBSerialTextHandle;
    open(options: USBSerialBinaryOptions): USBSerialBinaryHandle;
  }

  interface WebSocketClientOpenOptions {
    url: string;
    authorization?: string;
    subprotocol?: string;
    networkTimeoutMs?: number;
    sendTimeoutMs?: number;
    pingIntervalSec?: number;
    maxMessageBytes?: number;
  }

  type WebSocketClientEvent =
    | { type: "open"; sequence: number; timestampUs: number }
    | {
        type: "message";
        sequence: number;
        timestampUs: number;
        data: string | ByteView;
      }
    | {
        type: "close" | "error";
        sequence: number;
        timestampUs: number;
        code: number;
        message: string;
      };

  interface WebSocketClientStatus {
    open: boolean;
    connected: boolean;
    closing: boolean;
    maxMessageBytes: number;
    openedEvents: number;
    receivedMessages: number;
    sentMessages: number;
    droppedEvents: number;
    oversizedMessages: number;
    queueDroppedEvents: number;
  }

  interface WebSocketClientCapabilities {
    readonly apiVersion: "v1";
    readonly target: string;
    readonly idfVersion: string;
    readonly supported: true;
    readonly maxMessageBytes: number;
    readonly nativeReconnect: false;
  }

  interface WebSocketClientHandle extends EventQueue<WebSocketClientEvent> {
    receive(timeoutMs?: number): WebSocketClientEvent | null;
    stats(): EventQueueStats;
    send(data: string | ByteSource | ByteSpanSource): number;
    status(): WebSocketClientStatus;
    close(): boolean;
  }

  /** Singleton outbound WebSocket text/binary client. */
  interface WebSocketClientModule {
    capabilities(): WebSocketClientCapabilities;
    open(options: WebSocketClientOpenOptions): WebSocketClientHandle;
  }

  interface NetIPv4Status {
    address: string;
    netmask: string;
    gateway: string;
  }

  interface NetInterfaceStatus {
    /** Stable esp-netif configuration key while the interface exists. */
    key: string;
    description: string;
    /** Underlying TCP/IP implementation name, such as an lwIP interface name. */
    name: string;
    up: boolean;
    /** Up with at least one IPv4 or preferred IPv6 address. */
    ready: boolean;
    defaultRoute: boolean;
    routePriority: number;
    ipv4: NetIPv4Status | null;
    ipv6: string[];
  }

  interface NetStatus {
    /** At least one interface is ready; this is not an Internet reachability probe. */
    ready: boolean;
    primaryInterface: string | null;
    interfaces: NetInterfaceStatus[];
    /** More interfaces existed than the configured native snapshot limit. */
    truncated: boolean;
  }

  interface NetStatusEvent {
    type: "status";
    /** Current convergent snapshot, not a raw driver event. */
    status: NetStatus;
  }

  interface NetModule {
    status(): NetStatus;
    /** Emits one initial snapshot and later IP/default-route changes. */
    watch(): EventQueue<NetStatusEvent>;
  }

  /**
   * Current Wi-Fi station status.
   */
  type WiFiRadioMode = "off" | "station" | "softAP" | "station+softAP";
  type WiFiStorage = "ram" | "flash";
  type WiFiPowerSaveMode = "none" | "minimum" | "maximum";

  type WiFiRadioDriverState = "uninitialized" | "initializing" | "stopped" |
    "starting" | "started" | "stopping" | "faulted" | "cleanup-pending";

  /** Latest native config transaction, including failures; no credential fields. */
  interface WiFiConfigurationStatus {
    stage: string;
    error: number;
    mutationAttempted: boolean;
    rollbackAttempted: boolean;
    /** Verified runtime config restoration only; does not prove NVS restoration. */
    rollbackComplete: boolean;
    rollbackStage: string | null;
    rollbackError: number;
    persistentMutationPossible: boolean;
  }

  type WiFiPromiscuousDriverStage = "promiscuous-stop" | "promiscuous-control-filter" |
    "promiscuous-filter" | "promiscuous-callback" | "promiscuous-enable" |
    "promiscuous-control-readback" | "promiscuous-filter-readback" | "promiscuous-enable-readback" |
    "promiscuous-restore-control" | "promiscuous-restore-filter" | "promiscuous-restore-callback" |
    "promiscuous-restore-enable" | "promiscuous-snapshot-enable" | "promiscuous-snapshot-filter" |
    "promiscuous-snapshot-control" | "promiscuous-activate" | "promiscuous-registry";

  /** Framework-accepted writes, not SDK readback. Zero selects SDK default mode. */
  interface WiFiConnectionlessIntervalStatus {
    generation: number;
    revision: number;
    known: boolean;
    uncertain: boolean;
    milliseconds: number | null;
    previousMilliseconds: number | null;
    ownerIdentity: number;
    tokenIdentity: number;
    restorePending: boolean;
    error: number;
    restoreError: number;
  }
  /** Framework write history; no SDK getter or replay authorization. */
  interface WiFiPolicyRecord {
    /** Driver generation of the last attempt; 0 means never attempted. */
    generation: number;
    revision: number;
    acceptedRevision: number;
    /** At least one accepted write exists, possibly for an old driver generation. */
    configured: boolean;
    known: boolean;
    uncertain: boolean;
    requested: boolean | null;
    /** Null after a failed write or physical deinit, and before the first write. */
    value: boolean | null;
    lastAcceptedValue: boolean | null;
    error: number;
  }
  interface WiFiPolicyStatus {
    revision: number;
    identityExhausted: boolean;
    dynamicCarrierSense: WiFiPolicyRecord;
    station11bDisabled: WiFiPolicyRecord;
    accessPoint11bDisabled: WiFiPolicyRecord;
    coexistencePowerManagement: WiFiPolicyRecord;
    /** Present only on HE-capable builds; write acceptance, not a native getter. */
    bssColorCollisionReporting?: WiFiPolicyRecord;
  }
  /** Last explicit request, without SDK threshold/armed-state readback or event correlation. */
  interface WiFiRssiThresholdStatus {
    /** Boot-scoped attempt revision; zero before the first SDK write, never reused. */
    revision: number;
    identityExhausted: boolean;
    generation: number | null;
    /** Request belongs to the currently owned physical generation; does not mean armed. */
    generationActive: boolean;
    requestedDbm: number | null;
    /** Last SDK write returned ESP_OK; preserved as history after physical deinit. */
    accepted: boolean;
    espCode: number | null;
    espName: string | null;
  }
  /** AP configuration installed before native AP allocation; configuration retains the original error and rollback result. */
  type WiFiAPPrestartStage = "ap-prestart-context" | "ap-prestart-config" | "ap-prestart-create" |
    "ap-prestart-mode" | "ap-prestart-release" | "ap-prestart-rollback";
  /** Native NAN lifecycle diagnostics; does not expose a NAN service API. */
  type WiFiNanRadioStage = "nan-admission" | "nan-radio-initialize" | "nan-mode-snapshot" |
    "nan-observer" | "nan-tx-prepare" | "nan-netif-initialize" | "nan-netif-allocate" | "nan-netif-attach" |
    "nan-netif-handlers" | "nan-storage" | "nan-prepare" | "nan-start-events" | "nan-start" |
    "nan-native-ready" | "nan-netif-ready" | "nan-stop-events" | "nan-stop" |
    "nan-service-callbacks" | "nan-tx-retire" | "nan-native-reset" | "nan-netif-retire" | "nan-observer-retire" | "nan-mode-restore" | "nan-storage-restore";
  interface WiFiRadioStatus {
    /** Native credential checkpoint retained by an internal restart, never its contents. */
    restartSnapshotBytes: number;
    policies: WiFiPolicyStatus;
    rssiThreshold: WiFiRssiThresholdStatus;
    connectionlessInterval: WiFiConnectionlessIntervalStatus;
    configuration: WiFiConfigurationStatus | null;
    /** Last post-start TX-power attempt; rollback covers power only, not startup. */
    activation: WiFiConfigurationStatus | null;
    generation: number;
    driverState: WiFiRadioDriverState;
    requestedMode: WiFiRadioMode | "nan";
    channelObservationError: number | null;
    activeOperations: number;
    /** Live native force-wakeup references; prevent stop/shutdown until released. */
    wakeLocks: number;
    /** Last native wake operation error, or null. */
    wakeLockError: number | null;
    lifecycleActive: boolean;
    eventPhase: "idle" | "start" | "stop" | "ap-stop" | "ap-start" | "restart" | "sta-start";
    eventIdentity: number;
    /** Interface bits: Station=1, AP=2, NAN=4. These are lifecycle observations. */
    eventExpectedMask: number;
    eventSeenMask: number;
    /** STOP evidence in the current stop or internal restart event phase. */
    eventStoppedMask: number;
    eventLiveMask: number;
    eventFencePending: boolean;
    fixedChannelOwners: number;
    /** Live enable claims, including claims retained by failed cleanup. */
    promiscuousOwners: number;
    /** Boot-scoped acquisition identities never wrap or reset on runtime restart. */
    promiscuousIdentityExhausted: boolean;
    conflictedChannelOwners: number;
    cleanupStage: WiFiAPPrestartStage | WiFiNanRadioStage | "action-retire" | "vendor-ie-unregister" | "vendor-ie-drain" | "ap-reopen-channel" | "ap-reopen-state" | "ap-reopen-events" | "ap-reopen-mode" | "ap-reopen-mode-readback" | "ap-reopen-inactive-restore" | "ap-stop-state" | "ap-stop-mode-snapshot" | "ap-stop-mode" | "ap-stop-events" | "ap-stop-mode-readback" | "stop" | "stop-events" | "deinit" | WiFiPromiscuousDriverStage | "channel-unregister" | "channel-drain" | "configuration-rollback" | "activation-rollback" | "mode-rollback" | null;
    cleanupError: number | null;
    driverOwned: boolean;
    restartRequired: boolean;
    faultStage: WiFiAPPrestartStage | WiFiNanRadioStage | "vendor-ie-register" | "vendor-ie-unregister" | "vendor-ie-drain" | "ap-reopen-channel" | "ap-reopen-state" | "ap-reopen-events" | "ap-reopen-mode" | "ap-reopen-mode-readback" | "ap-reopen-inactive-restore" | "ap-stop-state" | "ap-stop-mode-snapshot" | "ap-stop-mode" | "ap-stop-events" | "ap-stop-mode-readback" | "event-loop" | "channel-handler" | "lifecycle-handler" | "start-events" | "stop-events" | "nvs" | "init" | "storage" | "get-mode" | "mode" | "start" | "stop" | "deinit" | WiFiPromiscuousDriverStage | "generation-exhausted" | "channel-unregister" | "channel-drain" | "ap-config" | "ap-config-readback" | "resume-mode-readback" |
      "restore-defaults" | "restore-mode-readback" | "restore-storage" | "restart-enterprise-install" |
      "mode-write" | "mode-readback" | "storage-write" | "pmf-write" | "pmf-readback" |
      "config-admission" | "config-allocate" | "ap-regulatory" | "mode-snapshot" | "station-snapshot" | "ap-snapshot" |
      "config-storage" | "config-mode" | "config-mode-readback" | "station-config" | "station-config-readback" | "ap-pmf-security" | "ap-pmf-config" | "station-pmf-security" | "station-pmf-config" |
      "country-config" | "country-config-readback" | "station-phy-config" | "station-phy-readback" |
      "ap-phy-config" | "ap-phy-readback" | "power-save-config" | "power-save-readback" |
      "tx-power-snapshot" | "tx-power-config" | "tx-power-readback" | null;
    faultError: number | null;
    initialized: boolean;
    starting: boolean;
    started: boolean;
    /** Framework mode; after a failed transition inspect fault/configuration details before relying on it. */
    mode: WiFiRadioMode | "nan";
    /** Accepted framework storage choice; null before initialization or after an uncertain storage write. */
    storage: WiFiStorage | null;
    channel: number | null;
    channelGeneration: number;
    maxTxPowerDbm: number | null;
    powerSave: WiFiPowerSaveMode | null;
    clients: {
      total: number;
      application: number;
      wifiAccessPoint: number;
      wifiStation: number;
      espNow: number;
      wifiCsi: number;
      wifiMonitor: number;
      wifiRawTx: number;
      wifiVendorIe: number;
      wifiAction: number;
      /** Present with either the native NAN-Sync or NAN-USD lifecycle binding. */
      wifiNan?: number;
      wifiMesh?: number;
      /** Present only with the FTM initiator binding. */
      wifiFtm?: number;
      /** Present only with the C5 HE TWT binding; includes retiring probes. */
      wifiTwt?: number;
    };
    rawTx: WiFiRawTxNativeStatus;
    action: WiFiActionStatus;
  }

  type WiFiNegotiatedPhy = "lr" | "11b" | "11g" | "11a" | "ht20" | "ht40" | "he20" | "vht20";

  /** Per-operation identity captured at association and accepted at IP readiness. */
  interface WiFiConnectResult {
    connected: true;
    /** UTF-8 text, or null for invalid UTF-8. */
    ssid: string | null;
    /** Exact bytes of the native SSID span; never decoded through text. */
    ssidBytes: number[];
    bssid: string;
    /** Channel from the association event. */
    channel: number;
    aid: number | null;
    /** Native dispatch through completion publication, excluding Future delivery delay. */
    elapsedMs: number;
    /** Advisory last-beacon reading at result delivery; null if unavailable. */
    rssi: number | null;
    /** SDK negotiated mode at delivery, not the AP's supported PHY flags. */
    negotiatedPhy: WiFiNegotiatedPhy | null;
  }

  interface WiFiAccessPointStatus {
    /** Last received native AP_START/AP_STOP state, not DHCP or client readiness. */
    started: boolean;
    cleanupPending: boolean;
    cleanupStage: string | null;
    queryError: number | null;
    queryStage: "admission" | "mode" | "allocate" | "config" | "mac" | "clients" | "channel" | "state" | null;
    /** UTF-8 text, or null for invalid UTF-8 or failed sampling; ssidBytes preserves exact binary SSIDs. */
    ssid: string | null;
    ssidBytes: number[] | null;
    hidden: boolean | null;
    /** Current SDK channel; may differ from the requested AP channel in APSTA. */
    channel: number | null;
    authMode: WiFiAPAuthMode | "unknown" | null;
    maxConnections: number | null;
    /** Association count observed during this query; not an atomic set with apClients(). */
    clientCount: number | null;
    mac: string | null;
  }

  interface WiFiMonitorDiagnosticSession {
    generation: number;
    closed: boolean;
    closeRequested: boolean;
    retirementBlocked: boolean;
    accepting: boolean;
    identityExhausted: boolean;
    /** Native Session struct only; excludes queue, allocator overhead and JS. */
    controlBytes: number;
    /** Slots and payload storage still owned by this pool at observation. */
    poolBytes: number;
    freeSlots: number;
    leasedFrames: number;
    publishers: number;
    callbacks: number;
    accepted: number;
    droppedPoolFull: number;
    droppedQueueFull: number;
    droppedClosing: number;
  }
  interface WiFiMonitorDiagnostics {
    /** All registered generations, including closed retained payload owners. */
    sessions: WiFiMonitorDiagnosticSession[];
    /** Registered controls that could not take a temporary observation reference. */
    unavailableSessions: number;
    capacity: number;
  }
  interface WiFiCsiDiagnosticGeneration {
    generation: number;
    state: "allocating" | "active" | "retained" | "retiring";
    capacity: number;
    /** Reserved resource control, slot metadata and sample bytes; held until free returns. */
    storageBytes: number;
    /** Null during allocation/free, when the native resource is unavailable to readers. */
    freeSlots: number | null;
    identityExhausted: boolean | null;
    droppedIdentityExhausted: number | null;
    leasedFrames: number | null;
    callbacks: number | null;
    accepted: number | null;
    droppedPoolFull: number | null;
    droppedQueueFull: number | null;
    droppedClosing: number | null;
  }
  interface WiFiCsiDiagnostics {
    /** Current or last driver control generation; data generations are listed separately. */
    generation: number;
    state: "closed" | "running" | "stopping" | "stopped" | "faulted";
    /** Boot-owned Session, registry and registry lock; excludes dynamic storage and queues. */
    controlBytes: number;
    slotBudget: number;
    generationBudget: number;
    reservedSlots: number;
    storageBytes: number;
    activeStorageBytes: number;
    retainedStorageBytes: number;
    pendingStorageBytes: number;
    identityExhausted: boolean;
    generations: WiFiCsiDiagnosticGeneration[];
    cleanupScheduled: boolean;
    closeRequested: boolean;
  }
  interface WiFiDiagnosticsSnapshot {
    apiVersion: "wifi-diagnostics/1";
    /** Boot-relative esp_timer microseconds bracketing independent observations. */
    startedUs: number;
    finishedUs: number;
    /** Last explicit reset, retained across runtime restarts. */
    counterReset: WiFiDiagnosticsCounterReset;
    /** Runtime-wide EventQueue counters, not just Wi-Fi queues. */
    runtimeQueues: SysEventQueueResourceStatus;
    /** Global managed-memory ledger; direct SDK/heap allocations are not all tracked here. */
    memory: SysMemoryManagerStatus;
    /** Includes scan/drain/AP/watch/Radio leases, Raw TX and Action diagnostics. */
    wifi: WiFiStatus;
    monitor: WiFiMonitorDiagnostics;
    vendorIe: WiFiVendorIeStatus;
    /** Null means the corresponding diagnostic provider is not in this build. */
    csi: WiFiCsiDiagnostics | null;
    ftmInitiator: WiFiFtmGlobalStatus | null;
    ftmResponder: WiFiFtmResponderOffsetStatus | null;
    twt: WiFiTwtStatus | null;
    enterprise: WiFiEnterpriseStatus | null;
    smartConfig: WiFiSmartConfigGlobalStatus | null;
    wpsStation: WiFiWpsGlobalStatus | null;
    wpsAccessPoint: WiFiWpsAPGlobalStatus | null;
    dpp: WiFiDppGlobalStatus | null;
    nan: WiFiNanGlobalStatus | null;
  }
  interface WiFiDiagnosticsCounterReset {
    /** Saturating boot-lifetime count of explicit reset calls. */
    count: number;
    startedUs: number | null;
    finishedUs: number | null;
    unavailableMonitorGenerations: number;
    unavailableCsiGenerations: number;
  }
  /** Counts of reviewed C symbols, not JS methods or hardware qualification.
   * Dispositions, implementation states and contract states each sum to symbols. */
  interface WiFiIdfApiCoverageCounts {
    symbols: number;
    functionSymbols: number;
    /** Symbols whose jsPath exists in the all-features API manifest. */
    registeredSymbols: number;
    mapped: number;
    frameworkOwned: number;
    buildTime: number;
    removedOrDeprecated: number;
    privateExcluded: number;
    targetUnsupported: number;
    planned: number;
    inProgress: number;
    implemented: number;
    reviewRequired: number;
    contractPending: number;
    reviewed: number;
  }
  interface WiFiIdfApiCoverageGroup {
    name: string;
    counts: WiFiIdfApiCoverageCounts;
  }
  interface WiFiIdfApiCoverage {
    apiVersion: "wifi-idf-coverage/1";
    scope: "reviewed-inventory";
    target: "esp32c3" | "esp32s3" | "esp32c5";
    idfVersion: string;
    reviewedIdfRevision: string;
    inventorySha256: string;
    mapSha256: string;
    manifestSha256: string;
    total: WiFiIdfApiCoverageCounts;
    headers: WiFiIdfApiCoverageGroup[];
    tasks: WiFiIdfApiCoverageGroup[];
    /** Recorded configurations, not a claim that this firmware uses one of them. */
    referenceVariants: WiFiIdfApiCoverageGroup[];
    unexpandedHeaders: { header: string; reason: string }[];
    /** Same current target/build and live country semantics as wifi.capabilities(). */
    capabilities: WiFiCapabilities;
  }
  interface WiFiDiagnosticsModule {
    /** Copies existing native ledgers without starting Radio, draining queues,
     * resetting counters or retaining JS/native owners in the result. Not atomic
     * across modules; normal GC during conversion may retire unreachable owners. */
    snapshot(): WiFiDiagnosticsSnapshot;
    /** SDK statistics log dump. 0..31 combines BUFFER=1, RXTX=2, HW=4,
     * DIAG=8 and PS=16; omitted/undefined, -1 or 4294967295 selects SDK ALL.
     * Returns true on SDK success, otherwise throws WIFI_DIAGNOSTICS_FAILED.
     * Requires stable initialized Radio; does not start it or reset counters. */
    dumpDriverStats(mask?: number): boolean;
    /** Generated reviewed-inventory counts with current capabilities separately.
     * Does not start Radio or infer runtime/RF validation from registration. */
    idfApiCoverage(): WiFiIdfApiCoverage;
    /** Reset registered runtime EventQueue drops/peaks, watch ingress history,
     * framework connection counters, readable Monitor/CSI observation histories,
     * global memory-manager history and shared wireless peaks/denials.
     * Peaks restart at current usage; native ownership, identities, filter
     * scheduling, operation progress, fault state and SDK statistics stay intact.
     * Independent per-provider resets; producers may increment during this call.
     * counterReset reports unavailable generations. No JS allocation on success. */
    resetFrameworkCounters(): void;
  }

  interface WiFiStatus {
    connectionCounters: WiFiConnectionCounters;
    /** Last helper setup failure, retained through successful unwind. */
    setupStage: "runtime-resources" | "radio-acquire" | "net-init" | "netif-create" |
      "netif-attach" | "netif-handlers" | "event-handlers" | "timer-create" | null;
    setupError: number | null;
    stationNetifCleanupError: number | null;
    /** Native detach already destroyed its driver; retry needs device reboot. */
    stationNetifRestartRequired: boolean;
    accessPointNetifCleanupError: number | null;
    accessPointNetifRestartRequired: boolean;
    /** Null when the AP helper has no live or pending resources, or SoftAP is not compiled. */
    accessPoint: WiFiAccessPointStatus | null;
    /** Association can precede IP-ready connected. */
    associated: boolean;
    bssid: string | null;
    /** Last association channel; use radio.channel for the current observed channel. */
    channel: number | null;
    aid: number | null;
    rssi: number | null;
    negotiatedPhy: WiFiNegotiatedPhy | null;
    watch: WiFiWatchStatus;
    cleanupStage: "configuration-action-shutdown" | "recovery-disconnect" | "recovery-checkpoint" | "recovery-stop" |
      "recovery-ap-retire" | "recovery-station-retire" | "recovery-native-drain" | "recovery-restore-handoff" | "recovery-phase" |
      "configuration-vendor-ie-clear" | "vendor-ie-clear" | "callbacks-drain" | "timer-stop" | "timer-delete" | "control-unregister" | "ip-unregister" |
      "scan-unregister" | "disconnect-unregister" | "connected-unregister" | "start-unregister" |
      "scan-drain" | "connection-drain" | "radio-release" | "radio-stop" | "netif-detach" |
      "start-ap-allocate" | "start-ap-config" | "ap-stop-admission" | "configuration-admission" | "configuration-disconnect" | "configuration-stop" | "configuration-ap-retire" | "configuration-shutdown" |
      "configuration-initialize" | "configuration-station-prepare" | "configuration-ap-prepare" |
      "configuration-ap-slot" | "configuration-commit" | "configuration-resume" | "configuration-finish-stop" |
      "restart-checkpoint" | "restart-stopped-ap-retire" | "restart-stopped-station-retire" |
      "restart-stopped-station-prepare" | "restart-ap-retire" | "restart-station-retire" | "restart-rebuild" |
      "restart-station-prepare" | "restart-ap-prepare" | "restart-replay" | "restart-temporary-station-retire" |
      "restart-ap-config" | "restart-ap-slot" | "restart-resume" |
      "configuration-enterprise-clear" | "configuration-enterprise-ap-release" |
      "restart-enterprise-admission" | "restart-enterprise-checkpoint" | "restart-enterprise-retry" |
      "restart-enterprise-ap-retire" | "restart-enterprise-station-retire" | "restart-enterprise-resume" | null;
    cleanupError: number | null;
    initialized: boolean;
    started: boolean;
    connected: boolean;
    scanning: boolean;
    scanDraining: boolean;
    scanCleanupError: number | null;
    connectTimerError: number | null;
    connectDraining: boolean;
    disconnectCleanupError: number | null;
    /** UTF-8 text, or null for invalid UTF-8. */
    ssid: string | null;
    /** Exact bytes of the native SSID span; never decoded through text. */
    ssidBytes: number[];
    lastDisconnectReason: number;
    lastDisconnectReasonName: WiFiDisconnectReasonName;
    droppedDriverEvents: number;
    radio: WiFiRadioStatus;
  }

  /**
   * One access-point result from `wifi.scan()`.
   */
  type WiFiSecondaryChannel = "none" | "above" | "below";
  type WiFiBand = "2.4GHz" | "5GHz";
  type WiFiBandMode = "2.4GHz-only" | "5GHz-only" | "auto";
  type WiFiCipher = "none" | "wep40" | "wep104" | "tkip" | "ccmp" | "tkip-ccmp" |
    "aes-cmac128" | "sms4" | "gcmp" | "gcmp256" | "aes-gmac128" | "aes-gmac256";

  /** SDK country fields. An AP record describes that AP, not the effective local policy. */
  interface WiFiCountryStatus {
    code: string;
    /** X preserves the SDK's literal third country-code octet. */
    environment: "indoor" | "outdoor" | "X" | null;
    policy: "auto" | "manual" | null;
    startChannel: number;
    channelCount: number;
    maxTxPowerDbm: number;
    /** Raw SDK mask, null without 5 GHz support. Zero does not mean all channels allowed. */
    ghz5ChannelMask: number | null;
  }

  interface WiFiCountryOptions {
    /** Defaults to auto. Must agree with ieee80211d when both are supplied. */
    policy?: "auto" | "manual";
    ieee80211d?: boolean;
  }

  interface WiFiSetChannelOptions {
    /** Defaults to none. Above/below applies only to 2.4 GHz. */
    secondaryChannel?: WiFiSecondaryChannel;
  }

  interface WiFiChannelStatus {
    channel: number;
    secondaryChannel: WiFiSecondaryChannel | null;
    band: WiFiBand | null;
    channelGeneration: number;
  }

  interface WiFiControlError extends NativeError {
    code: "WIFI_COUNTRY_FAILED" | "WIFI_CHANNEL_FAILED" | "WIFI_MAC_SET_FAILED";
    operation: "wifi.setCountry" | "wifi.setChannel" | "wifi.setMac";
    details: {
      espCode: number;
      stage: "admission" | "country-set" | "country-readback" |
        "channel-validate" | "channel-set" | "channel-readback" |
        "mac-collision-check" | "mac-set" | "mac-readback" | null;
      /** The setter returned ESP_OK; later readback may still fail. */
      driverAccepted: boolean;
    };
  }

  interface WiFiScanRecord {
    /** UTF-8 text, or null for invalid UTF-8. */
    ssid: string | null;
    /** Exact bytes of the native SSID span; never decoded through text. */
    ssidBytes: number[];
    bssid: string;
    rssi: number;
    channel: number;
    authMode: WiFiAuthMode;
    hidden: boolean;
    secondaryChannel: WiFiSecondaryChannel | null;
    band: WiFiBand | null;
    pairwiseCipher: WiFiCipher | null;
    groupCipher: WiFiCipher | null;
    antenna: 0 | 1 | null;
    /** SDK-reported PHY flags; not a negotiated rate. */
    protocols: WiFiProtocol[];
    country: WiFiCountryStatus | null;
    /** Flags reported by this scan; false is not independent proof of lack of support. */
    capabilities: { wps: boolean; ftmResponder: boolean; ftmInitiator: boolean; he: boolean; vht: boolean };
  }

  interface WiFiScanResult {
    records: WiFiScanRecord[];
    /** The scan completed normally; maxRecords can still cap this list. */
    complete: boolean;
    /** The operation deadline stopped the scan, or expired before it started. */
    timedOut: boolean;
  }

  interface WiFiScanOptions {
    /** Match one SSID: 1..32 UTF-8 bytes, without NUL. */
    ssid?: string;
    /** Match one nonzero unicast BSSID in xx:xx:xx:xx:xx:xx form. */
    bssid?: string;
    /** Limit the scan to one target-supported channel, subject to SDK regulatory rules. Omit or use all for all channels. */
    channel?: "all" | number;
    /** Explicit channel filters; omitted bands are skipped. Mutually exclusive with a numeric channel.
     * 5 GHz auto/zero-mask country policies delegate to the SDK; no country setter is required.
     * The SDK may scan only a permitted subset. */
    channels?: { ghz2?: number[]; ghz5?: number[] };
    /** Include access points that do not advertise an SSID. Defaults to true. */
    showHidden?: boolean;
    /** Active probes or passive listening; defaults to active. SDK regulatory rules may require passive scanning. */
    mode?: "active" | "passive";
    /** Active-mode minimum per channel: 0..1500 ms, default 0; at most activeMaxMs. */
    activeMinMs?: number;
    /** Active-mode maximum per channel: 1..1500 ms, default 120. */
    activeMaxMs?: number;
    /** Passive-mode time per channel: 1..1500 ms, default 360. */
    passiveMs?: number;
    /** Time on the home channel between scan channels: 30..150 ms, default 30. */
    homeChannelDwellMs?: number;
    /** Request the SDK's return-home-channel behavior under coexistence. Default false. */
    coexistenceBackgroundScan?: boolean;
    /** Maximum returned records: 1..32, default 32. Does not bound driver scan memory. */
    maxRecords?: number;
    /** Whole-operation deadline, 1..2147483647 ms. Returns partial records after native stop; stop/cleanup can add latency. */
    timeoutMs?: number;
  }

  type WiFiConnectAuthMode =
    | "open"
    | "wep"
    | "wpa"
    | "wpa2"
    | "wpa/wpa2"
    | "wpa3"
    | "wpa2/wpa3"
    | "wapi"
    | "owe";

  type WiFiSaePkMode = "automatic" | "only" | "disabled";

  interface WiFiConnectOptions {
    /** Additional Station fields. A defined field may appear only here or at
     * the top level; SSID is positional and timeoutMs stays at the top level. */
    driver?: Partial<Omit<WiFiStationDriverConfig, "ssid">>;
    password?: string;
    timeoutMs?: number;
    /** Lock association to one BSSID. */
    bssid?: string;
    /** Initial scan hint: 0 (none), 1..13, or a target-supported 5 GHz channel. */
    channel?: number;
    scanMethod?: "fast" | "all-channel";
    sortMethod?: "signal" | "security";
    minimumRssi?: number;
    minimumAuthMode?: WiFiConnectAuthMode;
    /** Integer 0..255 dB; prefer 5 GHz APs within this RSSI difference. Requires 5 GHz target support. */
    rssi5gAdjustment?: number;
    /** Disabled uses a stopped Station transaction before native connect; requires
     * disableWpa3CompatibleMode=true and rejects AP modes/foreign Radio owners. */
    pmf?: "disabled" | "optional" | "required";
    /** AP beacon intervals for maximum modem power saving; 0..65535, 0 means SDK default 3. */
    listenInterval?: number;
    /** 0..255 retries before trying the next AP; nonzero requires all-channel scanning. */
    failureRetryCount?: number;
    rmEnabled?: boolean;
    /** Cannot be combined with a BSSID or nonzero channel hint. */
    btmEnabled?: boolean;
    /** Also enables RM/BTM; explicit false for either dependency is rejected. */
    mboEnabled?: boolean;
    ftEnabled?: boolean;
    /** Enforces OWE authentication and required PMF; no password or weaker explicit threshold. */
    oweEnabled?: boolean;
    saePwe?: "hunting-and-pecking" | "hash-to-element" | "both";
    /** Secret, 1..32 UTF-8 bytes without NUL; requires H2E and a 1..63-byte password. */
    saeH2eIdentifier?: string;
    /** Native transition-disable handling; does not enforce a stronger initial auth mode. */
    transitionDisable?: boolean;
    /** Explicitly disable WPA3 RSN override compatibility; use minimumAuthMode to prevent WPA2 fallback. */
    disableWpa3CompatibleMode?: boolean;
    /** Only mode enforces WPA3, required PMF and H2E. Native handshake validates the PK password and peer. */
    saePkMode?: WiFiSaePkMode;
    /** Explicit HE controls require an HE-capable target, including explicit false values. */
    heDcmSet?: boolean;
    /** 0 unsupported, 1 BPSK, 2 QPSK, 3 16-QAM. Requires heDcmSet:true; omitted TX/RX defaults to 3. */
    heDcmMaxConstellationTx?: 0 | 1 | 2 | 3;
    heDcmMaxConstellationRx?: 0 | 1 | 2 | 3;
    /** Enable HE MCS 8/9; does not change configured protocol or band. */
    heMcs9Enabled?: boolean;
    heSuBeamformeeDisabled?: boolean;
    heTrigSuBeamformingFeedbackDisabled?: boolean;
    heTrigMuBeamformingPartialFeedbackDisabled?: boolean;
    heTrigCqiFeedbackDisabled?: boolean;
    /** Explicit VHT controls require a supported 5 GHz target in the pinned SDK. */
    vhtSuBeamformeeDisabled?: boolean;
    vhtMuBeamformeeDisabled?: boolean;
    vhtMcs8Enabled?: boolean;
  }

  interface WiFiConnectConfigError extends NativeError {
    code: "WIFI_CONNECT_UNSUPPORTED";
    operation: "wifi.connect" | "wifi.driver.setInterfaceConfig";
    details: { option: string; espCode: number; espName: string };
  }

  type TlsErrorCode =
    | "TLS_ALLOC_FAILED"
    | "TLS_TIME_INVALID"
    | "TLS_VERIFY_FAILED"
    | "TLS_HANDSHAKE_FAILED"
    | "TLS_TIMEOUT";

  interface TlsError extends NativeError {
    code: TlsErrorCode;
    operation: string;
    details: {
      operationError: number;
      espTlsError: number;
      mbedtlsError: number;
      verifyFlags: number;
    };
  }

  type WiFiAuthMode =
    | "open"
    | "wep"
    | "wpa"
    | "wpa2"
    | "wpa/wpa2"
    | "wpa2-enterprise"
    | "wpa3"
    | "wpa2/wpa3"
    | "wapi"
    | "owe"
    | "wpa3-ent-192"
    | "unknown";

  type WiFiDisconnectReasonName =
    | "none"
    | "beacon-timeout"
    | "no-ap-found"
    | "auth-fail"
    | "assoc-fail"
    | "handshake-timeout"
    | "connection-fail"
    | "no-ap-compatible-security"
    | "no-ap-authmode-threshold"
    | "no-ap-rssi-threshold"
    | "unknown";

  type WiFiErrorCode =
    | "WIFI_START_FAILED"
    | "WIFI_STOP_FAILED"
    | "WIFI_MAC_FAILED"
    | "WIFI_AP_UNSUPPORTED"
    | "WIFI_SCAN_BUSY"
    | "WIFI_SCAN_TIMEOUT"
    | "WIFI_SCAN_FAILED"
    | "WIFI_OPERATION_BUSY"
    | "WIFI_CONNECT_TIMEOUT"
    | "WIFI_CONNECT_FAILED"
    | "WIFI_DISCONNECT_FAILED"
    | "WIFI_CANCELLED";

  interface WiFiError extends NativeError {
    code: WiFiErrorCode;
    operation: "wifi" | "wifi.start" | "wifi.stop" | "wifi.getMac" |
      "wifi.startAP" | "wifi.apClients" | "wifi.scan" | "wifi.connect" | "wifi.disconnect" | "wifi.driver.setInterfaceConfig";
    details: {
      setupStage: WiFiStatus["setupStage"];
      cleanupStage: WiFiStatus["cleanupStage"];
      restartRequired: boolean;
      espCode: number;
      espName: string;
      ssid: string | null;
      disconnectReason: number | null;
      disconnectReasonName: WiFiDisconnectReasonName | null;
      scanStatus: number | null;
    };
  }

  /**
   * Wi-Fi station helpers.
   *
   * @example
   * ```js
   * print(JSON.stringify(wifi.status()));
   * var scan = Future.call(wifi.scan, wifi, []);
   * print(scan.wait(10000).length);
   * ```
   */
  type WiFiAPAuthMode = "open" | "wpa" | "wpa2" | "wpa/wpa2" | "wpa3" | "wpa2/wpa3" | "owe";
  type WiFiAPCipher = "tkip" | "ccmp" | "tkip/ccmp" | "gcmp" | "gcmp-256";

  interface WiFiAccessPointOptions {
    /** Default false. Explicitly authorize a STOP/configure/START transaction,
     * disconnecting Station and AP clients. Retains configured Station mode,
     * its saved credentials and storage; does not reconnect Station. */
    allowDisconnect?: boolean;
    /** Raw AP fields; keep SSID at the top level. Duplicate semantic fields are
     * rejected, including beaconIntervalMs plus driver.beaconIntervalTu. */
    driver?: Partial<Omit<WiFiAccessPointDriverConfig, "ssid">>;
    /** Text without NUL, or 1..32 source bytes (binary AP SSID may contain NUL). */
    ssid: string | ByteSource;
    password?: string;
    channel?: number;
    hidden?: boolean;
    authMode?: WiFiAPAuthMode;
    maxConnections?: number;
    /** Positive, at most 61440; rounded up to a 102.4 ms (100 TU) quantum. */
    beaconIntervalMs?: number;
    /** 1..10 beacon intervals; default 1. */
    dtimPeriod?: number;
    /** 1..255 beacon intervals before channel switching; default 3. */
    csaCount?: number;
    pairwiseCipher?: WiFiAPCipher;
    /** disabled is explicit and requires WPA2 or WPA/WPA2 without WPA3 compatible mode.
     * Pure WPA3/OWE requires required; open/pure WPA has no PMF setting. */
    pmf?: "disabled" | "optional" | "required";
    ftmResponder?: boolean;
    saePwe?: "hunting-and-pecking" | "hash-to-element" | "both";
    /** 0 disables GTK rekeying; otherwise 60..65535 seconds. */
    gtkRekeyIntervalSeconds?: number;
    /** WPA3, WPA2/WPA3 or explicitly enabled WPA3 compatible mode. */
    transitionDisable?: boolean;
    /** Requires pure WPA3, GCMP-256, required PMF and H2E; supplies those defaults when omitted. */
    saeExt?: boolean;
    /** Explicitly authorize the SDK's WPA2/CCMP base profile with SAE RSN override for compatible peers. */
    wpa3CompatibleMode?: boolean;
    /** 0 disables BSS idle; otherwise integer 10..65535 units of 1000 TU (1.024 seconds). */
    bssMaxIdlePeriod?: number;
    /** Requires a nonzero idle period and encrypted AP authentication. */
    bssMaxIdleProtectedKeepAlive?: boolean;
  }

  /** Non-secret configuration readback returned by a successful startAP. */
  interface WiFiAccessPointStartResult {
    started: true;
    /** UTF-8 text, or null for invalid UTF-8. */
    ssid: string | null;
    /** Exact bytes of the native SSID span; never decoded through text. */
    ssidBytes: number[];
    channel: number;
    hidden: boolean;
    authMode: WiFiAPAuthMode | "unknown";
    maxConnections: number;
    beaconIntervalMs: number;
    dtimPeriod: number;
    csaCount: number;
    pairwiseCipher: WiFiAPCipher | "unknown" | null;
    pmf: "disabled" | "optional" | "required" | null;
    saePwe: "hunting-and-pecking" | "hash-to-element" | "both" | null;
    ftmResponder: boolean;
    gtkRekeyIntervalSeconds: number;
    transitionDisable: boolean;
    saeExt: boolean;
    wpa3CompatibleMode: boolean;
    bssMaxIdlePeriod: number;
    bssMaxIdleProtectedKeepAlive: boolean;
  }

  type WiFiInterface = "station" | "access-point";
  type WiFiProtocol = "11b" | "11g" | "11n" | "11a" | "11ac" | "11ax" | "lr";

  interface WiFiAPClientsOptions {
    /** Include a later local DHCP IPv4 observation; default false. Requires DHCP server support. */
    includeIp?: boolean;
  }

  interface WiFiAPClient {
    /** Lowercase colon-separated MAC. */
    address: string;
    /** Later association observation; null if the client already left. Not an identity. */
    aid: number | null;
    /** Present only with includeIp:true; null when no local DHCP lease was found.
     * Not proof of reachability, current association, or a peer's static address. */
    ip?: string | null;
    /** SDK average RSSI in dBm at list collection. */
    rssi: number;
    /** SDK-reported PHY flags; not a negotiated rate. */
    phy: WiFiProtocol[];
  }

  interface WiFiWakeLock {
    readonly acquired: boolean;
    /** Idempotent; native failure retains the reference and throws WIFI_WAKE_LOCK_FAILED. */
    close(): void;
  }

  interface WiFiWakeLockError extends NativeError {
    code: "WIFI_WAKE_LOCK_FAILED";
    operation: "wifi.acquireWakeLock" | "WiFiWakeLock.close";
    details: WiFiError["details"];
  }

  type WiFiEventName =
    | "WIFI_EVENT_WIFI_READY"
    | "WIFI_EVENT_SCAN_DONE"
    | "WIFI_EVENT_STA_START"
    | "WIFI_EVENT_STA_STOP"
    | "WIFI_EVENT_STA_CONNECTED"
    | "WIFI_EVENT_STA_DISCONNECTED"
    | "WIFI_EVENT_STA_AUTHMODE_CHANGE"
    | "WIFI_EVENT_STA_WPS_ER_SUCCESS"
    | "WIFI_EVENT_STA_WPS_ER_FAILED"
    | "WIFI_EVENT_STA_WPS_ER_TIMEOUT"
    | "WIFI_EVENT_STA_WPS_ER_PIN"
    | "WIFI_EVENT_STA_WPS_ER_PBC_OVERLAP"
    | "WIFI_EVENT_AP_START"
    | "WIFI_EVENT_AP_STOP"
    | "WIFI_EVENT_AP_STACONNECTED"
    | "WIFI_EVENT_AP_STADISCONNECTED"
    | "WIFI_EVENT_AP_PROBEREQRECVED"
    | "WIFI_EVENT_FTM_REPORT"
    | "WIFI_EVENT_STA_BSS_RSSI_LOW"
    | "WIFI_EVENT_ACTION_TX_STATUS"
    | "WIFI_EVENT_ROC_DONE"
    | "WIFI_EVENT_STA_BEACON_TIMEOUT"
    | "WIFI_EVENT_CONNECTIONLESS_MODULE_WAKE_INTERVAL_START"
    | "WIFI_EVENT_AP_WPS_RG_SUCCESS"
    | "WIFI_EVENT_AP_WPS_RG_FAILED"
    | "WIFI_EVENT_AP_WPS_RG_TIMEOUT"
    | "WIFI_EVENT_AP_WPS_RG_PIN"
    | "WIFI_EVENT_AP_WPS_RG_PBC_OVERLAP"
    | "WIFI_EVENT_ITWT_SETUP"
    | "WIFI_EVENT_ITWT_TEARDOWN"
    | "WIFI_EVENT_ITWT_PROBE"
    | "WIFI_EVENT_ITWT_SUSPEND"
    | "WIFI_EVENT_TWT_WAKEUP"
    | "WIFI_EVENT_BTWT_SETUP"
    | "WIFI_EVENT_BTWT_TEARDOWN"
    | "WIFI_EVENT_NAN_SYNC_STARTED"
    | "WIFI_EVENT_NAN_SYNC_STOPPED"
    | "WIFI_EVENT_NAN_SVC_MATCH"
    | "WIFI_EVENT_NAN_REPLIED"
    | "WIFI_EVENT_NAN_RECEIVE"
    | "WIFI_EVENT_NDP_INDICATION"
    | "WIFI_EVENT_NDP_CONFIRM"
    | "WIFI_EVENT_NDP_TERMINATED"
    | "WIFI_EVENT_HOME_CHANNEL_CHANGE"
    | "WIFI_EVENT_STA_NEIGHBOR_REP"
    | "WIFI_EVENT_AP_WRONG_PASSWORD"
    | "WIFI_EVENT_STA_BEACON_OFFSET_UNSTABLE"
    | "WIFI_EVENT_DPP_URI_READY"
    | "WIFI_EVENT_DPP_CFG_RECVD"
    | "WIFI_EVENT_DPP_FAILED"
    | "WIFI_EVENT_NAN_PAIRING_INDICATION"
    | "WIFI_EVENT_NAN_PAIRING_CONFIRM"
    | "WIFI_EVENT_NAN_CLUSTER_JOIN";

  interface WiFiWatchOptions {
    /** Exact SDK event names; defaults to all, no duplicates or empty lists. */
    events?: WiFiEventName[] | "all";
    /** 1..64, default 16. At most 4 active subscriptions. */
    capacity?: number;
    overflow?: "drop-newest";
    /** Requests explicit raw availability; no raw layouts are exported yet. */
    includeRawEventData?: boolean;
  }

  interface WiFiWatchStatus {
    subscribers: number;
    ingressQueued: number;
    ingressCapacity: number;
    /** Ingress queue peak since boot/last framework counter reset. */
    ingressHighWater: number;
    /** Saturating since boot/last reset; separate from each EventQueue's dropped count. */
    ingressDropped: number;
    neighborSlotsUsed: number;
    neighborPoolBytes: number;
    /** Old closed queues/receivers must release the sole pool before reopening a neighbor watch. */
    neighborPoolRetired: boolean;
    sequenceExhausted: boolean;
  }

  interface WiFiConnectionCounters {
    /** Actual framework esp_wifi_connect submissions, including immediate failures. */
    attempts: number;
    /** Submissions after a valid framework-observed association earlier this boot.
     * Reset does not erase that marker; SDK RF retries are not counted. */
    reconnectAttempts: number;
    submissionFailures: number;
    /** Valid STA_CONNECTED events accepted by the framework helper, before IP readiness. */
    associations: number;
  }

  interface WiFiEventMetadata {
    /** Boot-scoped, exact JS integer; gaps can reflect filtering and drops. */
    sequence: number;
    /** esp_timer_get_time at framework callback capture, not RF/TSF time. */
    timestampUs: number;
    id: number;
    category: "station" | "access-point" | "scan" | "ftm" | "twt" | "wps" | "dpp" | "nan" | "radio" | "unknown";
    dataUnavailableReason: "not-reviewed" | "sensitive" | "unsupported-layout" | "capacity" | "too-large" | null;
    rawEventDataUnavailableReason?: "not-reviewed" | "sensitive";
  }

  interface WiFiNeighborReportEntry {
    bssid: string;
    bssidInformation: number;
    operatingClass: number;
    channel: number;
    phyTypeId: number;
    candidatePreference: number | null;
    /** Unknown extension contents are omitted. */
    skippedSubelements: number;
  }

  /** Exact native 64-bit word; no conversion to JS number or host time. */
  interface WiFiTwtTargetWakeTime { low: number; high: number; }

  interface WiFiIndividualTwtEventConfiguration {
    setupCommandId: number;
    trigger: boolean;
    flowTypeId: number;
    flowId: number;
    wakeIntervalExponent: number;
    wakeIntervalMantissa: number;
    /** Raw duration count; unit ID 0 is 256 us, 1 is 1024 us. */
    minimumWakeDuration: number;
    wakeDurationUnitId: number;
    twtId: number;
    timeoutMs: number;
  }

  interface WiFiBroadcastTwtEventConfiguration {
    setupCommandId: number;
    trigger: boolean;
    flowTypeId: number;
    broadcastId: number;
    wakeIntervalExponent: number;
    wakeIntervalMantissa: number;
    /** Raw duration count in 256 us units, as documented by the SDK. */
    minimumWakeDuration: number;
  }

  type WiFiEvent = WiFiEventMetadata & (
    | { name: "WIFI_EVENT_WIFI_READY" | "WIFI_EVENT_STA_START" | "WIFI_EVENT_STA_STOP" | "WIFI_EVENT_AP_START" | "WIFI_EVENT_AP_STOP" | "WIFI_EVENT_STA_BEACON_TIMEOUT" | "WIFI_EVENT_CONNECTIONLESS_MODULE_WAKE_INTERVAL_START"; data: Record<string, never> | null }
    | { name: "WIFI_EVENT_SCAN_DONE"; data: { status: number; number: number; scanId: number } | null }
    | { name: "WIFI_EVENT_STA_CONNECTED"; data: { ssid: string | null; ssidBytes: number[]; bssid: string; channel: number; authModeId: number; aid: number } | null }
    | { name: "WIFI_EVENT_STA_DISCONNECTED"; data: { ssid: string | null; ssidBytes: number[]; bssid: string; reason: number; rssi: number } | null }
    | { name: "WIFI_EVENT_STA_AUTHMODE_CHANGE"; data: { oldAuthModeId: number; newAuthModeId: number } | null }
    | { name: "WIFI_EVENT_STA_WPS_ER_SUCCESS" | "WIFI_EVENT_STA_WPS_ER_FAILED" | "WIFI_EVENT_STA_WPS_ER_TIMEOUT" | "WIFI_EVENT_STA_WPS_ER_PIN" | "WIFI_EVENT_STA_WPS_ER_PBC_OVERLAP" | "WIFI_EVENT_AP_WPS_RG_SUCCESS" | "WIFI_EVENT_AP_WPS_RG_FAILED" | "WIFI_EVENT_AP_WPS_RG_TIMEOUT" | "WIFI_EVENT_AP_WPS_RG_PIN" | "WIFI_EVENT_AP_WPS_RG_PBC_OVERLAP" | "WIFI_EVENT_NAN_SYNC_STARTED" | "WIFI_EVENT_NAN_SYNC_STOPPED" | "WIFI_EVENT_NAN_SVC_MATCH" | "WIFI_EVENT_NAN_REPLIED" | "WIFI_EVENT_NAN_RECEIVE" | "WIFI_EVENT_NDP_INDICATION" | "WIFI_EVENT_NDP_CONFIRM" | "WIFI_EVENT_NDP_TERMINATED" | "WIFI_EVENT_DPP_URI_READY" | "WIFI_EVENT_DPP_CFG_RECVD" | "WIFI_EVENT_DPP_FAILED" | "WIFI_EVENT_NAN_PAIRING_INDICATION" | "WIFI_EVENT_NAN_PAIRING_CONFIRM" | "WIFI_EVENT_NAN_CLUSTER_JOIN"; data: null }
    | { name: "WIFI_EVENT_AP_STACONNECTED"; data: { address: string; aid: number; isMeshChild: boolean } | null }
    | { name: "WIFI_EVENT_AP_STADISCONNECTED"; data: { address: string; aid: number; isMeshChild: boolean; reason: number } | null }
    | { name: "WIFI_EVENT_AP_PROBEREQRECVED"; data: { address: string; rssi: number } | null }
    | { name: "WIFI_EVENT_STA_NEIGHBOR_REP"; data: { received: boolean; reportLength: number; dialogToken: number | null; neighbors: WiFiNeighborReportEntry[]; skippedElements: number } | null }
    /** Pinned SDK success is 1. Failure has no negotiated configuration/time. */
    | { name: "WIFI_EVENT_ITWT_SETUP"; data: { statusId: number; reason: number | null; configuration: WiFiIndividualTwtEventConfiguration | null; targetWakeTime: WiFiTwtTargetWakeTime | null } | null }
    /** reason is present only for BTWT_SETUP_TXFAIL. */
    | { name: "WIFI_EVENT_BTWT_SETUP"; data: { statusId: number; reason: number | null; configuration: WiFiBroadcastTwtEventConfiguration | null; targetWakeTime: WiFiTwtTargetWakeTime | null } | null }
    | { name: "WIFI_EVENT_ITWT_TEARDOWN"; data: { statusId: number; flowId: number } | null }
    | { name: "WIFI_EVENT_BTWT_TEARDOWN"; data: { statusId: number; broadcastId: number } | null }
    /** Failure reason; null for ITWT_PROBE_SUCCESS. */
    | { name: "WIFI_EVENT_ITWT_PROBE"; data: { statusId: number; reason: number | null } | null }
    /** Exactly eight flow-indexed slots; unselected bitmap slots are null. */
    | { name: "WIFI_EVENT_ITWT_SUSPEND"; data: { statusId: number; flowIdBitmap: number; actualSuspendTimeMs: (number | null)[] } | null }
    | { name: "WIFI_EVENT_TWT_WAKEUP"; data: { typeId: number; flowId: number } | null }
    /** Summary only; unsuccessful measurements are null. Does not consume the SDK report. */
    | { name: "WIFI_EVENT_FTM_REPORT"; data: { peerAddress: string; statusId: number; rttRawNs: number | null; rttEstimatedNs: number | null; distanceCm: number | null; reportEntries: number | null } | null }
    /** SDK may publish TX_DONE then DURATION_COMPLETED. Opaque native context is omitted. */
    | { name: "WIFI_EVENT_ACTION_TX_STATUS"; data: { interfaceId: number; statusId: number; operationId: number; channel: number } | null }
    | { name: "WIFI_EVENT_ROC_DONE"; data: { statusId: number; operationId: number; channel: number } | null }
    | { name: "WIFI_EVENT_AP_WRONG_PASSWORD"; data: { address: string } | null }
    /** Finite SDK value; no inferred percentage/fraction conversion. */
    | { name: "WIFI_EVENT_STA_BEACON_OFFSET_UNSTABLE"; data: { beaconSuccessRate: number } | null }
    | { name: "WIFI_EVENT_STA_BSS_RSSI_LOW"; data: { rssi: number } | null }
    | { name: "WIFI_EVENT_HOME_CHANNEL_CHANGE"; data: { oldChannel: number; oldSecondaryChannelId: number; newChannel: number; newSecondaryChannelId: number } | null }
    | { name: "WIFI_EVENT_UNKNOWN"; data: null }
  );

  interface WiFiWatchError extends NativeError {
    code: "WIFI_WATCH_FAILED";
    operation: "wifi.watch" | "wifi.roaming.watch";
    details: WiFiError["details"];
  }

  /** Implemented API discovery; feature flags are not RF qualification or current admission. */
  interface WiFiCapabilities {
    apiVersion: "wifi/1";
    target: string;
    idfVersion: string;
    stationOptions: {
      /** ByteSource input; embedded NUL is not representable in native Station config. */
      binarySsid: boolean;
      /** Nested Station driver fields are accepted with duplicate-field rejection. */
      driver: boolean;
      listenInterval: boolean;
      failureRetryCount: boolean;
      rssi5gAdjustment: boolean;
      /** Availability of all he-prefixed Station configuration options. */
      he: boolean;
      /** Availability of all vht-prefixed Station configuration options. */
      vht: boolean;
      transitionDisable: boolean;
      disableWpa3CompatibleMode: boolean;
      saePkModes: WiFiSaePkMode[];
      rmEnabled: boolean;
      btmEnabled: boolean;
      mboEnabled: boolean;
      ftEnabled: boolean;
      oweEnabled: boolean;
      saeH2eIdentifier: boolean;
      pmf: ("disabled" | "optional" | "required")[];
      saePwe: ("hunting-and-pecking" | "hash-to-element" | "both")[];
      minimumAuthModes: WiFiConnectAuthMode[];
    };
    accessPointOptions: {
      /** ByteSource input including embedded NUL. */
      binarySsid: boolean;
      driver: boolean;
      saeExt: boolean;
      wpa3CompatibleMode: boolean;
      /** Availability of period/protected-keepalive BSS idle settings. */
      bssMaxIdle: boolean;
      authModes: WiFiAPAuthMode[];
      pairwiseCiphers: WiFiAPCipher[];
      pmf: ("disabled" | "optional" | "required")[];
      saePwe: ("hunting-and-pecking" | "hash-to-element" | "both")[];
      ftmResponder: boolean;
      beaconIntervalQuantumMs: number;
      maximumBeaconIntervalMs: number;
      maximumDtimPeriod: number;
    };
    modes: WiFiRadioMode[];
    interfaces: WiFiInterface[];
    /** Physical bands; channels are a regulatory snapshot, not an AP mode promise. */
    bands: { band: "2.4GHz" | "5GHz"; channels: number[] | null }[];
    /** Native regulatory read error, or null; discovery never starts Wi-Fi. */
    countryError: number | null;
    /** Child namespaces, including twt on the reviewed C5 HE build. */
    namespaces: string[];
    features: {
      /** Callable Candidate restart; source-state admission still applies, not arbitrary fault recovery. */
      driverRestart: boolean;
      /** Vendor IE configuration and reception watch. */
      vendorIe: boolean;
      station: boolean;
      accessPoint: boolean;
      scan: boolean;
      wakeLock: boolean;
      setMac: boolean;
      csi: boolean;
      apsta: boolean;
      watch: boolean;
      configure: boolean;
      promiscuous: boolean;
      rawTx: boolean;
      actionTx: boolean;
      remainOnChannel: boolean;
      wapi: boolean;
      ftmInitiator: boolean;
      ftmResponder: boolean;
      individualTwt: boolean;
      broadcastTwt: boolean;
      rrm: boolean;
      wnm: boolean;
      smartConfig: boolean;
      nan: boolean;
      mesh: boolean;
      multipleAntennas: boolean;
    };
    limits: {
      maxSoftApClients: number;
      maxScanRecords: number;
      maxWakeLocks: number;
      maxWatchers: number;
      maxWatchCapacity: number;
      watchIngressCapacity: number;
      watchNeighborSlots: number;
      watchMaxNeighbors: number;
      watchMaxReportBytes: number;
    };
  }

  // BEGIN GENERATED WIFI DRIVER SNAPSHOTS
  /** SDK enum ID is preserved even when its name is unknown or reserved. */
  interface WiFiDriverEnumValue { id: number; name: string | null; }
  /** Independent driver observation; not a writable configuration or a capability declaration. */
  interface WiFiStationDriverConfigSnapshot {
    interface: "station";
    secretsIncluded: boolean;
    /** Null if SSID bytes are not valid UTF-8. */
    ssid: string | null;
    /** Null when redacted or when secret bytes are not valid UTF-8. */
    password: string | null;
    pmf: "disabled" | "optional" | "required" | null;
    bssid: string;
    saeH2eIdentifier: string | null;
    ssidBytes: number[];
    /** Null unless secret readback was explicitly enabled and requested. */
    passwordBytes: number[] | null;
    scanMethod: WiFiDriverEnumValue;
    bssidSet: boolean;
    bssidBytes: number[];
    channel: number;
    listenInterval: number;
    sortMethod: WiFiDriverEnumValue;
    minimumRssi: number;
    minimumAuthMode: WiFiDriverEnumValue;
    rssi5gAdjustment: number;
    pmfCapable: boolean;
    pmfRequired: boolean;
    rmEnabled: boolean;
    btmEnabled: boolean;
    mboEnabled: boolean;
    ftEnabled: boolean;
    oweEnabled: boolean;
    transitionDisable: boolean;
    disableWpa3CompatibleMode: boolean;
    saePwe: WiFiDriverEnumValue;
    saePkMode: WiFiDriverEnumValue;
    failureRetryCount: number;
    heDcmSet: boolean;
    heDcmMaxConstellationTx: number;
    heDcmMaxConstellationRx: number;
    heMcs9Enabled: boolean;
    heSuBeamformeeDisabled: boolean;
    heTrigSuBeamformingFeedbackDisabled: boolean;
    heTrigMuBeamformingPartialFeedbackDisabled: boolean;
    heTrigCqiFeedbackDisabled: boolean;
    vhtSuBeamformeeDisabled: boolean;
    vhtMuBeamformeeDisabled: boolean;
    vhtMcs8Enabled: boolean;
    /** Null unless secret readback was explicitly enabled and requested. */
    saeH2eIdentifierBytes: number[] | null;
  }
  /** Independent driver observation; not a writable configuration or a capability declaration. */
  interface WiFiAccessPointDriverConfigSnapshot {
    interface: "access-point";
    secretsIncluded: boolean;
    /** Null if SSID bytes are not valid UTF-8. */
    ssid: string | null;
    /** Null when redacted or when secret bytes are not valid UTF-8. */
    password: string | null;
    pmf: "disabled" | "optional" | "required" | null;
    ssidBytes: number[];
    /** Null unless secret readback was explicitly enabled and requested. */
    passwordBytes: number[] | null;
    ssidLength: number;
    channel: number;
    authMode: WiFiDriverEnumValue;
    hidden: boolean;
    maxConnections: number;
    beaconIntervalTu: number;
    csaCount: number;
    dtimPeriod: number;
    pairwiseCipher: WiFiDriverEnumValue;
    ftmResponder: boolean;
    pmfCapable: boolean;
    pmfRequired: boolean;
    saePwe: WiFiDriverEnumValue;
    transitionDisable: boolean;
    saeExt: boolean;
    wpa3CompatibleMode: boolean;
    bssMaxIdlePeriod: number;
    bssMaxIdleProtectedKeepAlive: boolean;
    gtkRekeyIntervalSeconds: number;
  }
  // END GENERATED WIFI DRIVER SNAPSHOTS

  interface WiFiStationDriverConfig {
    /** ssid; unit: bytes; Required 1..32 UTF-8 bytes or raw bytes without NUL, captured by value; STA has no separate SSID length field. */
    ssid: string | ByteSource;
    /** password; unit: UTF-8 bytes; No embedded NUL; empty only for a compatible auth mode; WPA2 8..63 bytes or 64 hex PSK, pure SAE 1..63 bytes. Validate against auth/PWE before any mutation. Secret: never status/error/log or default readback. */
    password?: string;
    /** scan_method; unit: enum; Default all-channel; nonzero failureRetryCount requires all-channel. */
    scanMethod?: "fast" | "all-channel";
    /** bssid; unit: MAC address; Optional six-octet unicast MAC; mutually exclusive with BTM/MBO roaming. */
    bssid?: string;
    /** channel; unit: 802.11 channel; Station scan hint: 0, 1..13, or a supported 5 GHz channel; does not connect or force the shared channel. */
    channel?: number;
    /** listen_interval; unit: AP beacon intervals; Integer 0..65535; 0 selects SDK default 3. Applies to maximum modem power save. */
    listenInterval?: number;
    /** sort_method; unit: enum; Default signal. */
    sortMethod?: "signal" | "security";
    /** threshold.rssi; unit: dBm; Integer -127..0, default -127. */
    minimumRssi?: number;
    /** threshold.authmode; unit: enum; Default open; enforce security dependencies; reserved/dummy/unknown/max enum values are not inputs. Advanced credential-owner APIs remain unimplemented; values outside WiFiConnectAuthMode are rejected. */
    minimumAuthMode?: WiFiConnectAuthMode;
    /** threshold.rssi_5g_adjustment; unit: dB; Integer 0..255, default 0; prioritizes 5 GHz APs within this RSSI difference. */
    rssi5gAdjustment?: number;
    /** Three-state PMF policy; disabled requires eligible authentication and runs the dedicated SDK operation before START.
     * Station also requires disableWpa3CompatibleMode=true. */
    pmf?: "disabled" | "optional" | "required";
    /** rm_enabled; unit: boolean; Default false; MBO implies true. */
    rmEnabled?: boolean;
    /** btm_enabled; unit: boolean; Default false; reject BSSID/nonzero channel; MBO implies true. */
    btmEnabled?: boolean;
    /** mbo_enabled; unit: boolean; Default false; enables RM/BTM; explicit false dependency or BSSID/nonzero channel conflicts. */
    mboEnabled?: boolean;
    /** ft_enabled; unit: boolean; Default false; driver FT enablement, not a roaming-policy implementation. */
    ftEnabled?: boolean;
    /** owe_enabled; unit: boolean; Default false; requires no password, OWE threshold and required PMF; reject weaker explicit settings. */
    oweEnabled?: boolean;
    /** transition_disable; unit: boolean; Boolean, default false; true enables native transition-disable handling, not a stronger initial authentication threshold; false accepted without the feature. */
    transitionDisable?: boolean;
    /** disable_wpa3_compatible_mode; unit: boolean; Boolean, default false; true disables RSN override and can select WPA2 unless threshold excludes it. Explicit security choice, never an automatic fallback; false accepted without feature. */
    disableWpa3CompatibleMode?: boolean;
    /** sae_pwe_h2e; unit: enum; Omitted uses SDK unspecified; explicit mode requires SAE credentials. H2E modes require H2E support. */
    saePwe?: "hunting-and-pecking" | "hash-to-element" | "both";
    /** sae_pk_mode; unit: enum; Automatic where built; disabled always accepted. Only requires a 1..63-byte password, WPA3 threshold, required PMF and H2E; reject explicit weaker dependencies. Native authentication validates password checksum and PK/H2E peer support. */
    saePkMode?: "automatic" | "only" | "disabled";
    /** failure_retry_cnt; unit: retries per AP; Integer 0..255, default 0; nonzero requires all-channel scan and increases connection latency. */
    failureRetryCount?: number;
    /** he_dcm_set; unit: boolean; Boolean, default false. Explicit HE controls require HE support; no implicit protocol/mode change. */
    heDcmSet?: boolean;
    /** he_dcm_max_constellation_tx; unit: constellation enum; 0 unsupported, 1 BPSK, 2 QPSK, 3 16-QAM; explicit value requires heDcmSet=true. When set, omitted TX/RX defaults to 3. */
    heDcmMaxConstellationTx?: 0 | 1 | 2 | 3;
    /** he_dcm_max_constellation_rx; unit: constellation enum; 0 unsupported, 1 BPSK, 2 QPSK, 3 16-QAM; explicit value requires heDcmSet=true. When set, omitted TX/RX defaults to 3. */
    heDcmMaxConstellationRx?: 0 | 1 | 2 | 3;
    /** he_mcs9_enabled; unit: boolean; Boolean, default false. Explicit HE controls require HE support; no implicit protocol/mode change. */
    heMcs9Enabled?: boolean;
    /** he_su_beamformee_disabled; unit: boolean; Boolean, default false. Explicit HE controls require HE support; no implicit protocol/mode change. */
    heSuBeamformeeDisabled?: boolean;
    /** he_trig_su_bmforming_feedback_disabled; unit: boolean; Boolean, default false. Explicit HE controls require HE support; no implicit protocol/mode change. */
    heTrigSuBeamformingFeedbackDisabled?: boolean;
    /** he_trig_mu_bmforming_partial_feedback_disabled; unit: boolean; Boolean, default false. Explicit HE controls require HE support; no implicit protocol/mode change. */
    heTrigMuBeamformingPartialFeedbackDisabled?: boolean;
    /** he_trig_cqi_feedback_disabled; unit: boolean; Boolean, default false. Explicit HE controls require HE support; no implicit protocol/mode change. */
    heTrigCqiFeedbackDisabled?: boolean;
    /** vht_su_beamformee_disabled; unit: boolean; Boolean, default false; explicit VHT controls require VHT support and never switch protocols implicitly. */
    vhtSuBeamformeeDisabled?: boolean;
    /** vht_mu_beamformee_disabled; unit: boolean; Boolean, default false; explicit VHT controls require VHT support and never switch protocols implicitly. */
    vhtMuBeamformeeDisabled?: boolean;
    /** vht_mcs8_enabled; unit: boolean; Boolean, default false; explicit VHT controls require VHT support and never switch protocols implicitly. */
    vhtMcs8Enabled?: boolean;
    /** sae_h2e_identifier; unit: UTF-8 bytes; 1..32 bytes without NUL; requires H2E and 1..63-byte SAE password; copied and wiped with credentials. Secret: never status/error/log or default readback. */
    saeH2eIdentifier?: string;
  }

  interface WiFiAccessPointDriverConfig {
    /** ssid; unit: bytes; Required 1..32 UTF-8 bytes without NUL, or 1..32 raw bytes captured by value. */
    ssid: string | ByteSource;
    /** password; unit: UTF-8 bytes; No embedded NUL; empty only for open/OWE; WPA/WPA2 8..63 bytes, pure SAE 1..63 bytes. Validate auth/cipher/PMF before mutation; no 64-byte passphrase truncation. Secret: never status/error/log or default readback. */
    password?: string;
    /** channel; unit: 802.11 channel; Station scan hint: 0, 1..13, or a supported 5 GHz channel; does not connect or force the shared channel. */
    channel?: number;
    /** authmode; unit: enum; Default WPA2 with password, open otherwise; saeExt selects pure WPA3 when auth is omitted. Reject unsupported modes and enforce password/cipher/PMF dependencies. */
    authMode?: "open" | "wpa" | "wpa2" | "wpa/wpa2" | "wpa3" | "wpa2/wpa3" | "owe";
    /** ssid_hidden; unit: boolean; Boolean, default false; only 0/1 SDK values emitted. */
    hidden?: boolean;
    /** max_connection; unit: clients; Integer 1..actual framework/build shared key budget; default framework maximum. */
    maxConnections?: number;
    /** beacon_interval; unit: TU (1024 microseconds); Integer 100..60000, multiple of 100; default 100. Raw driver config uses TU, high-level startAP uses milliseconds. */
    beaconIntervalTu?: number;
    /** csa_count; unit: beacons; Integer 1..255; default 3. */
    csaCount?: number;
    /** dtim_period; unit: beacon intervals; Integer 1..10; default 1. */
    dtimPeriod?: number;
    /** pairwise_cipher; unit: enum; Auth-dependent default; require validated readback, reject incompatible or weaker security combinations. */
    pairwiseCipher?: "tkip" | "ccmp" | "tkip/ccmp" | "gcmp" | "gcmp-256";
    /** ftm_responder; unit: boolean; Boolean, default false; only responder configuration, not FTM session API. */
    ftmResponder?: boolean;
    /** Three-state PMF policy; disabled requires eligible authentication and runs the dedicated SDK operation before START.
     * Station also requires disableWpa3CompatibleMode=true. */
    pmf?: "disabled" | "optional" | "required";
    /** sae_pwe_h2e; unit: enum; Omitted uses SDK unspecified; explicit mode requires SAE credentials. H2E modes require H2E support. */
    saePwe?: "hunting-and-pecking" | "hash-to-element" | "both";
    /** transition_disable; unit: boolean; Boolean, default false; true requires WPA3/WPA2-WPA3 or compatible mode and retains requested security on readback. */
    transitionDisable?: boolean;
    /** sae_ext; unit: boolean; Boolean, default false; true requires pure WPA3, GCMP-256, required PMF and H2E. Supply omitted defaults, reject explicit conflicts and compatible mode; require exact flag readback. */
    saeExt?: boolean;
    /** wpa3_compatible_mode; unit: boolean; Boolean, default false; true explicitly authorizes WPA2/CCMP base override for WPA2/WPA3/WPA2-WPA3 input. Requires 8..63-byte password; preserve requested PMF strength and verify base/flag readback. */
    wpa3CompatibleMode?: boolean;
    /** bss_max_idle_cfg.period; unit: 1000 TU (1.024 seconds); Integer 0 (disabled) or 10..65535; default 0. Verify exact period readback; explicit 0 remains valid without feature. */
    bssMaxIdlePeriod?: number;
    /** bss_max_idle_cfg.protected_keep_alive; unit: boolean; Boolean, default false; true requires nonzero idle period and encrypted AP authentication; independent PMF policy remains enforced. Verify exact flag readback. */
    bssMaxIdleProtectedKeepAlive?: boolean;
    /** gtk_rekey_interval; unit: seconds; Integer 0 (disabled) or 60..65535; reject an unsupported auth combination. */
    gtkRekeyIntervalSeconds?: number;
  }

  interface WiFiCountryDetails {
    code: string;
    /** Defaults to auto. */
    policy?: "auto" | "manual";
    /** Details form requires both startChannel and channelCount, each 1..14, ending at most 14. */
    startChannel?: number;
    channelCount?: number;
    environment?: "indoor" | "outdoor" | "X" | null;
    /** Uint32 known channel bits. Nonzero requires manual policy; requires 5 GHz build support. */
    ghz5ChannelMask?: number;
  }

  interface WiFiProtocolConfig {
    /** Full nonempty protocol set, not the maximum-protocol shorthand. */
    ghz2?: WiFiProtocol[];
    ghz5?: WiFiProtocol[];
  }

  interface WiFiBandwidthConfig {
    ghz2MHz?: 20 | 40;
    ghz5MHz?: 20 | 40;
  }

  /** Wait options shared by Wi-Fi disconnect, stop and stopAP. */
  interface WiFiWaitOptions {
    /** Integer 1..2147483647 ms. stop/stopAP default 1000; disconnect defaults to wifi.DEFAULT_TIMEOUT_MS. SDK calls are not preemptible. */
    timeoutMs?: number;
  }

  interface WiFiStartOptions {
    /** Preserve configured mode when omitted; cold default station. */
    mode?: "station" | "ap" | "apsta";
    /** Preserve configured storage when omitted; cold default ram. */
    storage?: WiFiStorage;
  }

  interface WiFiConfigureOptions {
    /** Preserve initialized driver's mode; cold mode follows supplied interfaces, or Station. */
    mode?: "station" | "ap" | "apsta";
    /** Preserve initialized driver's storage; cold default RAM. */
    storage?: WiFiStorage;
    /** Preserve initialized driver's started state; cold default true. Does not connect Station. */
    start?: boolean;
    /** Default false. Required for an established Station or any running AP; never closes other features. */
    allowDisconnect?: boolean;
    station?: WiFiStationDriverConfig;
    /** Required when the resolved mode includes AP and start is true. */
    accessPoint?: WiFiAccessPointDriverConfig;
    country?: string | WiFiCountryDetails;
    protocols?: { station?: WiFiProtocolConfig; "access-point"?: WiFiProtocolConfig };
    bandwidths?: { station?: WiFiBandwidthConfig; "access-point"?: WiFiBandwidthConfig };
    /** 2..20 dBm, 0.25 dBm steps; requires start:true after defaults. Applied after native START. */
    txPowerDbm?: number;
    powerSave?: WiFiPowerSaveMode;
  }

  interface WiFiConfigureError extends NativeError {
    code: "WIFI_CONFIG_FAILED" | "WIFI_CONFIG_UNSUPPORTED";
    operation: "wifi.configure";
    details: {
      stage: "capture" | "admission" | NonNullable<WiFiStatus["cleanupStage"]>;
      option: string | null;
      espCode: number;
      espName: string;
      lifecycleAdmitted: boolean;
      /** Quiesce was invoked; does not imply that the driver was previously running. */
      stopAttempted: boolean;
      cleanupPending: boolean;
      restartRequired: boolean;
      /** This call's native config attempt, or null if not reached. */
      configuration: WiFiConfigurationStatus | null;
      /** This call's failed post-start TX-power attempt, or null if not reached. */
      activation: WiFiConfigurationStatus | null;
    };
  }

  type WiFiPacketType = "management" | "control" | "data" | "misc" | "unknown";
  type WiFiPhyFormat = "legacy" | "ht" | "vht" | "he-su" | "he-mu" | "he-er-su" | "he-tb" | "unknown";
  /** Numeric Protocol Version 0 MAC identity; independent of SDK callback categories. */
  interface WiFiFrameSelector {
    type: 0 | 1 | 2 | 3;
    /** Integer 0–15. */
    subtype: number;
    /** Optional catalogue assertion; when present must match the numeric pair. */
    name?: WiFiFrameName | null;
  }
  type WiFiFrameName =
    "association-request" | "association-response" | "reassociation-request" | "reassociation-response"
    | "probe-request" | "probe-response" | "timing-advertisement" | "beacon" | "atim"
    | "disassociation" | "authentication" | "deauthentication" | "action" | "action-no-ack"
    | "vht-ndp-announcement" | "control-wrapper" | "block-ack-request" | "block-ack"
    | "ps-poll" | "rts" | "cts" | "ack" | "cf-end" | "cf-end-cf-ack"
    | "data" | "data-cf-ack" | "data-cf-poll" | "data-cf-ack-cf-poll"
    | "null" | "cf-ack" | "cf-poll" | "cf-ack-cf-poll"
    | "qos-data" | "qos-data-cf-ack" | "qos-data-cf-poll" | "qos-data-cf-ack-cf-poll"
    | "qos-null" | "qos-cf-poll" | "qos-cf-ack-cf-poll";
  interface WiFiFrameType extends WiFiFrameSelector {
    /** Common RX/TX name; null for an unknown or reserved layout. Not a support guarantee. */
    name: WiFiFrameName | null;
  }
  interface WiFiPacketInfo {
    /** SDK callback category; misc does not imply MAC type 3. */
    category: WiFiPacketType;
    /** Null without a readable PV0 Frame Control. Check capture.parseValid separately. */
    frameType: WiFiFrameType | null;
    frameControl: number | null;
    durationId: number | null;
    sequenceControl: number | null;
    qosControl: number | null;
    flags: {
      toDs: boolean | null; fromDs: boolean | null; moreFragments: boolean | null;
      retry: boolean | null; powerManagement: boolean | null; moreData: boolean | null;
      protected: boolean | null; order: boolean | null;
    };
    capture: {
      /** Full packet capture up to snapLength, not a guarantee of complete RF bytes. */
      mode: "header" | "full";
      headerLength: number; payloadLength: number; driverLength: number;
      capturedLength: number; payloadCapturedLength: number;
      truncated: boolean; fcs: "unknown"; pointerLayoutValid: boolean; parseValid: boolean;
    };
  }
  interface WiFiRxInfo {
    sequence: number;
    timestampUs: number;
    timestampAccuracy: "callback-time";
    /** Header sequence number when known; Monitor currently reports null. */
    rxSequence: number | null;
    radioGeneration: number;
    signal: { rssi: number; noiseFloor: number | null; antenna: number | null };
    channel: { band: WiFiBand | null; primary: number; secondary: WiFiSecondaryChannel | null };
    phy: {
      format: WiFiPhyFormat;
      /** VHT/HE nominal PPDU width, not occupied tones or receiver capture width. */
      bandwidthMHz: 20 | 40 | 80 | 160 | null;
      /** PHY guard duration, independent of callback timestamp accuracy. */
      guardIntervalNs: 400 | 800 | 1600 | 3200 | null;
      /** HE-LTF size multiplier, not the number of HE-LTF symbols. */
      heLtfSize: 1 | 2 | 4 | null;
      dcm: boolean | null;
      mcs: number | null;
      /** Raw target rate codes are not exposed as bitrates. */
      legacyRate: null;
      stbc: boolean | null; shortGuardInterval: boolean | null;
      fecCoding: "bcc" | "ldpc" | null; aggregation: boolean | null;
      /** Null when unavailable, including HE-layout HT and the wire sentinel 255. */
      ampduCount: number | null; smoothingRecommended: boolean | null; sounding: boolean | null;
    };
    addresses: {
      source: string | null; destination: string | null; transmitter: string | null;
      receiver: string | null; bssid: string | null;
    };
    /** Metadata-only native callbacks have no packet bytes. */
    packet: WiFiPacketInfo | null;
  }
  interface WiFiRxFilter {
    types?: Array<Exclude<WiFiPacketType, "unknown">>;
    subtypes?: number[];
    /** OR across exact pairs, AND with other filters. At most 64 distinct pairs; empty matches none. */
    frames?: WiFiFrameSelector[];
    sourceMac?: string | string[];
    destinationMac?: string | string[];
    bssid?: string | string[];
    minimumRssi?: number;
    /** Monitor: valid MAC parse and RX status. CSI: valid sample/channel estimate. */
    validOnly?: boolean;
    sampleEvery?: number;
    /** 0 disables the rate limit; otherwise 1..1000000. */
    maximumRateHz?: number;
  }
  interface WiFiMonitorOptions {
    /** Omit to follow Radio, or request a fixed regulatory channel. */
    channel?: number | "current";
    filter?: WiFiRxFilter;
    capture?: { snapLength?: number; requireComplete?: boolean };
    buffering?: { poolCapacity?: number; queueCapacity?: number; overflow?: "drop-newest" };
  }
  interface WiFiMonitorCapabilities {
    apiVersion: "wifi-monitor/1";
    available: boolean;
    stability: "candidate";
    target: string; idfVersion: string;
    packetTypes: Array<Exclude<WiFiPacketType, "unknown">>;
    /** Named MAC layouts understood by the parser, not a reception or TX guarantee. */
    frameTypes: WiFiFrameType[];
    supports: {
      receive: boolean; frameSource: boolean; receiveBatch: boolean; configure: boolean;
      fixedChannel: boolean; typeFilter: boolean; subtypeFilter: boolean; frameFilter: boolean;
      sourceMacFilter: boolean; destinationMacFilter: boolean; bssidFilter: boolean;
      rssiFilter: boolean; nativeDecimation: boolean; nativeRateLimit: boolean;
      wireSource: boolean; hostPcapngConverter: boolean;
    };
    timestampAccuracy: "callback-time";
    limits: {
      maxSessions: number; maxPoolCapacity: number; maxQueueCapacity: number;
      maxSnapLength: number; maxBatchFrames: number; maxMacsPerRole: number;
    };
  }
  interface WiFiMonitorQueueStatus {
    open: boolean; queued: number; capacity: number; highWater: number; receiverPending: boolean;
  }
  interface WiFiMonitorStatus {
    state: "stopped" | "starting" | "running" | "stopping" | "closed";
    closeRequested: boolean; stopRequested: boolean; channelConflicted: boolean;
    cleanupPending: boolean; retirementBlocked: boolean; poolRetained: boolean;
    accepting: boolean; radioLeaseHeld: boolean; subscriberHeld: boolean; fixedChannelHeld: boolean;
    /** Null after native queue detach. Independently sampled from pool counters. */
    queue: WiFiMonitorQueueStatus | null;
    generation: number; radioGeneration: number;
    /** Zero means follow Radio. */
    requestedChannel: number;
    /** Snapshot at start; each Frame reports its own RX channel. */
    startChannel: number; startChannelGeneration: number;
    poolCapacity: number; queueCapacity: number; snapLength: number;
    requireComplete: boolean;
    allocatedPoolBytes: number; leasedFrames: number;
    lastEspCode: number; cleanupEspCode: number; lastStage: string | null; cleanupStage: string | null;
  }
  interface WiFiMonitorStats {
    callbacks: number; accepted: number; droppedClosing: number; droppedPoolFull: number;
    droppedQueueFull: number; invalidCallbackData: number; droppedRequiredComplete: number;
    truncatedFrames: number; receivedBytes: number; capturedBytes: number;
    droppedIdentityExhausted: number; leasedFrames: number; publishers: number;
    freeSlots: number; identityExhausted: boolean;
    /** First matching rejection reason; native counters saturate at uint64 max. */
    filtered: {
      invalidConfig: number; invalidCallback: number; invalidHeader: number; rxError: number;
      type: number; subtype: number; mac: number; rssi: number; decimation: number; rate: number;
    };
  }
  interface WiFiMonitorFrame {
    readonly info: WiFiRxInfo;
    /** Retains the native payload independently; close the returned view. */
    bytes(): ByteView;
    /** Independent heap copy; close the returned view. */
    copyBytes(): ByteView;
    /** One-shot raw packet ByteSpanSource, without a wire envelope. */
    source(): ByteSpanSource;
    close(): void;
  }
  interface WiFiReceiveBatchOptions {
    /** Defaults to min(32, poolCapacity); integer 1..poolCapacity, bounded by 128. */
    maximumFrames?: number;
    /** Defaults to 1; may not exceed maximumFrames. A partial batch can be returned. */
    minimumFrames?: number;
    /** Wait for the first Frame; omit for no deadline, 0 to poll. */
    timeoutMs?: number;
    /** Additional aggregation window after the first Frame; defaults to 0. */
    maximumLatencyMs?: number;
  }
  interface WiFiMonitorBatch {
    readonly frameCount: number;
    info(index: number): WiFiRxInfo;
    bytes(index: number): ByteView;
    /** One-shot sole-v1 wire; retains native payloads across Batch/Session close. */
    source(options: { format: "esp32qjs-monitor/1" }): ByteSpanSource;
    close(): void;
  }
  interface WiFiMonitorSession {
    /** Cooperative aggregation on the original queue; call directly, not through Future.call. */
    receiveBatch(options?: WiFiReceiveBatchOptions): WiFiMonitorBatch | null;
    /** Full replacement while stopped; preserves retired Frames/Views, then requires start(). */
    configure(options: WiFiMonitorOptions): WiFiMonitorStatus;
    status(): WiFiMonitorStatus;
    stats(): WiFiMonitorStats;
    /** EventQueue receive semantics; timeout ends the wait and preserves capture. */
    receive(timeoutMs?: number): WiFiMonitorFrame | null;
    start(): WiFiMonitorStatus;
    /** Stops capture and discards queued packets, retaining Radio until close. */
    stop(): WiFiMonitorStatus;
    /** Native cleanup can remain pending; inspect status and retry the same handle. */
    close(): void;
  }
  interface WiFiMonitorModule {
    capabilities(): WiFiMonitorCapabilities;
    open(options?: WiFiMonitorOptions): WiFiMonitorSession;
  }

  interface WiFiRawTxSendOptions {
    interface?: "station" | "access-point";
    /** Current while idle; pinned for each in-flight submission. */
    channel?: "current" | number;
    /** Passed unchanged to the SDK; association-dependent restrictions are not prechecked by the framework. */
    sequenceControl?: "driver" | "application";
    /** Whole Future deadline including waiting/initialization; 1-2147483647 ms, default 1000. */
    timeoutMs?: number;
  }
  interface WiFiRawTxResult {
    sequence: number;
    radioGeneration: number;
    interface: "station" | "access-point";
    channel: number;
    frameType: WiFiFrameType;
    byteLength: number;
    submittedAtUs: number;
    completedAtUs: number;
    completion: WiFiRawTxCompletion;
    /** Target SDK rate name after native completion; null for unknown numeric codes. */
    rate: WiFiTxRate | null;
    rawRate: number;
  }
  interface WiFiRawTxNativeStatus {
    /** Generation captured with operationIdentity in the same broker snapshot. */
    radioGeneration: number | null;
    operationActive: boolean;
    /** Physical deinit proof retained for an exact native owner, not TX completion. */
    nativeTerminated: boolean;
    terminatedRadioGeneration: number | null;
    operationIdentity: number | null;
    quarantined: boolean;
    correlationFault: boolean;
    /** First broker correlation failure; retained until a fresh registration. */
    correlationFailureReason: "invalid-info" | "invalid-interface" | "interface-mismatch" |
      "destination-mismatch" | "source-mismatch" | "orphan-callback" | "duplicate-callback" |
      "completion-after-rejection" | "descriptor-reused" | "descriptor-transfer" | "allocation-binding" | "callback-overflow" | null;
    /** Originating native operation; null if unattributable. May differ from operationIdentity. */
    correlationFailureIdentity: number | null;
    correlationFailureGeneration: number | null;
    /** Saturating boot-lifetime counters; recovery does not reset them. */
    invalidCallbacks: number;
    mismatchedCallbacks: number;
    orphanCallbacks: number;
    duplicateCallbacks: number;
    cleanupPending: boolean;
    cleanupStage: string | null;
    cleanupError: number | null;
    submitError: number | null;
    registrationError: number | null;
    identityExhausted: boolean;
    /** Boot-scoped scheduler grant, distinct from operationIdentity. */
    laneIdentity: number | null;
    laneWaiters: number;
    laneIdentityExhausted: boolean;
    liveSessions: number;
    retainedClosedSessions: number;
    closingSessions: number;
    faultedSessions: number;
    registeredSessionResults: number;
    registeredSessionFlushes: number;
    /** First observed Session error; individual Session status gives its own diagnostics. */
    sessionErrorGeneration: number | null;
    sessionError: number | null;
    sessionStage: string | null;
    sessionCleanupError: number | null;
    sessionCleanupStage: string | null;
    periodicJobs: number; retiredPeriodicJobs: number; faultedPeriodicJobs: number;
    periodicCleanupPending: number; periodicIdentityExhausted: boolean;
    periodicErrorGeneration: number | null; periodicError: number | null; periodicStage: string | null;
    periodicCleanupError: number | null; periodicCleanupStage: string | null;
  }
  interface WiFiRawTxCapabilities {
    apiVersion: "wifi-raw-tx/1";
    target: string;
    idfVersion: string;
    stability: "candidate";
    /** Interfaces admitting a pre-start exclusive Session rate lease. */
    rateLeaseInterfaces: ("station" | "access-point")[];
    interfaces: Array<"station" | "access-point">;
    /** Supported types only; order is not significant. Frame and live Radio constraints still apply. */
    frameTypes: WiFiFrameType[];
    supports: {
      driverSequence: boolean; applicationSequence: boolean;
      txDoneCallback: boolean; fixedChannel: boolean;
      nativeQueue: boolean; batchAdmission: boolean; periodicTx: boolean;
      rateLease: boolean; recovery: boolean; callerFcs: false;
      /** Build-local, hash-gated SDK extension; does not imply RF or PMF qualification. */
      extendedManagement: boolean;
    };
    limits: {
      minimumFrameBytes: 24; maximumFrameBytes: 1500;
      maximumQueueCapacity: number; maximumQueueBytes: number; maximumInFlight: number; maximumBatchFrames: number;
      maximumSessions: number;
      maximumPendingResultsPerSession: number;
      maximumPendingFlushesPerSession: number;
      minimumPeriodicIntervalUs: number | null;
      maximumPeriodicJobs: number;
    };
  }
  /** Candidate SDK-built Action frames. Completion is a driver observation, not peer delivery. */
  interface WiFiActionSendOptions {
    interface?: "station" | "access-point";
    channel: number;
    secondaryChannel?: "none" | "above" | "below";
    destination: string;
    bssid?: string;
    /** Action body including category; excludes MAC header and FCS. 1-1476 bytes. */
    payload: ByteSource;
    /** Native channel residency, 1-60000 ms, default 100. */
    waitMs?: number;
    /** Public deadline, 1-60000 ms, default 1000. Does not preempt SDK calls. */
    timeoutMs?: number;
    noAck?: boolean;
  }
  interface WiFiActionResult {
    sequence: number; radioGeneration: number; operationId: number;
    interface: "station" | "access-point"; channel: number; payloadBytes: number;
    /** Null if the operation ended without a TX observation. */
    completion: TxCompletion<WiFiActionNativeCode> | null;
    terminalStatus: "duration-completed" | "cancelled";
  }
  interface WiFiFtmError extends NativeError {
    code: "WIFI_FTM_FAILED" | "WIFI_FTM_TIMEOUT" | "WIFI_FTM_CLOSED";
    operation: "wifi.ftm.start" | "WiFiFtmSession.receive" | "WiFiFtmSession.end" | "WiFiFtmSession.close";
    details: {
      espCode: number; espName: string; stage: string;
      sequence: number | null; radioGeneration: number | null;
      endRequested: boolean; closeRequested: boolean; cleanupPending: boolean;
      cleanupError: number | null; cleanupStage: string | null;
    };
  }
  interface WiFiFtmOptions {
    /** Required unicast, nonzero colon-separated MAC. */
    peerAddress: string;
    /** Explicit responder primary channel; live Radio regulatory/owner checks apply. */
    channel: number;
    /** Default 0 means no preference. */
    frameCount?: 0 | 16 | 24 | 32 | 64;
    /** 0 (no preference), or 200-10000 in multiples of 100 ms. Default 0. */
    burstPeriodMs?: number;
    /** 0-64, default 64; 0 requests summary only. */
    maxReportEntries?: number;
    /** Opening wait only, 1-60000 ms, default 1000. Timeout requests close. */
    timeoutMs?: number;
  }
  interface WiFiFtmWaitOptions { /** 1-60000 ms, default 1000. */ timeoutMs?: number; }
  type WiFiFtmTerminalStatus = "success" | "unsupported" | "configuration-rejected" | "no-response" | "failed" | "no-valid-measurement" | "terminated";
  /** Exact unsigned 64-bit native picoseconds; not a JS number or wall-clock time. */
  interface WiFiFtmTimestampPs { low: number; high: number; }
  interface WiFiFtmReportEntry {
    dialogToken: number; rssi: number; rttPs: number; ppm: number;
    t1Ps: WiFiFtmTimestampPs; t2Ps: WiFiFtmTimestampPs; t3Ps: WiFiFtmTimestampPs; t4Ps: WiFiFtmTimestampPs;
  }
  interface WiFiFtmReport {
    sequence: number; radioGeneration: number; peerAddress: string;
    status: WiFiFtmTerminalStatus; statusId: number;
    rttRawNs: number | null; rttEstimatedNs: number | null; distanceCm: number | null;
    reportEntries: number; copiedEntries: number; truncated: boolean;
    entries: WiFiFtmReportEntry[];
  }
  interface WiFiFtmStatus {
    state: "opening" | "active" | "ending" | "draining" | "completed" | "closing" | "closed" | "faulted";
    peerAddress: string; channel: number; frameCount: number; burstPeriodMs: number;
    driverAccepted: boolean; endRequested: boolean; closeRequested: boolean; cleanupPending: boolean;
    sequence: number | null; radioGeneration: number | null;
    terminalStatus: WiFiFtmTerminalStatus | null; terminalStatusId: number | null; ambiguous: boolean; physicalTermination: boolean;
    reportReady: boolean; reportConsumed: boolean; reportDiscarded: boolean;
    reportCapacity: number; retainedEntries: number; reportEntries: number | null; copiedEntries: number;
    sdkFenced: boolean; eventFenced: boolean;
    error: number | null; stage: string | null; cleanupError: number | null; cleanupStage: string | null;
  }
  interface WiFiFtmGlobalStatus {
    active: boolean; handles: number; reservedEntries: number; workerBusy: boolean;
    cleanupPending: boolean; session: WiFiFtmStatus | null;
  }
  interface WiFiFtmCapabilities {
    apiVersion: "wifi-ftm/1"; stability: "candidate"; target: string;
    initiator: boolean; responderConfiguration: boolean; responderOffset: boolean; recovery: boolean;
    maximumOperations: number; maximumHandles: number; maximumReportEntries: number; maximumRetainedEntries: number;
  }
  interface WiFiFtmSession {
    status(): WiFiFtmStatus;
    /** Timeout starts when scheduled, returns null; cancellation only stops waiting. Repeated calls copy the saved report. */
    receive(options?: WiFiFtmWaitOptions): WiFiFtmReport | null;
    /** Wait for native termination/drain, keeping the final report. Timeout preserves end intent. */
    end(options?: WiFiFtmWaitOptions): void;
    /** Discard report and wait for native drain. Timeout preserves cleanup. Idempotent. */
    close(options?: WiFiFtmWaitOptions): void;
  }
  interface WiFiFtmModule {
    capabilities(): WiFiFtmCapabilities;
    /** Initiator build only. Already-started Station interface required. Also supports Future.call. */
    start?(options: WiFiFtmOptions): WiFiFtmSession;
    /** Initiator build only. Individually locked observations, not a combined atomic snapshot. */
    status?(): WiFiFtmGlobalStatus;
    /** Initiator build only. Explicit physical recovery; never repeats the measurement or reconnects Station. */
    recover?(options: WiFiFtmRecoveryOptions): WiFiFtmRecoveryResult;
    /** Responder + SoftAP build only. Stopped AP/APSTA, zero owners. Signed integer centimeters. */
    setResponderOffsetCm?(centimeters: number): WiFiFtmResponderOffsetStatus;
    /** Responder + SoftAP build only. Framework acceptance record, not SDK readback. */
    responderOffsetStatus?(): WiFiFtmResponderOffsetStatus;
  }
  interface WiFiFtmResponderOffsetStatus {
    radioGeneration: number | null; revision: number; acceptedRevision: number | null;
    requestedCm: number | null; lastAcceptedCm: number | null; valueCm: number | null;
    configured: boolean; known: boolean; uncertain: boolean; error: number | null;
  }
  interface WiFiFtmRecoveryOptions {
    /** Exact active native FTM identity from Session/global status. */
    sequence: number; radioGeneration: number;
    /** Also permit an established Station connection to disconnect. Default false. */
    allowDisconnect?: boolean;
    /** Scheduled wait deadline, 1-60000 ms, default 10000. Timeout retains central cleanup. */
    timeoutMs?: number;
  }
  interface WiFiFtmRecoveryResult {
    sequence: number; previousRadioGeneration: number; radioGeneration: number;
  }
  interface WiFiFtmRecoveryError extends NativeError {
    code: "WIFI_FTM_RECOVERY_FAILED" | "WIFI_FTM_RECOVERY_TIMEOUT";
    operation: "wifi.ftm.recover";
    details: { espCode: number; espName: string; stage: string; sequence: number; radioGeneration: number;
      lifecycleAdmitted: boolean; checkpointAttempted: boolean; replayAttempted: boolean; resumeAttempted: boolean;
      cleanupPending: boolean; restartRequired: boolean; radioFaultStage: string | null; radioFaultError: number | null };
  }
  interface WiFiFtmOffsetError extends NativeError {
    code: "WIFI_FTM_OFFSET_FAILED"; operation: "wifi.ftm.setResponderOffsetCm";
    details: { espCode: number; espName: string; stage: string; mutationAttempted: boolean;
      offset: WiFiFtmResponderOffsetStatus };
  }

  interface WiFiRocOptions {
    interface?: "station" | "access-point";
    channel: number; secondaryChannel?: "none" | "above" | "below";
    /** Required native residency 1-60000 ms. */
    durationMs: number; allowBroadcast?: boolean;
    /** Opening deadline 1-60000 ms, default 1000; does not change residency duration. */
    timeoutMs?: number;
  }
  interface WiFiRocStatus {
    state: "opening" | "active" | "closing" | "faulted" | "closed";
    interface: "station" | "access-point";
    channel: number; secondaryChannel: "none" | "above" | "below";
    durationMs: number; allowBroadcast: boolean; driverAccepted: boolean;
    closeRequested: boolean; cleanupPending: boolean;
    sequence: number | null; radioGeneration: number | null; operationId: number | null;
    terminalStatus: "completed" | "cancelled" | null; ambiguous: boolean; nativeQuiescent: boolean; nativeTerminated: boolean;
    error: number | null; stage: string | null; cleanupError: number | null; cleanupStage: string | null;
  }
  /** Created by wifi.action.remainOnChannel; retained closed handles count toward the native limit. */
  interface WiFiRocSession {
    status(): WiFiRocStatus;
    /** Wait timeout/cancel only stops waiting; does not cancel residency. */
    wait(options?: { timeoutMs?: number }): WiFiRocStatus;
    /** Native cancellation and drain; timeout retains cleanup responsibility. Idempotent. */
    close(options?: { timeoutMs?: number }): void;
  }
  interface WiFiActionStatus {
    kind: "send" | "roc" | null; rocHandles: number; rocActive: boolean;
    rocError: number | null; rocStage: string | null; rocCleanupError: number | null; rocCleanupStage: string | null;
    operationActive: boolean; sequence: number | null; radioGeneration: number | null;
    operationId: number | null; terminal: boolean; ambiguous: boolean; nativeQuiescent: boolean; nativeTerminated: boolean; identityExhausted: boolean;
    cancelWritten: boolean; sdkFenced: boolean; eventFenced: boolean;
    cleanupPending: boolean; cleanupError: number | null; cleanupStage: string | null;
    submitError: number | null; cancelError: number | null;
  }
  interface WiFiActionCapabilities {
    apiVersion: "wifi-action/1"; stability: "candidate"; target: string;
    accessPoint: boolean; maximumPayloadBytes: number; maximumOperations: number;
    remainOnChannel: boolean; maximumRocHandles: number; recover: boolean;
  }
  interface WiFiActionRecoveryOptions {
    /** Exact framework identity from action.status(); the native operationId is not accepted. */
    sequence: number;
    radioGeneration: number;
    /** Permit disconnecting the established Station. Recovery always restarts managed Wi-Fi/AP. */
    allowDisconnect?: boolean;
    /** 1-60000 ms, default 10000; native SDK calls cannot be preempted. */
    timeoutMs?: number;
  }
  interface WiFiActionRecoveryResult {
    sequence: number;
    previousRadioGeneration: number;
    radioGeneration: number;
  }
  interface WiFiActionModule {
    /** Readable-source physical recovery; also supports Future.call. Does not reconnect Station. */
    recover(options: WiFiActionRecoveryOptions): WiFiActionRecoveryResult;
    remainOnChannel(options: WiFiRocOptions): WiFiRocSession;
    capabilities(): WiFiActionCapabilities;
    send(options: WiFiActionSendOptions): WiFiActionResult;
    status(): WiFiActionStatus;
  }

  interface WiFiRawTxModule {
    capabilities(): WiFiRawTxCapabilities;
    /** Exact broker operation from wifi.status().rawTx; supports Future.call. */
    recover(options: WiFiRawTxRecoveryOptions): WiFiRawTxRecoveryResult;
    /** Synchronous wait over a native Future driver; also usable with Future.call. */
    send(frame: ByteSource, options?: WiFiRawTxSendOptions): WiFiRawTxResult;
    /** Wait for native Radio opening. Also usable with Future.call. */
    open(options?: WiFiRawTxOpenOptions): WiFiRawTxSession;
  }
  interface WiFiRawTxRecoveryOptions {
    /** wifi.status().rawTx.operationIdentity, not a Session queue or periodic sequence. */
    sequence: number;
    /** Same broker snapshot's radioGeneration. */
    radioGeneration: number;
    /** Permit disconnecting an established Station. Default false. AP restart is authorized by the call. */
    allowDisconnect?: boolean;
    /** Scheduled wait deadline 1-60000 ms, default 10000; admitted cleanup survives timeout. */
    timeoutMs?: number;
  }
  interface WiFiRawTxRecoveryResult {
    sequence: number; previousRadioGeneration: number; radioGeneration: number;
  }
  interface WiFiRawTxRecoveryError extends NativeError {
    code: "WIFI_RAW_TX_RECOVERY_FAILED" | "WIFI_RAW_TX_RECOVERY_TIMEOUT";
    operation: "wifi.rawTx.recover";
    details: { espCode: number; espName: string; stage: string; sequence: number; radioGeneration: number;
      lifecycleAdmitted: boolean; checkpointAttempted: boolean; replayAttempted: boolean; resumeAttempted: boolean;
      cleanupPending: boolean; restartRequired: boolean; radioFaultStage: string | null; radioFaultError: number | null };
  }
  /** Shared admission policy for native packet queues. Started batches cannot be evicted. */
  type PacketQueueOverflow = "reject-newest" | "drop-oldest-batch";

  interface WiFiRawTxOpenOptions {
    /** Temporary interface-global rate; stopped/owner-free driver with a known predecessor. AP requires configured AP or APSTA mode; Station is not connected automatically. */
    rate?: WiFiTxRateConfig;
    interface?: "station" | "access-point";
    channel?: "current" | number;
    /** Passed unchanged at each submission; the SDK decides association-dependent acceptance. */
    sequenceControl?: "driver" | "application";
    /** Opening deadline, 1-2147483647 ms; default 1000. */
    timeoutMs?: number;
    /** Native submission window, 1-8 and at most capacityPackets; default 1. */
    maxInFlight?: number;
    queue?: {
      /** 1-128 packets including all in-flight packets; default 32. */
      capacityPackets?: number;
      /** Queue-owned payload bytes, 24-capacityPackets*1500; default capacityPackets*1500. */
      capacityBytes?: number;
      overflow?: PacketQueueOverflow;
    };
  }
  interface WiFiRawTxAdmission {
    sessionGeneration: number;
    firstSequence: number;
    lastSequence: number;
    batchSequence: number;
    admittedPackets: number;
    evictedPackets: number;
    evictedBatches: number;
  }
  interface WiFiRawTxStats {
    admitted: number; submitted: number; settled: number; completed: number;
    succeeded: number; failed: number; unknown: number;
    rejected: number; aborted: number; dropped: number;
  }
  interface WiFiRawTxFlushResult extends WiFiRawTxStats {
    sessionGeneration: number;
    throughSequence: number;
    pending: number;
  }
  interface WiFiRawTxSessionResult extends WiFiRawTxResult {
    sessionGeneration: number;
    /** Session admission identity, distinct from the broker sequence. */
    admissionSequence: number;
  }
  interface WiFiRawTxStatus {
    state: "open" | "closing" | "closed" | "faulted";
    sessionGeneration: number;
    radioGeneration: number | null;
    interface: "station" | "access-point";
    channel: number | null;
    sequenceControl: "driver" | "application";
    capacityPackets: number;
    capacityBytes: number; usedBytes: number; availableBytes: number; highWaterBytes: number;
    availablePackets: number; inFlight: number; maxInFlight: number;
    queuedPackets: number;
    pendingPackets: number;
    activeSequence: number | null;
    /** May be waiting for the shared grant. */
    requestIdentity: number | null;
    workerBusy: boolean;
    /** Native periodic children retaining timer/worker/template ownership. */
    periodicJobs: number;
    faulted: boolean;
    cleanupPending: boolean;
    error: number | null;
    stage: string | null;
    cleanupError: number | null;
    cleanupStage: string | null;
  }
  interface WiFiRawPeriodicTxOptions {
    frame: ByteSource;
    /** Integer 1000-4294967295; cadence is best-effort, not a throughput promise. */
    intervalUs: number;
    /** Schedule opportunities, including skips; 0 (default) continues until stopped. */
    count?: number;
    /** Integer 0-4294967295 us; default 0. */
    startDelayUs?: number;
    busyPolicy?: "skip" | "stop";
    /** Stop on failed/rejected/dropped packets; default true. */
    stopOnError?: boolean;
    /** Startup Future deadline, 1-2147483647 ms; default 1000. */
    timeoutMs?: number;
  }
  interface WiFiRawPeriodicTxStatus {
    state: "running" | "stopped" | "closing" | "closed" | "faulted";
    periodicGeneration: number;
    intervalUs: number;
    count: number;
    scheduled: number; issued: number; submitted: number; completed: number; succeeded: number;
    failed: number; unknown: number; rejected: number; aborted: number; dropped: number;
    skippedBusy: number; skippedLate: number;
    activeSequence: number | null;
    retired: boolean; stopRequested: boolean; closeRequested: boolean;
    workerBusy: boolean; timerPresent: boolean; timerQuiesced: boolean; timerTransition: boolean;
    faulted: boolean; exhausted: boolean; uncertain: boolean; cleanupPending: boolean;
    error: number | null; stage: string | null;
    cleanupError: number | null; cleanupStage: string | null;
  }
  interface WiFiRawPeriodicTx {
    status(): WiFiRawPeriodicTxStatus;
    /** Stop future admissions; packets already admitted can still transmit. */
    stop(): void;
    /** Wait up to 1000 ms for native retirement; deadline retains close intent. */
    close(): void;
  }
  interface WiFiRawTxSession {
    /** Capture a template and wait for native timer startup; also supports Future.call. */
    startPeriodic(options: WiFiRawPeriodicTxOptions): WiFiRawPeriodicTx;
    /** A deadline stops waiting; an admitted packet can still transmit. */
    send(frame: ByteSource, options?: { timeoutMs?: number }): WiFiRawTxSessionResult;
    enqueue(frame: ByteSource): WiFiRawTxAdmission;
    enqueueBatch(frames: ArrayLike<ByteSource>): WiFiRawTxAdmission;
    /** Observe free capacity without reserving it. Supports Future.call; timeout/cancel only stop waiting. */
    waitWritable(options?: { minimumPackets?: number; minimumBytes?: number; timeoutMs?: number }): void;
    /** Cumulative result through a captured fence; later enqueue cannot extend it. */
    flush(timeoutMs?: number): WiFiRawTxFlushResult;
    status(): WiFiRawTxStatus;
    stats(): WiFiRawTxStats;
    /** Wait up to 1000 ms for cleanup; a timeout leaves native close requested. */
    close(): void;
  }

  type WiFiTxPhy = "lr" | "11b" | "11g" | "11a" | "ht20" | "ht40" | "he20" | "vht20";
  type WiFiTxRate = "1m-long" | "2m-long" | "5.5m-long" | "11m-long"
    | "2m-short" | "5.5m-short" | "11m-short"
    | "6m" | "9m" | "12m" | "18m" | "24m" | "36m" | "48m" | "54m"
    | "mcs0-long" | "mcs0-short" | "mcs1-long" | "mcs1-short"
    | "mcs2-long" | "mcs2-short" | "mcs3-long" | "mcs3-short"
    | "mcs4-long" | "mcs4-short" | "mcs5-long" | "mcs5-short"
    | "mcs6-long" | "mcs6-short" | "mcs7-long" | "mcs7-short"
    | "mcs8-long" | "mcs8-short" | "mcs9-long" | "mcs9-short" | "lr-250k" | "lr-500k";
  interface WiFiTxRateConfig {
    phy: WiFiTxPhy;
    rate: WiFiTxRate;
    /** Default false; true requires HE20. SDK validates further constraints. */
    ersu?: boolean;
    /** Default false; true requires HE20. */
    dcm?: boolean;
  }
  interface WiFiTxRateLeaseStatus {
    radioGeneration: number;
    radioLeaseIdentity: number;
    writeIdentity: number;
    previous: Required<WiFiTxRateConfig>;
    restorePending: boolean;
    restoreError: number | null;
  }
  interface WiFiTxRateStatus {
    interface: WiFiInterface;
    /** Knowledge from a successful write in this driver generation, not readback. */
    known: boolean;
    uncertain: boolean;
    source: "framework-write" | null;
    /** Same-mutex snapshot; null after restoration has completed. */
    temporaryLease: WiFiTxRateLeaseStatus | null;
    radioGeneration: number;
    writeGeneration: number;
    /** Boot-monotonic; zero means no retained write. Never wraps. */
    writeIdentity: number;
    config: Required<WiFiTxRateConfig> | null;
    /** Last attempted write and rollback errors; null on success/no attempt. */
    error: number | null;
    rollbackError: number | null;
  }
  interface WiFiTxRateError extends NativeError {
    code: "WIFI_TX_RATE_FAILED";
    operation: "wifi.driver.configureTxRate" | "wifi.driver.txRateStatus";
    details: WiFiTxRateStatus & {
      espCode: number;
      stage: "admission" | "write";
      attempted: boolean;
      driverAccepted: boolean;
      rollbackAttempted: boolean;
      restored: boolean;
    };
  }
  interface WiFiDriverReadError extends NativeError {
    code: "WIFI_DRIVER_READ_FAILED";
    operation: "wifi.driver.getProtocol" | "wifi.driver.getProtocols" |
      "wifi.driver.getBandwidth" | "wifi.driver.getBandwidths" |
      "wifi.driver.getBand" | "wifi.driver.getBandMode" | "wifi.driver.getPowerSave" |
      "wifi.driver.getTxPower" | "wifi.driver.getRssi" | "wifi.driver.getAid" |
      "wifi.driver.getNegotiatedPhy" | "wifi.driver.getTsfTime" | "wifi.driver.getInactiveTime" |
      "wifi.driver.getMode" | "wifi.driver.getCountry" | "wifi.driver.getChannel" | "wifi.driver.getHomeChannel" | "wifi.driver.getEventMask" |
      "wifi.driver.getAntenna" | "wifi.driver.getAntennaGpio" |
      "wifi.driver.getStatisticsConfig" | "wifi.driver.getScanParameters" |
      "wifi.driver.getInterfaceConfig" | "wifi.driver.setInterfaceConfig";
    details: {
      /** Null for Radio-wide observations. */
      interface: WiFiInterface | null;
      espCode: number;
      stage: "admission" | "band-mode" | "protocol" | "protocols" | "bandwidth" | "bandwidths" | "decode" |
        "band" | "power-save" | "tx-power" | "rssi" | "aid" | "negotiated-phy" | "tsf-time" | "inactive-time" |
        "mode" | "country" | "channel" | "home-channel" | "event-mask-read" | "antenna" | "antenna-gpio" |
        "secret-readback" | "interface-config" | "he-statistics-admission" | "he-statistics-read" |
        "scan-parameters-admission" | "scan-parameters-read";
    };
  }
  interface WiFiDriverWriteError extends NativeError {
    code: "WIFI_DRIVER_WRITE_FAILED";
    operation: "wifi.driver.setProtocol" | "wifi.driver.setProtocols" |
      "wifi.driver.setBandwidth" | "wifi.driver.setBandwidths" |
      "wifi.driver.setInactiveTime" | "wifi.driver.setRssiThreshold" |
      "wifi.driver.setBand" | "wifi.driver.setBandMode" | "wifi.driver.setEventMask" |
      "wifi.driver.setBssColorCollisionReporting" | "wifi.driver.setScanParameters" |
      "wifi.driver.configureRxStatistics" | "wifi.driver.setTxStatistics" |
      "wifi.driver.setDynamicCarrierSense" | "wifi.driver.configure11bRate" | "wifi.driver.setCoexistencePowerManagement" |
      "wifi.driver.setConnectionlessWakeInterval" | "wifi.driver.setStorage" | "wifi.driver.setMode" | "wifi.driver.disablePmf" |
      "wifi.driver.setAntenna" | "wifi.driver.setAntennaGpio" | "wifi.driver.setCountryDetails" |
      "wifi.driver.setInterfaceConfig" | "wifi.driver.restore";
    details: Omit<WiFiConfigurationStatus, "error"> & {
      /** Null for driver-wide settings. */
      interface: WiFiInterface | null;
      espCode: number;
    };
  }
  type WiFiAntenna = "ant0" | "ant1";
  type WiFiAntennaMode = WiFiAntenna | "auto";
  /** SDK-stored shared PHY configuration, not measured RF selection. */
  interface WiFiAntennaConfig {
    rxMode: WiFiAntennaMode;
    rxDefault: WiFiAntenna;
    /** Stored mode; auto does not imply effective automatic TX selection in the fixed SDK. */
    txMode: WiFiAntennaMode;
    /** Raw four-bit SDK selection value, 0..15; not a GPIO number or an array index limited to 0..3. */
    enabledAnt0: number;
    enabledAnt1: number;
  }
  interface WiFiAntennaGpio {
    selected: boolean;
    /** Raw seven-bit SDK GPIO number, 0..127, preserved even when selected is false.
     * This observation does not prove pin availability, routing or physical wiring. */
    gpio: number;
  }
  interface WiFiAntennaGpioConfig {
    /** Four entries in SDK switch-line order. */
    gpios: [WiFiAntennaGpio, WiFiAntennaGpio, WiFiAntennaGpio, WiFiAntennaGpio];
  }
  interface WiFiDriverRestartOptions {
    /** Shared native wait budget, integer 1..2147483647 ms, default 10000. SDK calls are not preempted. */
    timeoutMs?: number;
    /** Permit temporary AP activation to restore saved AP policy from off; default false. Final mode stays off. */
    allowApRestart?: boolean;
  }
  interface WiFiDriverRestartError extends NativeError {
    code: "WIFI_RESTART_FAILED";
    operation: "wifi.driver.restart";
    details: {
      stage: string;
      espCode: number;
      espName: string;
      lifecycleAdmitted: boolean;
      /** Entered checkpoint/STOP phase; does not prove an SDK STOP write was dispatched. */
      checkpointAttempted: boolean;
      replayAttempted: boolean;
      resumeAttempted: boolean;
      cleanupPending: boolean;
      restartRequired: boolean;
      restartSnapshotBytes: number;
      /** Current Radio fault, which may predate a rejected admission. */
      radioFaultStage: string | null;
      radioFaultError: number | null;
      /** Most recent native config result from this call; null before checkpoint. */
      configuration: WiFiConfigurationStatus | null;
    };
  }
  /** Same contract and converter as wifi.status().radio; independent observation. */
  type WiFiDriverStatus = WiFiRadioStatus;
  interface WiFiDriverCapabilities {
    apiVersion: "wifi-driver/1";
    target: string;
    idfVersion: string;
    /** Whether includeSecrets:true can be explicitly requested in this build. */
    secretReadback: boolean;
    stationOptions: WiFiCapabilities["stationOptions"];
    accessPointOptions: WiFiCapabilities["accessPointOptions"];
    /** Build availability and admission requirements, not live admission or RF proof.
     * idfSymbols lists principal controls; it is not an exhaustive transitive call graph. */
    operations: Array<{ jsPath: string; idfSymbols: string[]; available: boolean; stateRequirements: string[] }>;
    /** Accepted input keys, not a claim that every nondefault value is supported.
     * Consult target/build option flags and the operation's parameter contract. */
    configSchemas: { stationFields: string[]; accessPointFields: string[]; scanFields: string[]; txRateFields: string[] };
  }
  type WiFiAccessCategory = "voice" | "video" | "bestEffort" | "background";
  interface WiFiRxStatisticsConfig { ordinary: boolean; multiUser: boolean; }
  interface WiFiStatisticsConfig {
    rx: WiFiRxStatisticsConfig;
    tx: { voice: boolean; video: boolean; bestEffort: boolean; background: boolean };
  }
  interface WiFiScanParameters {
    /** 0..1500 ms; must not exceed the effective activeMaxMs. */
    activeMinMs: number;
    /** 0 selects 120 ms; otherwise 1..1500 ms. */
    activeMaxMs: number;
    /** 0 selects 360 ms; otherwise 1..1500 ms. */
    passiveMs: number;
    /** 0 selects 30 ms; otherwise 30..150 ms. */
    homeChannelDwellMs: number;
  }
  interface WiFiDriverModule {
    /** C5 only. Actual native enabled state, not counters or saved restart intent.
     * Requires started stable Radio; does not initialize Wi-Fi. */
    getStatisticsConfig(): WiFiStatisticsConfig;
    /** Complete RX pair replacement; both booleans required. Replaces native RX storage even
     * for equal values. Allocation failure leaves both RX kinds disabled; TX is unchanged.
     * Started Radio, idle helpers and exact framework owners required. Successful writes
     * precede JS conversion; after an exception inspect getStatisticsConfig before retrying. */
    configureRxStatistics(config: WiFiRxStatisticsConfig): WiFiRxStatisticsConfig;
    /** C5 only. Changes one ACI; enabling an already-enabled ACI keeps its counters.
     * Native allocation failure releases that ACI's partial buffers, preserving other ACIs.
     * Statistics storage is released before STOP. Managed START/restart restores enabled
     * configuration with fresh storage; shutdown/restore/device reboot discard the intent. */
    setTxStatistics(category: WiFiAccessCategory, enabled: boolean): boolean;
    /** Actual global defaults, also used by connection scans. Requires started stable Station. */
    getScanParameters(): WiFiScanParameters;
    /** Complete four-field replacement; null resets to SDK defaults. Idle framework helpers and
     * exact owners required; does not disconnect a connected Station. Returns actual readback.
     * RAM-only; preserved across managed start/restart, reset by deinit/restore/device reboot.
     * A JS conversion OOM may follow an accepted write; inspect getScanParameters before retrying. */
    setScanParameters(parameters: WiFiScanParameters | null): WiFiScanParameters;

    /** Build capability discovery in any Radio state; does not initialize Wi-Fi. */
    capabilities(): WiFiDriverCapabilities;
    /** Native ownership/fault/configuration observations; contains no credentials. */
    status(): WiFiDriverStatus;
    /** Explicit SDK defaults on initialized, fully stopped, zero-owner Radio.
     * Accepts healthy state or exact mode/protocol/bandwidth faults without unrelated cleanup.
     * Preserves driver ownership/generation and reselects the existing storage policy.
     * true means SDK loader acceptance and mode readback; NVS durability is not verified.
     * Failure preserves the first fault and failed suffix; explicit restore retries that suffix.
     * Clears only admitted faults after full acceptance; no rollback or automatic retry. */
    restore(): boolean;
    /** Independent actual SDK configuration; credentials are null by default.
     * includeSecrets:true requires CONFIG_ESP32_MQUICKJS_WIFI_ALLOW_SECRET_READBACK.
     * Does not initialize Wi-Fi; rejects native operation/lifecycle/fault transitions. */
    getInterfaceConfig(iface: WiFiInterface, options?: { includeSecrets?: boolean }): WiFiStationDriverConfigSnapshot | WiFiAccessPointDriverConfigSnapshot;
    /** Full replacement using constructor defaults for omitted fields. Requires an initialized,
     * stopped, zero-owner driver with the selected interface enabled; preserves mode/storage.
     * Returns actual readback with credentials hidden. Delivery OOM may follow a successful write. */
    setInterfaceConfig(iface: WiFiInterface, config: WiFiStationDriverConfig | WiFiAccessPointDriverConfig): WiFiStationDriverConfigSnapshot | WiFiAccessPointDriverConfigSnapshot;
    /** Candidate: reconstruct clean uninitialized or healthy initialized stopped Wi-Fi, with every owner exited.
     * Also accepts a healthy started, unassociated Enterprise Station with exact managed helper owners.
     * Retains and reinstalls its same profile/security policy before publishing new leases; active AP requires allowApRestart.
     * Failed Enterprise installation is cleared under the retained lifecycle before retry STOP/deinit.
     * Ordinary stop disables EAP; restart from stopped does not implicitly enable it.
     * Restores and starts its saved STA/AP/APSTA mode, without connecting Station or rearming RSSI.
     * Uses qualified STOP history when available; otherwise temporarily starts the configured
     * source in RAM and captures actual values. AP may advertise during this source START.
     * Clean uninitialized sources initialize/start Station in RAM without a predecessor snapshot.
     * Initialized off sources use temporary START, preserve configuration/storage and finish stopped/off.
     * Saved AP policy requires allowApRestart:true for temporary AP activation from off.
     * Explicit retry can reuse the same retained complete checkpoint after STOP/helper retirement.
     * Unproven init, incomplete checkpoints, native-pending or unknown required policy values reject.
     * Errors retain central cleanup; inspect status before retrying after an exception. */
    restart(options?: WiFiDriverRestartOptions): WiFiStatus;
    /** Initialized/stable Radio; snapshot of SDK-stored shared PHY settings. No implicit init or pin mutation. */
    getAntenna(): WiFiAntennaConfig;
    /** Independent observation, not atomic with getAntenna(); no GPIO reservation or configuration. */
    getAntennaGpio(): WiFiAntennaGpioConfig;
    /** Complete shared PHY configuration; initialized/stopped/zero Radio owners and BLE closed.
     * Returns verified SDK storage; applied at the next PHY enable. No implicit stop or NVS write. */
    setAntenna(config: WiFiAntennaConfig): WiFiAntennaConfig;
    /** Four complete switch-line entries. Selected pins must be unreserved, disabled GPIOs,
     * or belong to this antenna configuration. Replacing/removing a pin restores its original route.
     * Configuration/ownership survives Wi-Fi deinit and runtime restart; four unselected entries release it.
     * Failed rollback requires device reboot; an explicit external pin mutation is never overwritten. */
    setAntennaGpio(config: WiFiAntennaGpioConfig): WiFiAntennaGpioConfig;
    /** Initialized, fully stopped, zero-owner driver; off disables interfaces but does not deinitialize.
     * Uses the same mode strings as getMode/status. AP modes require SoftAP support.
     * Verifies SDK mode and rolls back a failed write; NVS restoration is not guaranteed. */
    setMode(mode: WiFiRadioMode): WiFiRadioMode;
    /** Initialized, fully stopped, zero-owner driver. Selects future configuration writes;
     * returns SDK acceptance, not a persistence flush. An uncertain write is repaired by
     * another explicit accepted selection; no implicit init, stop or credential copying. */
    setStorage(storage: "ram" | "flash"): "ram" | "flash";
    /** Explicit PMF disable on an existing eligible configuration, before START.
     * Fully stopped/zero owners; rejects WPA3/OWE and incompatible transition policy.
     * Verifies both flags and other fields; a failed mutation faults the Radio.
     * Subsequent configuration writes can re-enable PMF. */
    disablePmf(iface: WiFiInterface): void;
    /** Integer 0..65535; 0 selects default mode. Returns SDK-accepted value, no readback.
     * Requires stable initialized Radio with only framework owners; no live ESP-NOW Session. */
    setConnectionlessWakeInterval(milliseconds: number): number;
    /** Started driver, exact framework owners. Returns SDK acceptance; no getter or automatic rollback. */
    setDynamicCarrierSense(enabled: boolean): boolean;
    /** HE targets; started Station or APSTA and exact framework owners. Reports collisions
     * to the associated AP, not to a JS event queue. Returns SDK write acceptance.
     * Each accepted call clears the native collision bitmap; known policy replays after
     * START during managed restart. No native getter or automatic rollback. */
    setBssColorCollisionReporting(enabled: boolean): boolean;
    /** Enabled interface, initialized/fully stopped/zero owners. true disables 11b rates; false enables them. */
    configure11bRate(iface: WiFiInterface, disabled: boolean): boolean;
    /** Requires CONFIG_ESP_COEX_POWER_MANAGEMENT. Radio-wide policy; SDK acceptance only. */
    setCoexistencePowerManagement(enabled: boolean): boolean;
    /** Raw SDK event mask, including unknown bits; no implicit initialization. */
    getEventMask(): number;
    /** Only 0 (none) or 1 (AP probe requests); lifecycle/control bits are reserved. Verified readback. */
    setEventMask(mask: 0 | 1): number;
    /** Live SDK mode; initialized/stable driver, no implicit init. */
    getMode(): WiFiRadioMode;
    /** Live local regulatory settings; raw mask is not an authoritative allowlist. */
    getCountry(): WiFiCountryStatus;
    /** Complete country details on an initialized/stopped/zero-owner Radio, in any mode.
     * Returns actual SDK readback, including read-only maxTxPowerDbm. Country writes persist
     * independently of RAM/FLASH storage; verified rollback does not prove NVS restoration.
     * OOM while delivering success may occur after the country has already changed. */
    setCountryDetails(details: WiFiCountryDetails & { startChannel: number; channelCount: number }): WiFiCountryStatus;
    /** Callback-aware current channel observation; revision is not an operation identity. */
    getChannel(): WiFiChannelStatus;
    /** Started driver; SDK home channel, independent of the current-channel cache. No revision available. */
    getHomeChannel(): Omit<WiFiChannelStatus, "channelGeneration"> & { channelGeneration: null };
    /** Current SDK band; requires initialized, stable driver. */
    getBand(): WiFiBand;
    getBandMode(): WiFiBandMode;
    /** Started Station only, no other feature/fixed-channel owners. AUTO required; actual change requires unassociated STA. */
    setBand(band: WiFiBand): WiFiBand;
    /** Started Station only. SDK may change home channel; failed mutation faults Radio without guessed rollback. */
    setBandMode(mode: WiFiBandMode): WiFiBandMode;
    getPowerSave(): WiFiPowerSaveMode;
    /** Configured maximum in dBm, 0.25 dBm resolution; requires started driver. */
    getTxPower(): number;
    /** Latest beacon RSSI in dBm; requires associated Station, SDK failure is preserved. */
    getRssi(): number;
    /** SDK observation; 0 means not associated. Not an operation identity. */
    getAid(): number;
    /** Negotiated Station PHY, sampled independently of RSSI/AID. */
    getNegotiatedPhy(): WiFiNegotiatedPhy;
    /** SDK TSF microseconds; 0 unavailable. Not UTC; power save can affect accuracy. Exact integers only. */
    getTsfTime(iface: WiFiInterface): number;
    /** Current inactivity configuration in seconds; requires started interface. */
    getInactiveTime(iface: WiFiInterface): number;
    /** Started interface; exact framework owners, no other feature leases. FLASH may write NVS; rollback proves only the running value, not NVS or reversal of disconnect/deauth. */
    setInactiveTime(iface: WiFiInterface, seconds: number): number;
    /** Integer -100..10 dBm. SDK acceptance only; explicitly call again after each low-RSSI event. No automatic rearm. */
    setRssiThreshold(dbm: number): number;
    /** Initialized/stopped/zero-owner only. Preserves bandwidth, verifies readback, rolls back on failure. */
    setProtocol(iface: WiFiInterface, protocols: WiFiProtocol[]): WiFiProtocol[];
    /** Replaces supplied active-band sets; preserves omitted bands. No maximum-protocol shorthand. */
    setProtocols(iface: WiFiInterface, config: WiFiProtocolConfig): WiFiProtocolConfig;
    /** Initialized/stopped/zero-owner only. Preserves protocols; rejects incompatible 40 MHz. */
    setBandwidth(iface: WiFiInterface, mhz: 20 | 40): 20 | 40;
    /** Replaces supplied active-band widths; preserves omitted bands. */
    setBandwidths(iface: WiFiInterface, config: WiFiBandwidthConfig): WiFiBandwidthConfig;
    /** Live configured protocol set; SDK rejects dual-band AUTO. Does not initialize Wi-Fi. */
    getProtocol(iface: WiFiInterface): WiFiProtocol[];
    /** Live per-band configuration. Inactive/unsupported band properties are omitted. */
    getProtocols(iface: WiFiInterface): WiFiProtocolConfig;
    /** Configured MHz, not negotiated width; SDK rejects dual-band AUTO. */
    getBandwidth(iface: WiFiInterface): 20 | 40;
    /** Live per-band configuration. Inactive/unsupported band properties are omitted. */
    getBandwidths(iface: WiFiInterface): WiFiBandwidthConfig;
    /** Initialized, stopped, owner-free only. Explicit write; no implicit init/stop.
     * LR requires this interface's enabled 2.4 GHz LR protocol. Fresh dual-band
     * AUTO common PHYs require equivalent contexts or an explicitly selected band. */
    configureTxRate(iface: WiFiInterface, config: WiFiTxRateConfig): WiFiTxRateStatus;
    /** Side-effect-free framework record, available even during a Radio fault. */
    txRateStatus(iface: WiFiInterface): WiFiTxRateStatus;
  }

  type WiFiVendorIeFrame = "beacon" | "probe-request" | "probe-response" | "association-request" | "association-response";
  type WiFiVendorIeOptions = (
    | { interface: "station"; frame: "probe-request" | "association-request" }
    | { interface: "access-point"; frame: "beacon" | "probe-response" | "association-response" }
  ) & { index: 0 | 1 } & (
    | { enabled: true; data: ByteSource }
    | { enabled: false; data?: never }
  );
  interface WiFiVendorIeSlot {
    interface: WiFiInterface;
    frame: WiFiVendorIeFrame;
    index: 0 | 1;
    state: "empty" | "enabled" | "uncertain";
    /** Complete IE bytes, 0 for empty; null when the SDK outcome is uncertain. */
    byteLength: number | null;
    espCode: number | null;
  }
  interface WiFiVendorIeWatchOptions {
    /** Omit to accept all; otherwise 1..8 unique colon-separated three-byte OUIs. */
    oui?: string | string[];
    /** Integer 1..32, default 8; drop newest on full. */
    capacity?: number;
  }
  interface WiFiVendorIeEvent {
    /** Boot-scoped exact sequence; gaps include filtered/dropped observations. */
    sequence: number;
    /** Local esp_timer time at callback capture, not an RF timestamp. */
    timestampUs: number;
    radioGeneration: number;
    frame: WiFiVendorIeFrame;
    sourceMac: string;
    rssi: number;
    oui: string;
    vendorType: number;
    /** Full copied IE including 0xdd and length, 6..257 bytes. */
    data: number[];
  }
  interface WiFiVendorIeWatchStatus {
    subscribers: number;
    /** Includes closed handles until native queue destruction. */
    retainedHandles: number;
    reservedCapacity: number;
    eventBytes: number;
    capacityLimit: 64;
    subscriberLimit: 4;
    retainedHandleLimit: 8;
    sequence: number;
    sequenceExhausted: boolean;
    /** Saturating boot counters; filtered and droppedQueue count per subscriber. */
    droppedBusy: number;
    invalid: number;
    filtered: number;
    droppedQueue: number;
    /** Zero when the physical callback broker is absent. */
    radioGeneration: number;
    registered: boolean;
    registrationUncertain: boolean;
    unregisterWritten: boolean;
    callbacksActive: number;
    registrationError: number | null;
  }
  interface WiFiVendorIeStatus {
    radioGeneration: number;
    owners: number;
    /** Exact start lifecycle owns parked driver copies while ordinary leases are absent. */
    startPending: boolean;
    /** Framework-owned IE payload only; excludes SDK metadata and allocator overhead. */
    driverPayloadBytesUpperBound: number;
    /** Ten slots, or four with SoftAP disabled. No claim about external SDK writers or RF delivery. */
    slots: WiFiVendorIeSlot[];
    watch: WiFiVendorIeWatchStatus;
  }
  interface WiFiVendorIeError extends Error {
    code: "WIFI_VENDOR_IE_FAILED";
    operation: "wifi.vendorIe.set" | "wifi.vendorIe.clear";
    details: { espCode: number; espName: string; status: WiFiVendorIeStatus };
  }
  interface WiFiVendorIeModule {
    /** Existing healthy configured interface required; started or fully stopped. 6..257 bytes: 0xdd, length, OUI[3], type, payload.
     * Occupied/uncertain slots require explicit clear before setting again. SDK copies input.
     * Enabled or uncertain slots retain Radio owners; clear before stop/configure/restart.
     * wifi.start() preserves known pre-start slots only with the same mode and storage.
     * Result allocation can fail after success: inspect status before retrying. */
    set(options: WiFiVendorIeOptions): WiFiVendorIeStatus;
    /** Clear framework-owned slots; errors retain only unfinished slots for retry. Omit for both interfaces. */
    clear(interface?: WiFiInterface): void;
    status(): WiFiVendorIeStatus;
    /** Observation only: no Radio start or RF owner; queue survives ordinary driver stop/restart. */
    watch(options?: WiFiVendorIeWatchOptions): EventQueue<WiFiVendorIeEvent>;
  }

  type WiFiBtmQueryReason = "unspecified" | "frame-loss" | "delay" | "bandwidth" | "load-balance"
    | "rssi" | "retransmissions" | "interference" | "gray-zone" | "premium-ap";
  interface WiFiBtmCandidate {
    /** Distinct, nonzero unicast MAC address in xx:xx:xx:xx:xx:xx form. */
    bssid: string;
    /** Unsigned 32-bit information field; all bits are preserved. */
    bssidInformation: number;
    /** Integer 1–255. These are reported candidates, not local channel writes. */
    operatingClass: number;
    /** Integer 1–233. */
    channel: number;
    /** Integer 0–255. */
    phyType: number;
    /** Optional integer 0–255; omitted does not insert a preference subelement. */
    preference?: number;
  }
  interface WiFiBtmQueryOptions {
    /** Default unspecified. */
    reason?: WiFiBtmQueryReason;
    /** At most 16 entries; default empty. No implicit supplicant scan-cache list. */
    candidates?: WiFiBtmCandidate[];
    /** Default false. APSTA requires explicit permission for later AP channel movement. */
    allowApChannelChange?: boolean;
  }
  interface WiFiBtmQueryResult {
    /** SDK submission succeeded; RF TX, response and completed roaming are not implied. */
    accepted: true;
    completion: "sdk-submit";
    reason: WiFiBtmQueryReason;
    candidateCount: number;
  }
  interface WiFiRoamingCapabilities {
    apiVersion: "wifi-roaming/1";
    stability: "candidate";
    target: string;
    /** Build support; not a statement about the current AP. */
    rrm11k: boolean;
    btm11v: boolean;
    fastTransition11r: boolean;
    maximumBtmCandidates: number;
  }
  interface WiFiRoamingError extends NativeError {
    code: "WIFI_ROAMING_FAILED";
    operation: "wifi.roaming.isRrmSupported" | "wifi.roaming.isBtmSupported" | "wifi.roaming.sendBtmQuery";
    details: {
      stage: string; espCode: number; espName: string; radioGeneration: number | null;
      nativeEntered: boolean; submissionAttempted: boolean; sdkCode: number | null;
    };
  }
  type WiFiRoamingEventName = "WIFI_EVENT_STA_START" | "WIFI_EVENT_STA_STOP"
    | "WIFI_EVENT_STA_CONNECTED" | "WIFI_EVENT_STA_DISCONNECTED" | "WIFI_EVENT_STA_AUTHMODE_CHANGE"
    | "WIFI_EVENT_STA_BSS_RSSI_LOW" | "WIFI_EVENT_STA_BEACON_TIMEOUT" | "WIFI_EVENT_HOME_CHANNEL_CHANGE"
    | "WIFI_EVENT_STA_NEIGHBOR_REP" | "WIFI_EVENT_STA_BEACON_OFFSET_UNSTABLE";
  /** Link/channel observations; does not imply BTM acceptance or completed roaming. */
  type WiFiRoamingEvent = WiFiEvent & { name: WiFiRoamingEventName };
  interface WiFiRoamingWatchOptions {
    /** Default all roaming observations. Neighbor Report requires RRM in this build. */
    events?: WiFiRoamingEventName[] | "all";
    /** Integer 1..64, default 16. Shares the four-subscriber wifi.watch budget. */
    capacity?: number;
    overflow?: "drop-newest";
  }
  interface WiFiRoamingModule {
    /** Observation only; no Radio start or lease. Close the returned queue to release its subscription. */
    watch(options?: WiFiRoamingWatchOptions): EventQueue<WiFiRoamingEvent>;
    capabilities(): WiFiRoamingCapabilities;
    /** Only registered when RRM is enabled. False when not associated; does not start Wi-Fi. */
    isRrmSupported?(): boolean;
    /** RRM only, associated Station with rmEnabled. Native-driver Future; SDK submission opens the handle. */
    requestNeighborReport?(options?: { timeoutMs?: number; maxReportBytes?: number }): WiFiNeighborReportRequest;
    /** RRM request resource/cleanup observations, also available after an opening Future fails. */
    status?(): { handles: number; reservedBytes: number; workerBusy: boolean; callbackBusy: boolean; activeRequest: WiFiNeighborReportStatus | null };
    /** Only registered when WNM is enabled. False when not associated. */
    isBtmSupported?(): boolean;
    /** WNM only; Station btmEnabled must already be true. Capture/result allocation precedes submission. */
    sendBtmQuery?(options: WiFiBtmQueryOptions): WiFiBtmQueryResult;
  }

  interface WiFiNeighborReportStatus {
    terminal: "pending" | "report" | "no-report" | "failed" | "cancelled" | "timed-out";
    started: boolean; closed: boolean; retired: boolean; cleanupPending: boolean;
    sequence: number | null; radioGeneration: number | null;
    nativeEntered: boolean; transmissionAttempted: boolean; sdkCode: number | null;
    callbackSeen: boolean; cancelWritten: boolean;
    reportBytes: number; reportCapacity: number; retainedBytes: number;
    error: number | null; stage: string | null; cleanupError: number | null; cleanupStage: string | null;
  }
  interface WiFiNeighborReport {
    sequence: number; radioGeneration: number;
    /** SDK dialog-token matching does not prove RF freshness after token wrap/reset. */
    correlation: "sdk-dialog-token";
    dialogToken: number; reportLength: number; skippedElements: number;
    neighbors: WiFiNeighborReportEntry[];
  }
  interface WiFiNeighborReportRequest {
    status(): WiFiNeighborReportStatus;
    /** Native-driver Future. Deadline/cancellation requests native cancel and preserves unfinished cleanup. */
    receive(options?: { timeoutMs?: number }): WiFiNeighborReport;
    /** Synchronous intent; does not wait for native retirement. */
    cancel(): void;
    /** Revokes report access immediately; status exposes remaining cleanup. */
    close(): void;
  }
  interface WiFiNeighborReportError extends NativeError {
    code: "WIFI_NEIGHBOR_FAILED" | "WIFI_NEIGHBOR_CLOSED" | "WIFI_NEIGHBOR_TIMEOUT" | "WIFI_NEIGHBOR_INVALID_REPORT";
    operation: "wifi.roaming.requestNeighborReport" | "WiFiNeighborReportRequest.receive";
    details: WiFiNeighborReportStatus & { operationError: number | null };
  }

  type WiFiEnterpriseMethod = "tls" | "ttls" | "peap" | "fast";
  interface WiFiEnterpriseOptions {
    methods: WiFiEnterpriseMethod[];
    anonymousIdentity?: string | ByteSource;
    username?: string | ByteSource;
    password?: string | ByteSource;
    newPassword?: string | ByteSource;
    caCertificate?: string | ByteSource;
    clientCertificate?: string | ByteSource;
    privateKey?: string | ByteSource;
    privateKeyPassword?: string | ByteSource;
    pac?: string | ByteSource;
    domain?: string | ByteSource;
    ttlsPhase2?: "eap" | "mschapv2" | "mschap" | "pap" | "chap";
    checkCertificateTime?: boolean;
    suiteB192?: boolean;
    defaultCertificateBundle?: boolean;
    okc?: boolean;
    fast?: {
      provisioning?: "disabled" | "unauthenticated" | "authenticated";
      maximumPacEntries?: number;
      binaryPac?: boolean;
    };
  }
  interface WiFiEnterpriseConfiguration {
    configured: true;
    /** Exact decimal uint64 revision; never rounded through a JS number. */
    revision: string;
  }
  interface WiFiEnterpriseCapabilities {
    apiVersion: "wifi-enterprise/1";
    stability: "candidate";
    target: string;
    methods: WiFiEnterpriseMethod[];
    domain: boolean;
    suiteB192: boolean;
    defaultCertificateBundle: boolean;
    maximumProfiles: number;
    maximumProfileBytes: number;
    maximumRetainedBytes: number;
  }
  interface WiFiEnterpriseStatus {
    configured: boolean;
    revision: string;
    busy: boolean;
    closing: boolean;
    revisionExhausted: boolean;
    identityExhausted: boolean;
    /** Unknown while a control is active. */
    binding: boolean | null;
    enabled: boolean | null;
    cleanupPending: boolean | null;
    profiles: number;
    /** Profile allocations including metadata and padding; excludes SDK copies. */
    reservedBytes: number;
    nativeObserved: boolean;
    nativeResources: number | null;
    /** Actual owned SDK observation; null without a binding, while busy, or if unavailable.
     * This does not prove a certificate or the device clock was validated. */
    checkCertificateTime: boolean | null;
    error: number | null;
    cleanupError: number | null;
    controlError: number | null;
    stage: string | null;
  }
  interface WiFiEnterpriseControlOptions { timeoutMs?: number; }
  interface WiFiEnterpriseModule {
    capabilities(): WiFiEnterpriseCapabilities;
    /** Captures secrets and allocates the commit result before replacing configuration. */
    configure(options: WiFiEnterpriseOptions): WiFiEnterpriseConfiguration;
    status(): WiFiEnterpriseStatus;
    /** Native Future. Does not start Wi-Fi or associate; requires idle started Station. */
    enable(options?: WiFiEnterpriseControlOptions): WiFiEnterpriseStatus;
    /** Native Future. Disconnects this Station before retiring SDK credentials; preserves profile. */
    disable(options?: WiFiEnterpriseControlOptions): WiFiEnterpriseStatus;
    /** Native Future. Retires SDK credentials, then releases the configured profile. */
    clear(options?: WiFiEnterpriseControlOptions): WiFiEnterpriseStatus;
  }
  interface WiFiEnterpriseError extends NativeError {
    code: "WIFI_ENTERPRISE_FAILED" | "WIFI_ENTERPRISE_TIMEOUT";
    operation: "wifi.enterprise.configure" | "wifi.enterprise.enable" | "wifi.enterprise.disable" | "wifi.enterprise.clear";
    details: { stage: string; espCode: number; nativePending: boolean };
  }

  type WiFiTwtProbeStatus = "success" | "failed" | "timeout" | "disconnected" | "unknown";
  interface WiFiTwtProbeOptions {
    /** SDK response-time estimate in ms, integer 1..60000, default 5000. */
    responseTimeoutMs?: number;
    /** Future deadline in ms, integer 1..60000, default 6000. Includes scheduling. */
    timeoutMs?: number;
  }
  interface WiFiIndividualTwtOptions {
    command?: "request" | "suggest" | "demand";
    flowId?: number;
    /** Boot-scoped monotonic ID, 0..32767. Omit for automatic allocation. */
    connectionId?: number;
    trigger?: boolean;
    announced?: boolean;
    wakeDurationUnit?: "256us" | "1024us";
    minimumWakeDuration?: number;
    wakeIntervalMantissa?: number;
    wakeIntervalExponent?: number;
    responseTimeoutMs?: number;
    timeoutMs?: number;
  }
  interface WiFiBroadcastTwtOptions {
    command?: "request" | "suggest" | "demand";
    /** Integer 1..31. One managed owner per ID until full native retirement. */
    broadcastId: number;
    /** Integer 1..65535 ms, default 5000. */
    responseTimeoutMs?: number;
    /** Public deadline, integer 1..60000 ms, default 6000. */
    timeoutMs?: number;
  }
  type WiFiTwtAgreementStatus = WiFiIndividualTwtAgreementStatus | WiFiBroadcastTwtAgreementStatus;
  interface WiFiBroadcastTwtAgreementStatus {
    state: "pending" | "active" | "failed" | "closing" | "closed";
    kind: "broadcast";
    broadcastId: number;
    radioGeneration: number;
    sequence: number | null;
    nativeStatusId: number | null;
    reason: number | null;
    ambiguous: boolean;
    nativeClosed: boolean;
    setupCommandId: number;
    trigger: boolean | null;
    announced: boolean | null;
    /** SDK completion does not supply the duration unit. */
    wakeDurationUnit: null;
    minimumWakeDuration: number | null;
    wakeIntervalMantissa: number | null;
    wakeIntervalExponent: number | null;
    targetWakeTimeUs: string | null;
    submitError: number | null;
    driverError: number | null;
    handoffError: number | null;
    nativeError: number | null;
    observationError: number | null;
    teardownError: number | null;
    teardownStatusId: number | null;
    teardownObservationError: number | null;
    /** Remaining non-reused wire dialogs for this ID in the physical boot, 0..255. */
    dialogAttemptsRemaining: number;
    cleanupPending: boolean;
    cleanupError: number | null;
    cleanupStage: string | null;
  }
  interface WiFiIndividualTwtAgreementStatus {
    state: "pending" | "active" | "failed" | "closing" | "closed";
    kind: "individual";
    /** Original connection authority was revoked; native retirement may still be pending. */
    nativeClosed: boolean;
    connectionId: number;
    radioGeneration: number;
    sequence: number | null;
    flowId: number | null;
    requestedFlowId: number;
    nativeStatusId: number | null;
    reason: number | null;
    ambiguous: boolean;
    setupCommandId: number;
    trigger: boolean;
    announced: boolean;
    wakeDurationUnit: "256us" | "1024us";
    minimumWakeDuration: number;
    wakeIntervalMantissa: number;
    wakeIntervalExponent: number;
    /** Exact unsigned 64-bit SDK timestamp, decimal string. */
    targetWakeTimeUs: string | null;
    submitError: number | null;
    driverError: number | null;
    handoffError: number | null;
    observationError: number | null;
    teardownStatusId: number | null;
    teardownError: number | null;
    teardownObservationError: number | null;
    cleanupPending: boolean;
    cleanupError: number | null;
    cleanupStage: string | null;
  }
  interface WiFiTwtAgreement {
    /** Closed objects retain their last observed setup snapshot. */
    status(): WiFiTwtAgreementStatus;
    /** Native-driver Future; waits for local native retirement. Timeout leaves cleanup active. */
    close(options?: { timeoutMs?: number }): void;
    /** Individual agreements only; broadcast handles reject this operation.
     * Native-driver Future. durationMs 0 suspends indefinitely; 1..4294967 schedules
     * native automatic resume. Timeout/cancel does not undo an accepted suspension. */
    suspend(options: { durationMs: number; timeoutMs?: number }): void;
    /** Individual agreements only; broadcast handles reject this operation.
     * Native-driver Future. Sends the earliest negotiated wake time and waits
     * for the matching local resume timer to finish. Timeout leaves it pending. */
    resume(options?: { timeoutMs?: number }): void;
  }
  interface WiFiTwtInformationStatus {
    operation: "suspend" | "resume";
    sequence: number;
    agreementSequence: number;
    flowId: number;
    durationMs: number;
    dispatching: boolean;
    complete: boolean;
    txComplete: boolean;
    resumeComplete: boolean;
    cleanupPending: boolean;
    submitError: number | null;
    nativeError: number | null;
    observationError: number | null;
    cleanupError: number | null;
  }
  interface WiFiTwtAgreementError extends NativeError {
    code: "WIFI_TWT_AGREEMENT_FAILED" | "WIFI_TWT_AGREEMENT_TIMEOUT";
    operation: "wifi.twt.setupIndividual" | "wifi.twt.setupBroadcast" | "WiFiTwtAgreement.close" | "WiFiTwtAgreement.suspend" | "WiFiTwtAgreement.resume";
    details: {
      espCode: number;
      espName: string;
      stage: "setup" | "close" | "suspend" | "resume" | "deadline";
      informationSequence: number | null;
      informationError: number | null;
      nativeStatusId: number | null;
      reason: number | null;
      sdkError: number | null;
      driverError: number | null;
      handoffError: number | null;
    };
  }
  interface WiFiTwtProbeResult {
    /** Boot-scoped native operation identity; never reused. */
    sequence: number;
    radioGeneration: number;
    status: WiFiTwtProbeStatus;
    /** SDK reason byte, normalized to zero for success. */
    reason: number;
    /** AP Beacon/Probe Response observation, without an RF request cookie. */
    correlation: "associated-ap-liveness";
  }
  interface WiFiTwtCapabilities {
    flowStatus: true;
    targetWakeTimeOffset: true;
    configure: true;
    apiVersion: "wifi-twt/1";
    stability: "candidate";
    target: "esp32c5";
    probe: true;
    maximumProbes: 1;
    broadcastDiscovery: true;
    maximumBroadcastSchedules: 32;
    setupBroadcast: true;
    maximumBroadcastAgreements: 31;
    maximumBroadcastAttemptsPerId: 255;
    setupIndividual: true;
    closeAll: true;
    recover: true;
    maximumIndividualAgreements: 8;
    suspendIndividual: true;
    resumeIndividual: true;
    maximumInformationOperations: 1;
    maximumSuspendDurationMs: 4294967;
    maximumResponseTimeoutMs: 60000;
    maximumTimeoutMs: 60000;
  }
  interface WiFiTwtStatus {
    /** Current information operation, including retained TX cleanup; null when retired.
     * This is an operation result, not the current power state or an RF acknowledgement. */
    information: WiFiTwtInformationStatus | null;
    operationActive: boolean;
    sequence: number | null;
    radioGeneration: number | null;
    dispatching: boolean;
    nativeOwned: boolean;
    nativeStatus: WiFiTwtProbeStatus | null;
    nativeReason: number | null;
    nativeError: number | null;
    submitError: number | null;
    observationError: number | null;
    cancelRequested: boolean;
    cancelComplete: boolean;
    cleanupPending: boolean;
    cleanupError: number | null;
    cleanupStage: string | null;
  }
  interface WiFiBroadcastTwtSchedule {
    /** AP-advertised ID, 0..31; discovery does not establish an Agreement. */
    broadcastId: number;
    /** Native joined bitmap at capture time; does not confer a framework owner. */
    joined: boolean;
    trigger: boolean;
    announced: boolean;
    /** Raw three-bit recommendation, including reserved values. */
    recommendation: number;
    /** Raw eight-bit duration. The SDK getter does not return its unit. */
    minimumWakeDuration: number;
    wakeDurationUnit: null;
    wakeIntervalMantissa: number;
    wakeIntervalExponent: number;
    /** Exact integer mantissa * 2^exponent, in microseconds. */
    wakeIntervalUs: number;
    /** Raw advertised persistence in TBTTs; not an elapsed-time countdown. */
    persistence: number;
  }
  interface WiFiBroadcastTwtSnapshot {
    radioGeneration: number;
    /** Up to 32 copied advertisements from the current associated AP. */
    schedules: WiFiBroadcastTwtSchedule[];
  }
  interface WiFiTwtDiscoveryOptions {
    /** Public wait deadline, 1..60000 ms; default 6000. No RF timeout. */
    timeoutMs?: number;
  }
  interface WiFiTwtDiscoveryError extends NativeError {
    code: "WIFI_TWT_DISCOVERY_FAILED" | "WIFI_TWT_DISCOVERY_TIMEOUT";
    operation: "wifi.twt.broadcasts";
    details: { espCode: number; espName: string; stage: "snapshot" | "deadline" };
  }
  interface WiFiTwtCloseError extends NativeError {
    code: "WIFI_TWT_CLOSE_TIMEOUT";
    operation: "wifi.twt.closeAll";
    details: { started: boolean; selected: number; pending: number };
  }
  interface WiFiTwtRecoveryOptions {
    /** Current Radio generation. Recovery closes every managed TWT operation in it. */
    radioGeneration: number;
    closeAll: true;
    allowDisconnect: true;
    /** Scheduled wait deadline, 1..60000 ms, default 10000. */
    timeoutMs?: number;
  }
  interface WiFiTwtRecoveryResult {
    previousRadioGeneration: number;
    radioGeneration: number;
  }
  interface WiFiTwtRecoveryError extends NativeError {
    code: "WIFI_TWT_RECOVERY_FAILED" | "WIFI_TWT_RECOVERY_TIMEOUT";
    operation: "wifi.twt.recover";
    details: { espCode: number; espName: string; stage: string; radioGeneration: number;
      lifecycleAdmitted: boolean; checkpointAttempted: boolean; replayAttempted: boolean; resumeAttempted: boolean;
      cleanupPending: boolean; restartRequired: boolean; radioFaultStage: string | null; radioFaultError: number | null };
  }
  interface WiFiTwtConfig { postWakeupEvents: boolean; keepAlive: boolean; }
  interface WiFiTwtFlowStatus { radioGeneration: number; bitmap: number; }
  interface WiFiTwtControlError extends NativeError {
    code: "WIFI_TWT_CONTROL_FAILED";
    operation: "wifi.twt.getConfig" | "wifi.twt.configure" | "wifi.twt.getFlowStatus" | "wifi.twt.setTargetWakeTimeOffset";
    details: { espCode: number; espName: string; stage: string | null; mutationAttempted: boolean };
  }
  interface WiFiTwtModule {
    /** Actual native shared policy on initialized stable STA/APSTA Radio. No implicit init. */
    getConfig(): WiFiTwtConfig;
    /** Full replacement; both booleans required. Affects all managed TWT agreements.
     * Idle helpers and exact framework owners required; existing managed TWT owners allowed.
     * Returns actual native readback. Preserved by managed START/restart; driver restore
     * clears the saved intent. A JS return allocation failure may follow an accepted write. */
    configure(config: WiFiTwtConfig): WiFiTwtConfig;
    /** Associated started Station only. Eight-bit native established-flow observation,
     * including unmanaged native flows; does not create handles or confer ownership. */
    getFlowStatus(): WiFiTwtFlowStatus;
    /** Integer 0..102400 microseconds relative to TBTT in generated iTWT setup requests.
     * Requires current association. Affects the current native AP node only, including
     * subsequently generated retry frames; never replayed onto a new association/restart.
     * Does not reschedule already-negotiated agreements or guarantee physical wake timing. */
    setTargetWakeTimeOffset(offsetUs: number): number;
    /** Native-driver Future. Explicit whole-generation recovery from a readable,
     * healthy started Radio. Disconnects Station/stops AP, drains original TWT
     * owners before deinit and replays configuration. Does not renegotiate TWT.
     * Timeout/cancel leaves central cleanup, without promising replay. */
    recover(options: WiFiTwtRecoveryOptions): WiFiTwtRecoveryResult;
    /** Native-driver Future. Atomically selects current managed individual and
     * broadcast owners when started and waits for their local retirement.
     * Later owners and probe are excluded. Timeout/cancel leaves cleanup active. */
    closeAll(options?: { timeoutMs?: number }): void;
    /** Native-driver Future. Copies cached AP advertisements in the Wi-Fi task;
     * requires associated Station, sends no probe and creates no Agreement. */
    broadcasts(options?: WiFiTwtDiscoveryOptions): WiFiBroadcastTwtSnapshot;
    /** Native-driver Future. Requires associated Station and retains its own Radio lease. */
    setupIndividual(options: WiFiIndividualTwtOptions): WiFiTwtAgreement;
    /** Native-driver Future. Requires associated Station and explicit modem sleep.
     * Each broadcast ID has at most 255 wire attempts per physical boot. */
    setupBroadcast(options: WiFiBroadcastTwtOptions): WiFiTwtAgreement;
    /** Includes pending and closing owners even after their public Future ended. */
    agreements(): WiFiTwtAgreementStatus[];
    capabilities(): WiFiTwtCapabilities;
    status(): WiFiTwtStatus;
    /** Native Future. Requires started, associated Station. Timeout cancels only this probe;
     * native cleanup retains Radio until proven drained. Does not establish a TWT agreement. */
    probe(options?: WiFiTwtProbeOptions): WiFiTwtProbeResult;
  }
  interface WiFiTwtError extends NativeError {
    code: "WIFI_TWT_PROBE_FAILED" | "WIFI_TWT_TIMEOUT";
    operation: "wifi.twt.probe";
    details: {
      espCode: number;
      espName: string;
      stage: string | null;
      nativeStatus: WiFiTwtProbeStatus | null;
      nativeReason: number | null;
    };
  }

  type WiFiSmartConfigProtocol = "esptouch" | "airkiss" | "esptouch-airkiss" | "esptouch-v2";
  interface WiFiSmartConfigOptions {
    /** Default esptouch. */
    protocol?: WiFiSmartConfigProtocol;
    /** Explicit automatic connection and local phone ACK flow; default false. */
    autoConnect?: boolean;
    /** autoConnect only, integer 1..3600000 ms; default 30000. */
    connectionTimeoutMs?: number;
    /** autoConnect only. Empty password is rejected unless true; default false. */
    allowOpenNetwork?: boolean;
    /** autoConnect only; default wpa2-psk. WPA3 requires the SDK SAE build option. */
    minimumAuthMode?: "wpa2-psk" | "wpa3-psk";
    /** autoConnect only; default optional (required for WPA3). No disabled mode. */
    pmf?: "optional" | "required";
    /** Default false. */
    fastMode?: boolean;
    /** SDK find-channel timeout, integer 15..255 seconds; default 15. */
    channelTimeoutSeconds?: number;
    /** esptouch-v2 only: exactly 16 non-NUL UTF-8 bytes, copied before activation. */
    aesKey?: string;
    /** Acquisition/handoff deadline; with autoConnect includes connection and local ACK, 1..3600000 ms; default 120000. */
    timeoutMs?: number;
    /** Explicitly permit APSTA channel interruption during decoding; default false. */
    allowApChannelChange?: boolean;
  }
  interface WiFiSmartConfigWaitOptions {
    /** Scheduled wait budget 1..3600000 ms, default 1000. Native SDK calls are not preempted. */
    timeoutMs?: number;
  }
  interface WiFiSmartConfigCredentials {
    /** SDK C-string bytes through first NUL, or all 32 bytes. No UTF-8 conversion. */
    ssidBytes: number[];
    /** Secret SDK C-string bytes through first NUL, or all 64 bytes. Never log this result. */
    passwordBytes: number[];
    /** Exact ESPTouch v2 bytes, including embedded NUL, length 0..64; null for other protocols. Secret. */
    customDataBytes: number[] | null;
    protocol: WiFiSmartConfigProtocol;
    bssid: string | null;
  }
  interface WiFiSmartConfigStatus {
    state: "opening" | "listening" | "credentials-ready" | "connecting" | "completed" | "closing" | "closed" | "faulted";
    operation: number | null;
    radioGeneration: number | null;
    /** Exact unsigned 64-bit decoder identity, not a lossy JS number. */
    decoderIdentity: { low: number; high: number } | null;
    autoConnect: boolean;
    connectionGeneration: number | null;
    connectionStarted: boolean;
    /** Successful connection and ACK transferred to application; close no longer disconnects it. */
    connectionTransferred: boolean;
    connectionReason: number;
    ackRequested: boolean;
    /** Local UDP submission completed, not proof of phone receipt. */
    ackCompleted: boolean;
    /** Automatic flow and native cleanup complete; credential result may still be unread. */
    completed: boolean;
    workerBusy: boolean;
    driverStarted: boolean;
    scanDone: boolean;
    channelFound: boolean;
    credentialsReady: boolean;
    credentialsConsumed: boolean;
    captureStopped: boolean;
    channelRestored: boolean;
    eventFenced: boolean;
    homeChannel: number | null;
    closeRequested: boolean;
    cleanupPending: boolean;
    timedOut: boolean;
    handoffUnknown: boolean;
    /** Session allocation only; SDK/timer/event allocations are additional. */
    reservedBytes: number;
    capturedEvents: number;
    duplicateCredentials: number;
    discardedEvents: number;
    error: number | null;
    stage: string | null;
    cleanupError: number | null;
    cleanupStage: string | null;
  }
  interface WiFiSmartConfigGlobalStatus {
    active: boolean;
    handles: number;
    workers: number;
    runtimeClosing: boolean;
    session: WiFiSmartConfigStatus | null;
  }
  interface WiFiSmartConfigCapabilities {
    apiVersion: "wifi-smartconfig/1";
    stability: "candidate";
    /** Third-party SDK SmartConfig version. */
    version: string;
    protocols: WiFiSmartConfigProtocol[];
    espTouchV2: true;
    encryptedV2: true;
    autoConnect: true;
    acknowledgement: true;
    customData: true;
    observations: true;
    maxWatchQueues: 4;
    maxWatchCapacity: 16;
    maxSessions: 4;
    maxActiveSessions: 1;
  }
  interface WiFiSmartConfigSession {
    status(): WiFiSmartConfigStatus;
    /** One observer per Session; initial and changed metadata snapshots, drop-newest. Keeps Session alive until queue close/GC. */
    watch(options?: WiFiSmartConfigWatchOptions): EventQueue<WiFiSmartConfigObservation>;
    /** Future-capable one-time credential transfer; autoConnect waits for completed flow. Wait expiry returns null. */
    receive(options?: WiFiSmartConfigWaitOptions): WiFiSmartConfigCredentials | null;
    /** Future-capable, idempotent. Revokes credential delivery and waits for full native cleanup. */
    close(options?: WiFiSmartConfigWaitOptions): void;
  }
  interface WiFiSmartConfigWatchOptions {
    /** Integer 1..16, default 8. Four retained queues globally, including closed buffers awaiting GC. */
    capacity?: number;
  }
  interface WiFiSmartConfigObservation {
    /** Per-queue nonwrapping sequence; gaps indicate dropped snapshots. */
    sequence: number;
    /** Sampled metadata only. Intermediate changes can coalesce; workerBusy alone does not emit. */
    status: WiFiSmartConfigStatus;
  }
  interface WiFiSmartConfigModule {
    capabilities(): WiFiSmartConfigCapabilities;
    status(): WiFiSmartConfigGlobalStatus;
    /** Synchronously reserves a Session and schedules native start; inspect status/receive for acceptance. */
    start(options: WiFiSmartConfigOptions): WiFiSmartConfigSession;
  }
  interface WiFiSmartConfigError extends Error {
    code: "WIFI_SMARTCONFIG_FAILED" | "WIFI_SMARTCONFIG_TIMEOUT" | "WIFI_SMARTCONFIG_CLOSED";
    operation: "wifi.smartConfig.start" | "WiFiSmartConfigSession.receive" | "WiFiSmartConfigSession.close";
    details: WiFiSmartConfigStatus & { espCode: number; espName: string; waitTimedOut: boolean };
  }

  interface WiFiWpsOptions {
    /** Station enrollee method; default pbc. */
    method?: "pbc" | "pin";
    /** PIN method only: eight ASCII digits with WPS checksum. Absent or 00000000 asks SDK to generate one. Secret. */
    pin?: string;
    device?: {
      /** Up to 64 UTF-8 bytes, no NUL. Empty/absent uses SDK default. */
      manufacturer?: string;
      /** Up to 32 UTF-8 bytes, no NUL. Empty/absent uses SDK default. */
      modelNumber?: string;
      modelName?: string;
      deviceName?: string;
    };
    /** Session acquisition/cleanup-to-delivery budget, 1..3600000 ms; default 120000. Does not extend SDK's 120 s negotiation limit. */
    timeoutMs?: number;
    /** Permit APSTA channel interruption during enrolment; default false. */
    allowApChannelChange?: boolean;
  }
  interface WiFiWpsWaitOptions {
    /** Scheduled wait budget 1..3600000 ms; default 1000. */
    timeoutMs?: number;
  }
  interface WiFiWpsCredential {
    /** Exact native SSID bytes including NUL; maximum 32 bytes. */
    ssidBytes: number[];
    /** Secret key bytes including NUL; maximum 64 bytes. */
    passwordBytes: number[];
    /** Raw WPS authentication/encryption bit masks, not WIFI_AUTH_* enum values. */
    authType: number;
    encryptionType: number;
    keyIndex: number;
    /** WPS credential MAC attribute; not inferred to be AP BSSID. */
    mac: string;
  }
  type WiFiWpsEvent = { type: "pin"; pin: string } |
    { type: "credentials"; credentials: WiFiWpsCredential[] };
  interface WiFiWpsStatus {
    state: "opening" | "negotiating" | "pin-ready" | "credentials-ready" | "consumed" | "closing" | "closed" | "faulted";
    operation: number | null;
    radioGeneration: number | null;
    nativeIdentity: { low: number; high: number } | null;
    workerBusy: boolean;
    driverStarted: boolean;
    terminalSeen: boolean;
    pinReady: boolean;
    pinConsumed: boolean;
    credentialsReady: boolean;
    credentialsConsumed: boolean;
    captureFinished: boolean;
    nativeClosed: boolean;
    helperDrained: boolean;
    configRestored: boolean;
    channelRestored: boolean;
    storageRestored: boolean;
    eventFenced: boolean;
    closeRequested: boolean;
    cleanupPending: boolean;
    timedOut: boolean;
    handoffUnknown: boolean;
    trackingFault: boolean;
    /** Raw fixed-SDK event/failure metadata; zero before a native result. */
    eventId: number;
    failureReason: number;
    /** Internal fixed-SDK stage codes; stage/cleanupStage identify the framework operation. */
    nativeErrorStage: number;
    nativeCleanupStage: number;
    /** Session allocation only, excluding worker/Radio/SDK/queue storage. */
    reservedBytes: number;
    error: number | null;
    stage: string | null;
    cleanupError: number | null;
    cleanupStage: string | null;
  }
  interface WiFiWpsGlobalStatus {
    active: boolean;
    handles: number;
    workers: number;
    runtimeClosing: boolean;
    session: WiFiWpsStatus | null;
  }
  interface WiFiWpsCapabilities {
    apiVersion: "wifi-wps/1";
    stability: "candidate";
    enrollee: true;
    pbc: true;
    pin: true;
    /** AP registrar API is present only with the SDK registrar build flag. */
    apRegistrar: boolean;
    /** Registrar can use an already running 5 GHz AP on supported builds; not RF qualification. */
    apRegistrar5GHz: boolean;
    /** Separate retained AP Session and AP watch budgets; zero when registrar is disabled. */
    maxAPSessions: 0 | 4;
    maxAPWatchQueues: 0 | 4;
    /** Fixed SDK registrar support in this build; separate from implemented API. */
    sdkApRegistrar: boolean;
    autoConnect: false;
    sdkTimeoutMs: 120000;
    sdkTimeoutConfigurable: false;
    maxCredentials: 3;
    observations: true;
    maxWatchQueues: 4;
    maxWatchCapacity: 16;
    maxSessions: 4;
    maxActiveSessions: 1;
  }
  interface WiFiWpsWatchOptions {
    /** Integer 1..16, default 8. Closed queues awaiting GC still count against the four-queue budget. */
    capacity?: number;
  }
  interface WiFiWpsObservation {
    sequence: number;
    /** Metadata only. Intermediate changes may coalesce; workerBusy alone does not emit. */
    status: WiFiWpsStatus;
  }
  interface WiFiWpsSession {
    status(): WiFiWpsStatus;
    /** One observer per Session, drop-newest. Holds Session alive until queue close/GC. */
    watch(options?: WiFiWpsWatchOptions): EventQueue<WiFiWpsObservation>;
    /** Future-capable one-time transfer of PIN then credentials; wait expiry returns null. */
    receive(options?: WiFiWpsWaitOptions): WiFiWpsEvent | null;
    /** Synchronously revoke delivery and request cleanup; does not wait for native retirement. */
    cancel(): void;
    /** Future-capable idempotent close. Wait cancellation/timeout does not cancel native cleanup. */
    close(options?: WiFiWpsWaitOptions): void;
  }
  interface WiFiWpsAPOptions {
    /** Registrar method, default pbc. Existing managed AP must already be running on 2.4 GHz. */
    method?: "pbc" | "pin";
    /** Same checksum/generation rules as Station WPS. Never included in status/watch. */
    pin?: string;
    device?: WiFiWpsOptions["device"];
    /** Integer 1..3600000 ms, default 120000; includes native capture retirement before result delivery. */
    timeoutMs?: number;
  }
  type WiFiWpsAPEvent = { type: "pin"; pin: string } | { type: "registered"; mac: string };
  interface WiFiWpsAPStatus {
    state: "opening" | "negotiating" | "pin-ready" | "registered" | "consumed" | "closing" | "closed" | "faulted";
    operation: number | null;
    radioGeneration: number | null;
    /** Boot-scoped 32-bit AP identity in the shared word-pair representation; high is zero. */
    nativeIdentity: { low: number; high: number } | null;
    workerBusy: boolean;
    driverStarted: boolean;
    terminalSeen: boolean;
    pinReady: boolean;
    pinConsumed: boolean;
    resultReady: boolean;
    resultConsumed: boolean;
    captureFinished: boolean;
    nativeClosed: boolean;
    helperDrained: boolean;
    eventFenced: boolean;
    closeRequested: boolean;
    cleanupPending: boolean;
    timedOut: boolean;
    handoffUnknown: boolean;
    trackingFault: boolean;
    /** Raw fixed-SDK event/failure metadata; zero before a native result. */
    eventId: number;
    failureReason: number;
    /** Internal fixed-SDK stage codes; stage/cleanupStage identify the framework operation. */
    nativeCleanupStage: number;
    /** Session allocation only, excluding worker/Radio/SDK/queue storage. */
    reservedBytes: number;
    error: number | null;
    stage: string | null;
    cleanupError: number | null;
    cleanupStage: string | null;
  }
  interface WiFiWpsAPGlobalStatus {
    active: boolean;
    handles: number;
    workers: number;
    runtimeClosing: boolean;
    session: WiFiWpsAPStatus | null;
  }
  interface WiFiWpsAPObservation {
    sequence: number;
    /** Metadata only, sampled after waiting Futures settle. */
    status: WiFiWpsAPStatus;
  }
  interface WiFiWpsAPSession {
    status(): WiFiWpsAPStatus;
    /** One queue per AP Session, four retained queues per AP role, drop-newest. */
    watch(options?: WiFiWpsWatchOptions): EventQueue<WiFiWpsAPObservation>;
    /** Future-capable; consumes PIN then first successful peer registration. Wait expiry returns null. */
    receive(options?: WiFiWpsWaitOptions): WiFiWpsAPEvent | null;
    /** Revokes delivery and requests cleanup. Does not stop AP/STA or disconnect ordinary clients. */
    cancel(): void;
    /** Future-capable. Timeout/cancellation of a started close leaves native cleanup active. */
    close(options?: WiFiWpsWaitOptions): void;
  }
  interface WiFiWpsAPError extends Error {
    code: "WIFI_WPS_FAILED" | "WIFI_WPS_TIMEOUT" | "WIFI_WPS_CLOSED";
    operation: "wifi.wps.startAP" | "WiFiWpsAPSession.receive" | "WiFiWpsAPSession.close";
    details: WiFiWpsAPStatus & { espCode: number; espName: string; waitTimedOut: boolean };
  }
  interface WiFiWpsModule {
    /** Present only when capabilities().apRegistrar is true. AP and Station WPS are mutually exclusive. */
    startAP?(options: WiFiWpsAPOptions): WiFiWpsAPSession;
    apStatus?(): WiFiWpsAPGlobalStatus;
    capabilities(): WiFiWpsCapabilities;
    status(): WiFiWpsGlobalStatus;
    /** Reserve managed idle/disconnected Station, construct handle, schedule enrolment. SDK acceptance is asynchronous. */
    start(options: WiFiWpsOptions): WiFiWpsSession;
  }
  interface WiFiWpsError extends Error {
    code: "WIFI_WPS_FAILED" | "WIFI_WPS_TIMEOUT" | "WIFI_WPS_CLOSED";
    operation: "wifi.wps.start" | "WiFiWpsSession.receive" | "WiFiWpsSession.close";
    details: WiFiWpsStatus & { espCode: number; espName: string; waitTimedOut: boolean };
  }

  interface WiFiDppOptions {
    /** Unique channel integers, 1..5 entries; default [6]. SDK and current country rules must allow every channel. */
    channels?: number[];
    /** Optional hexadecimal DER private bootstrap key, 1..256 decoded bytes. Never a raw scalar. */
    privateKeyHexDer?: string;
    /** Optional printable ASCII, at most 128 bytes, excluding semicolon. */
    info?: string;
    /** Overall provisioning deadline, 1..3600000 ms; default 120000, starting at activation. */
    timeoutMs?: number;
    /** Explicit consent to interrupt a managed AP's channel during APSTA provisioning. */
    allowApChannelChange?: boolean;
  }
  type WiFiDppAuthentication = "dpp" | "wpa2-psk" | "wpa3-sae";
  interface WiFiDppConnectOptions {
    /** Default strongest received authentication: DPP, then SAE, then PSK. No build-dependent downgrade. */
    authentication?: WiFiDppAuthentication;
    /** Default false. Permit AP interruption during the STOP/START needed to restore a saved PMF-disabled Station configuration on close. Independent of provisioning channel consent. */
    allowApRestart?: boolean;
    /** Selection, installation, IP and negotiated-authentication deadline, 1..3600000 ms; default 30000. */
    timeoutMs?: number;
  }
  interface WiFiDppConnectResult extends WiFiConnectResult {
    configurationIndex: number;
    connectionGeneration: number;
    authentication: WiFiDppAuthentication;
  }
  interface WiFiDppWaitOptions {
    /** Per-wait budget 1..3600000 ms; default 1000. A receive wait expiry returns null. */
    timeoutMs?: number;
  }
  interface WiFiDppConfiguration {
    index: number;
    /** Exact native octets, including embedded NUL. */
    ssidBytes: number[];
    /** Secret bytes; empty for a configuration without a legacy password. */
    passwordBytes: number[];
    akm: "unknown" | "dpp" | "psk" | "sae" | "psk-sae" | "sae-dpp" | "psk-sae-dpp";
    connector: string | null;
    /** Secret native key bytes. Present only in the dedicated receive result. */
    netAccessKeyBytes: number[];
    cSignKeyBytes: number[];
    /** Unmodified SDK expiry hint as two exact unsigned words; null when zero. */
    netAccessKeyExpiry: { low: number; high: number } | null;
    /** Channel of the provisioning exchange; not a BSSID or a connection result. */
    channel: number;
  }
  type WiFiDppEvent = { type: "uri"; uri: string } |
    { type: "configurations"; configurations: WiFiDppConfiguration[] };
  interface WiFiDppStatus {
    state: "opening" | "negotiating" | "uri-ready" | "configurations-ready" | "consumed" | "connecting" | "connected" | "closing" | "closed" | "faulted";
    operation: number | null;
    radioGeneration: number | null;
    nativeIdentity: { low: number; high: number } | null;
    workerBusy: boolean;
    connectionRequested: boolean;
    connectionPrepared: boolean;
    connectionVerified: boolean;
    connected: boolean;
    configurationIndex: number | null;
    authentication: WiFiDppAuthentication | null;
    connectionGeneration: number | null;
    connectionReason: number | null;
    stationConfigRestored: boolean;
    restoreRequiresRestart: boolean;
    restoreStopped: boolean;
    restoreStarted: boolean;
    restoreStartFailed: boolean;
    /** Last SDK START return code; null after an accepted START. */
    restoreStartError: number | null;
    /** Explicit recovery request queued or native restoration still pending. */
    recoveryPending: boolean;
    /** Admitted retries for this Session's known restoration START failure. Never wraps. */
    recoveryAttempts: number;
    storageRestored: boolean;
    listening: boolean;
    uriReady: boolean;
    uriConsumed: boolean;
    configurationsReady: boolean;
    configurationsConsumed: boolean;
    configurationCount: number;
    captureFinished: boolean;
    nativeClosed: boolean;
    helperDrained: boolean;
    channelRestored: boolean;
    eventFenced: boolean;
    closeRequested: boolean;
    cleanupPending: boolean;
    timedOut: boolean;
    handoffUnknown: boolean;
    terminalSeen: boolean;
    /** Framework Session/Radio/worker/result allocations and copied pending TX, excluding SDK-internal heaps and watch queues. */
    reservedBytes: number;
    observationDrops: number;
    asyncPending: number;
    asyncActive: number;
    txWakeRetries: number;
    txQueuedBytes: number;
    txBufferPresent: boolean;
    txRecycling: boolean;
    txRecycled: boolean;
    txQueued: boolean;
    txError: number | null;
    txBufferError: number | null;
    txPostError: number | null;
    channelTimerError: number | null;
    error: number | null;
    stage: string | null;
    cleanupError: number | null;
    cleanupStage: string | null;
  }
  interface WiFiDppGlobalStatus {
    active: boolean;
    handles: number;
    workers: number;
    runtimeClosing: boolean;
    session: WiFiDppStatus | null;
  }
  interface WiFiDppCapabilities {
    apiVersion: "wifi-dpp/1";
    stability: "candidate";
    enrollee: true;
    qrCode: true;
    autoConnect: false;
    configurationSelection: true;
    connectorAuthentication: true;
    pskAuthentication: true;
    saeAuthentication: boolean;
    connectionOwnership: "session";
    observations: true;
    maxChannels: 5;
    /** Configured SDK result bound, 1..10. */
    maxConfigurations: number;
    maxSessions: 4;
    maxActiveSessions: 1;
    maxWatchQueues: 4;
    maxWatchCapacity: 16;
  }
  interface WiFiDppWatchOptions {
    /** Integer 1..16, default 8; closed queues awaiting collection retain their budget. */
    capacity?: number;
  }
  interface WiFiDppObservation {
    sequence: number;
    /** Metadata only; intermediate changes can coalesce. No URI, SSID, password, connector or keys. */
    status: WiFiDppStatus;
  }
  interface WiFiDppSession {
    status(): WiFiDppStatus;
    /** One observer per Session; drop-newest. Queue lifetime retains the Session. */
    watch(options?: WiFiDppWatchOptions): EventQueue<WiFiDppObservation>;
    /** Future-capable URI then configuration delivery. Consume only after complete conversion. */
    receive(options?: WiFiDppWaitOptions): WiFiDppEvent | null;
    /** Future-capable explicit selection after receipt. Session owns the connection; cancel/close disconnects it.
     * A connected same-index/same-auth retry returns the existing result without resubmission.
     * Restores an eligible saved PMF-disabled configuration through STOP/START; APSTA requires allowApRestart. Never persists received credentials to FLASH. */
    connect(index: number, options?: WiFiDppConnectOptions): WiFiDppConnectResult;
    /** Request one recovery of a known restoration START failure. Idempotent while pending; use close() to wait. Does not reconnect or recover unknown faults. */
    recover(): void;
    /** Synchronously revoke delivery and request cleanup; does not wait for native retirement. */
    cancel(): void;
    /** Future-capable close. Wait cancellation/timeout leaves native cleanup active. */
    close(options?: WiFiDppWaitOptions): void;
  }
  interface WiFiDppModule {
    capabilities(): WiFiDppCapabilities;
    status(): WiFiDppGlobalStatus;
    /** Disconnected, managed STARTED Station required. Starts background QR enrollee provisioning. */
    startEnrollee(options: WiFiDppOptions): WiFiDppSession;
  }
  interface WiFiDppError extends Error {
    code: "WIFI_DPP_FAILED" | "WIFI_DPP_TIMEOUT" | "WIFI_DPP_CLOSED";
    operation: "wifi.dpp.startEnrollee" | "WiFiDppSession.receive" | "WiFiDppSession.connect" | "WiFiDppSession.close" | "WiFiDppSession.recover";
    details: WiFiDppStatus & { espCode: number; espName: string; waitTimedOut: boolean };
  }

  interface WiFiMeshWaitOptions {
    /** 1..120000 ms, default 30000. Wait expiry does not preempt a dispatched SDK call. */
    timeoutMs?: number;
  }
  interface WiFiMeshOpenConfig extends WiFiMeshWaitOptions {
    /** Exactly six bytes, nonzero. */
    meshId: ByteSource;
    /** UTF-8 bytes: 1..32. */
    routerSsid: string;
    /** Empty or 8..64 UTF-8 bytes; no embedded NUL. */
    routerPassword?: string;
    routerBssid?: ByteSource;
    /** Empty for open authentication; otherwise 8..64 bytes. */
    apPassword: string;
    apAuthentication?: "open" | "wpa-psk" | "wpa2-psk" | "wpa-wpa2-psk";
    /** 0..14; 0 searches. Regulatory/channel support remains driver-controlled. */
    channel?: number;
    allowChannelSwitch?: boolean;
    allowRouterSwitch?: boolean;
    /** Startup-only voting threshold, (0,1], default 0.9. */
    votePercentage?: number;
    topology?: "tree" | "chain";
    type?: "idle" | "root" | "node" | "leaf" | "station";
    /** 1..25 for tree or 1..1000 for chain; default 6. */
    maxLayer?: number;
    /** 1..1000, default 32. */
    capacity?: number;
    /** 16..128, default 16. */
    receiveQueue?: number;
    /** Native send blocking setting, 1..60000 ms, default 1000. */
    sendBlockMs?: number;
    /** 1..10, default 4; sum with nonMeshConnections must be <=10. */
    maxConnections?: number;
    nonMeshConnections?: number;
    fixedRoot?: boolean;
    selfOrganized?: boolean;
    powerSave?: boolean;
    /** Explicit consent to transient AP restart when restoring a saved AP/APSTA mode. */
    allowApRestart?: boolean;
  }
  type WiFiMeshOpenOptions = WiFiMeshOpenConfig & (
    { encryptIE: true; ieKey: string } | { encryptIE: false; ieKey?: never }
  );
  interface WiFiMeshStatus {
    identity: number;
    state: "opening" | "ready" | "closing" | "closed";
    /** Native startup complete; does not imply parent association or IP connectivity. */
    ready: boolean;
    workerBusy: boolean;
    cleanupPending: boolean;
    radioGeneration: number | null;
    nativeIdentity: number | null;
    parentKnown: boolean;
    parentConnected: boolean;
    parent: number[] | null;
    channel: number | null;
    layer: number | null;
    snapshotValid: boolean;
    routerBssid: number[] | null;
    queryError: number | null;
    scan: WiFiMeshScanStatus | null;
    root: boolean | null;
    /** Requires a valid native snapshot and parent-connected observation. */
    type: "idle" | "root" | "node" | "leaf" | "station" | null;
    nodes: number | null;
    routes: number | null;
    txPending: { toParent: number; toParentP2P: number; toChild: number; toChildP2P: number; management: number; broadcast: number } | null;
    rxPending: { toDS: number; toSelf: number } | null;
    voting: boolean;
    toDSReachable: boolean;
    dhcpStarted: boolean;
    /** Local IPv4 acquired; independent of ToDS/Internet reachability. */
    ipReady: boolean;
    ipv4: number[] | null;
    nativeRetired: boolean;
    stopped: boolean;
    netifsRetired: boolean;
    restored: boolean;
    restartRequired: boolean;
    recoveryPending: boolean;
    recoveryAttempts: number;
    timedOut: boolean;
    sends: number;
    receives: number;
    events: number;
    droppedEvents: number;
    malformedEvents: number;
    /** Session and Radio/native-owner storage; not all jobs, queues or SDK memory. */
    reservedBytes: number;
    error: number | null;
    cleanupError: number | null;
    networkError: number | null;
    stage: string | null;
    nativeStage: string | null;
    cleanupStage: string | null;
  }
  interface WiFiMeshSendData extends WiFiMeshWaitOptions {
    /** Copied during capture; 1..1472 bytes. */
    data: ByteSource;
    protocol?: "binary" | "http" | "json" | "mqtt";
  }
  type WiFiMeshSendOptions = WiFiMeshSendData & (
    { destination?: "root"; reliable?: true; address?: never; ipv4?: never; port?: never; dropOnRootChange?: never } |
    { destination: "peer" | "fromDS" | "group"; address: ByteSource; reliable?: boolean; ipv4?: never; port?: never; dropOnRootChange?: never } |
    { destination: "broadcast"; reliable?: boolean; address?: never; ipv4?: never; port?: never; dropOnRootChange?: never } |
    { destination: "toDS"; ipv4: ByteSource; port: number; reliable?: boolean; dropOnRootChange?: boolean; address?: never }
  );
  interface WiFiMeshCommandResult {
    identity: number;
    bytes: number;
    dispatched: boolean;
    returned: boolean;
    /** Monotonic microseconds when the SDK call returned; not a delivery acknowledgement. */
    completedAtUs: number;
  }
  interface WiFiMeshReceiveOptions extends WiFiMeshWaitOptions { toDS?: boolean; }
  interface WiFiMeshGroupOptions extends WiFiMeshWaitOptions { addresses: ByteSource[]; }
  interface WiFiMeshToDSOptions extends WiFiMeshWaitOptions { reachable: boolean; }
  interface WiFiMeshMessage {
    sequence: number;
    from: number[];
    toDS: boolean;
    ipv4: number[] | null;
    port: number | null;
    /** SDK protocol enum, including values not accepted by send(). */
    protocol: number;
    service: number;
    flags: number;
    /** Owned copied bytes; close the view when finished. Survives Session close. */
    data: ByteView;
  }
  interface WiFiMeshEvent {
    sequence: number;
    /** Fixed SDK Mesh event identifier and valid-field bit mask. */
    id: number;
    fields: number;
    mac: number[] | null;
    channel: number | null;
    layer: number | null;
    reason: number | null;
    tableSize: number | null;
    tableChange: number | null;
    value: boolean | null;
    duty: number | null;
  }
  interface WiFiMeshWatchOptions { capacity?: number; }
  interface WiFiMeshConfiguration {
    meshId: number[];
    routerSsidBytes: number[];
    routerSsid: string | null;
    routerBssid: number[];
    channel: number;
    allowChannelSwitch: boolean;
    allowRouterSwitch: boolean;
    topology: "tree" | "chain" | null;
    /** Native SDK authentication enum. */
    apAuthentication: number;
    maxLayer: number; capacity: number; receiveQueue: number; sendBlockMs: number;
    maxConnections: number; nonMeshConnections: number;
    fixedRoot: boolean; selfOrganized: boolean; powerSave: boolean;
    votePercentage: number; rootConflicts: boolean;
    associationExpirySeconds: number; rootHealingDelayMs: number;
    encryptIE: boolean; ieKeyLength: number;
    secretsIncluded: boolean;
    /** Null unless includeSecrets is explicitly true. Returned strings belong to JS. */
    routerPassword: string | null; apPassword: string | null; ieKey: string | null;
  }
  interface WiFiMeshConfigurationOptions extends WiFiMeshWaitOptions { includeSecrets?: boolean; }
  interface WiFiMeshRouterOptions extends WiFiMeshWaitOptions {
    ssid: string; password?: string; bssid?: ByteSource; allowRouterSwitch?: boolean;
  }
  interface WiFiMeshAddressOptions extends WiFiMeshWaitOptions { address: ByteSource; }
  interface WiFiMeshTypeOptions extends WiFiMeshWaitOptions { type: "idle" | "root" | "node" | "leaf" | "station"; }
  interface WiFiMeshBooleanOptions extends WiFiMeshWaitOptions { enabled: boolean; }
  interface WiFiMeshSelfOrganizedOptions extends WiFiMeshBooleanOptions { selectParent?: boolean; }
  interface WiFiMeshAssociationExpiryOptions extends WiFiMeshWaitOptions { /** 10..2147483 seconds. */ seconds: number; }
  interface WiFiMeshRootHealingOptions extends WiFiMeshWaitOptions { milliseconds: number; }
  type WiFiMeshEncryptionOptions = WiFiMeshWaitOptions & (
    { enabled: true; key: string } | { enabled: false; key?: never }
  );
  interface WiFiMeshVoteOptions extends WiFiMeshWaitOptions { attempts?: number; percentage?: number; }
  interface WiFiMeshChannelOptions extends WiFiMeshWaitOptions { channel: number; beaconCount?: number; routerBssid?: ByteSource; }
  interface WiFiMeshDeviceDutyOptions extends WiFiMeshWaitOptions { duty: number; type: "request" | "demand"; }
  interface WiFiMeshNetworkDutyOptions extends WiFiMeshWaitOptions {
    duty: number;
    /** Positive integer minutes, or -1 for a root's indefinite network policy. Entire-network rule. */
    durationMinutes: number;
  }
  interface WiFiMeshSignalDutyOptions extends WiFiMeshWaitOptions { /** 0..254; native command stores count+1 in a byte. */ forwardCount: number; }
  interface WiFiMeshUpstreamCapacity { available: number; lastSequence: number; }
  interface WiFiMeshPowerStatus {
    enabled: boolean; active: boolean; deviceDuty: number; deviceType: number;
    networkDuty: number; durationMinutes: number; networkType: number; appliedRule: number; runningDuty: number;
  }
  interface WiFiMeshParentOptions extends WiFiMeshWaitOptions {
    /** 1..32 bytes, no NUL; ByteSource preserves non-UTF-8 SSIDs. */
    ssid: string | ByteSource;
    password?: string;
    /** Optional nonzero unicast six-byte BSSID. */
    bssid?: ByteSource;
    channel: number;
    meshId?: ByteSource;
    type: "root" | "node" | "leaf";
    /** Root=1; node>1 and <maxLayer; leaf<=maxLayer. SDK may change layer after joining. */
    layer: number;
  }
  interface WiFiMeshScanOptions extends WiFiMeshWaitOptions {
    ssid?: string | ByteSource;
    bssid?: ByteSource;
    /** 0=all allowed 2.4 GHz channels; otherwise 1..14. */
    channel?: number;
    /** Unique 2.4 GHz channels, mutually exclusive with nonzero channel. */
    channels?: number[];
    showHidden?: boolean;
    /** Default passive. */
    mode?: "active" | "passive";
    activeMinMs?: number; activeMaxMs?: number; passiveMs?: number;
    homeChannelDwellMs?: number;
    coexistenceBackgroundScan?: boolean;
  }
  interface WiFiMeshScanStatus {
    identity: number;
    running: boolean; completed: boolean; uncertain: boolean; retained: boolean;
    /** Native retained scan list count, not every AP present over RF. */
    total: number;
    /** Includes the framework's pending retained record until JS conversion commits. */
    remaining: number;
    error: number | null;
  }
  interface WiFiMeshScanReadOptions extends WiFiMeshWaitOptions { scanId: number; }
  interface WiFiMeshAssociation {
    topology: "tree" | "chain";
    /** Third-party SDK IE version, independent of project v1. */
    ieVersion: number; ieType: number; encrypted: boolean; type: number;
    meshId: number[];
    layer: number; layerCapacity: number;
    connections: number; connectionCapacity: number; leaves: number; leafCapacity: number;
    rootCapacity: number; selfCapacity: number; layer2Capacity: number; scanAPs: number;
    parentRssi: number; routerRssi: number; flags: number;
    rootCandidate: number[]; rootCandidateRssi: number;
    votedAddress: number[]; votedRssi: number; voteTTL: number; votes: number; myVotes: number;
    voteReason: number; child: number[]; toDS: number;
  }
  interface WiFiMeshScanRecord {
    scanId: number; sequence: number;
    accessPoint: WiFiScanRecord;
    bssidBytes: number[];
    /** Decoded pinned SDK tree/chain IE, or null for an absent/unrecognized layout. Not an authentication proof. */
    association: WiFiMeshAssociation | null;
    /** Exact SDK-returned IE, owned copy. Close when finished; survives flush/session close. */
    meshIE: ByteView;
  }
  interface WiFiMeshSession {
    status(): WiFiMeshStatus;
    /** Future-capable. Wait expiry leaves native startup governed by open.timeoutMs. */
    ready(options?: WiFiMeshWaitOptions): WiFiMeshStatus;
    /** Future-capable. Native retirement, Radio restoration and ownership release complete before success. */
    close(options?: WiFiMeshWaitOptions): void;
    /** Requests close immediately; cannot preempt an executing native SDK call. */
    cancel(): void;
    /** Future-capable. Requests one explicit recovery of a known failed restoration. */
    recover(options?: WiFiMeshWaitOptions): void;
    /** Future-capable. Successful SDK queue submission, not end-to-end delivery. */
    send(options: WiFiMeshSendOptions): WiFiMeshCommandResult;
    /** Future-capable. One waiter per self/ToDS lane; timeout returns null. */
    receive(options?: WiFiMeshReceiveOptions): WiFiMeshMessage | null;
    watch(options?: WiFiMeshWatchOptions): EventQueue<WiFiMeshEvent>;
    /** Future-capable. Copied native routing table. */
    routingTable(options?: WiFiMeshWaitOptions): number[][];
    /** Future-capable. Copied native group list. */
    groups(options?: WiFiMeshWaitOptions): number[][];
    /** Future-capable. Group arrays contain 1..64 unique nonzero six-byte addresses. */
    addGroups(options: WiFiMeshGroupOptions): WiFiMeshCommandResult;
    removeGroups(options: WiFiMeshGroupOptions): WiFiMeshCommandResult;
    /** Future-capable. Requires actual root role when the SDK command executes. */
    setToDSState(options: WiFiMeshToDSOptions): WiFiMeshCommandResult;
    /** Future-capable. Requests native connection; observe status for association/IP. */
    connect(options?: WiFiMeshWaitOptions): WiFiMeshCommandResult;
    /** Future-capable. Requests native disconnect, retaining the Mesh Session. */
    disconnect(options?: WiFiMeshWaitOptions): WiFiMeshCommandResult;
    /** Future-capable. Explicitly discards native upstream queued packets. */
    flushUpstream(options?: WiFiMeshWaitOptions): WiFiMeshCommandResult;
    /** Future-capable. Sequential SDK readback; credentials require explicit includeSecrets. */
    configuration(options?: WiFiMeshConfigurationOptions): WiFiMeshConfiguration;
    /** Future-capable. Replaces the full router settings; absent password/BSSID means empty/automatic. */
    setRouter(options: WiFiMeshRouterOptions): WiFiMeshCommandResult;
    /** Future-capable. SDK acceptance does not imply convergence of the network. */
    setMeshId(options: WiFiMeshAddressOptions): WiFiMeshCommandResult;
    setType(options: WiFiMeshTypeOptions): WiFiMeshCommandResult;
    setSelfOrganized(options: WiFiMeshSelfOrganizedOptions): WiFiMeshCommandResult;
    setFixedRoot(options: WiFiMeshBooleanOptions): WiFiMeshCommandResult;
    setRootConflicts(options: WiFiMeshBooleanOptions): WiFiMeshCommandResult;
    setAssociationExpiry(options: WiFiMeshAssociationExpiryOptions): WiFiMeshCommandResult;
    setRootHealingDelay(options: WiFiMeshRootHealingOptions): WiFiMeshCommandResult;
    /** Future-capable. Key update precedes encryption enable; errors report completedSteps/controlStage. */
    setIEEncryption(options: WiFiMeshEncryptionOptions): WiFiMeshCommandResult;
    /** Future-capable. Actual root required. Default 15 attempts and 0.9 threshold. */
    waiveRoot(options?: WiFiMeshVoteOptions): WiFiMeshCommandResult;
    switchChannel(options: WiFiMeshChannelOptions): WiFiMeshCommandResult;
    setDeviceDuty(options: WiFiMeshDeviceDutyOptions): WiFiMeshCommandResult;
    setNetworkDuty(options: WiFiMeshNetworkDutyOptions): WiFiMeshCommandResult;
    signalDuty(options: WiFiMeshSignalDutyOptions): WiFiMeshCommandResult;
    /** Future-capable. Associated child's subnet, bounded to 1000 entries; network may change between native reads. */
    subnet(options: WiFiMeshAddressOptions): number[][];
    hasGroup(options: WiFiMeshAddressOptions): boolean;
    upstreamCapacity(options: WiFiMeshAddressOptions): WiFiMeshUpstreamCapacity;
    powerStatus(options?: WiFiMeshWaitOptions): WiFiMeshPowerStatus;
    /** Future-capable. Exact native signed microseconds; rejects outside JS safe-integer range. */
    tsfTime(options?: WiFiMeshWaitOptions): number;
    /** Future-capable. Requires manual networking; native acceptance does not mean association/IP. */
    setParent(options: WiFiMeshParentOptions): WiFiMeshCommandResult;
    /** Future-capable. Blocking SDK scan on the native worker; selfOrganized must be false. */
    scan(options?: WiFiMeshScanOptions): WiFiMeshScanStatus;
    /** Future-capable. Exact scan identity, one reader; null when exhausted. Conversion commits the retained record. */
    receiveScan(options: WiFiMeshScanReadOptions): WiFiMeshScanRecord | null;
    /** Future-capable. Explicit discard before another scan or topology mutation. Uncertain scan requires Session close. */
    flushScan(options: WiFiMeshScanReadOptions): WiFiMeshCommandResult;

  }
  interface WiFiMeshCapabilities {
    apiVersion: "wifi-mesh/1";
    stability: "candidate";
    radioOwnership: "exclusive";
    band: "2.4ghz";
    maxSessions: 4;
    maxActiveSessions: 1;
    maxPendingCommands: 1;
    maxCommandHandles: 8;
    maxPayloadBytes: 1472;
    maxRoutingEntries: 1000;
    maxGroupEntries: 64;
    maxTimeoutMs: 120000;
    maxScanIEBytes: 257;
  }
  interface WiFiMeshModule {
    capabilities(): WiFiMeshCapabilities;
    open(options: WiFiMeshOpenOptions): WiFiMeshSession;
  }
  interface WiFiMeshError extends Error {
    code: "WIFI_MESH_FAILED" | "WIFI_MESH_CLOSED" | "WIFI_MESH_TIMEOUT";
    operation: "wifi.mesh.open" | "WiFiMeshSession.ready" | "WiFiMeshSession.close" | "WiFiMeshSession.recover" |
      "WiFiMeshSession.send" | "WiFiMeshSession.receive" | "WiFiMeshSession.watch" | "WiFiMeshSession.routingTable" |
      "WiFiMeshSession.groups" | "WiFiMeshSession.addGroups" | "WiFiMeshSession.removeGroups" | "WiFiMeshSession.setToDSState" |
      "WiFiMeshSession.connect" | "WiFiMeshSession.disconnect" | "WiFiMeshSession.flushUpstream" |
      "WiFiMeshSession.configuration" | "WiFiMeshSession.setRouter" | "WiFiMeshSession.setMeshId" | "WiFiMeshSession.setType" | "WiFiMeshSession.setSelfOrganized" | "WiFiMeshSession.setFixedRoot" | "WiFiMeshSession.setRootConflicts" | "WiFiMeshSession.setAssociationExpiry" | "WiFiMeshSession.setRootHealingDelay" | "WiFiMeshSession.setIEEncryption" | "WiFiMeshSession.waiveRoot" | "WiFiMeshSession.switchChannel" | "WiFiMeshSession.setDeviceDuty" | "WiFiMeshSession.setNetworkDuty" | "WiFiMeshSession.signalDuty" | "WiFiMeshSession.subnet" | "WiFiMeshSession.hasGroup" | "WiFiMeshSession.upstreamCapacity" | "WiFiMeshSession.powerStatus" | "WiFiMeshSession.tsfTime" | "WiFiMeshSession.setParent" | "WiFiMeshSession.scan" | "WiFiMeshSession.receiveScan" | "WiFiMeshSession.flushScan";
    details: WiFiMeshStatus & {
      espCode: number; espName: string; waitTimedOut: boolean;
      operationIdentity?: number; dispatched?: boolean; returned?: boolean; nativeError?: number | null;
      controlStage?: string | null; completedSteps?: number | null;
    };
  }

  interface WiFiNanOpenOptions {
    /** Defaults to synchronized when enabled, otherwise unsynchronized. USD requires SDK NAN_USD_ENABLE and accepts only mode/timeoutMs here; channels belong to services. */
    mode?: "synchronized" | "unsynchronized";
    /** Default false. Requires compiled NAN security; Session-wide IGTK/BIGTK policy, also advertises group data support on secured services. */
    groupManagementProtection?: boolean;
    /** SDK channel field, integer 1..255; default 6. Target driver enforces supported NAN channels and regulatory limits. */
    channel?: number;
    /** Integer 0..255; default 2. */
    masterPreference?: number;
    /** Integer seconds 0..255; default 3. */
    scanTimeSeconds?: number;
    /** Integer seconds 0..65535; default 5. */
    warmUpSeconds?: number;
    /** Default true. Does not persist or erase NAN credentials. */
    randomizeMac?: boolean;
    /** Startup deadline from activation, 1..120000 ms; default 10000. Expiry requests native cleanup. */
    timeoutMs?: number;
  }
  interface WiFiNanWaitOptions {
    /** Per-wait deadline, 1..120000 ms; default 10000. Does not change the Session's startup deadline. */
    timeoutMs?: number;
  }
  interface WiFiNanStatus {
    /** Boot-unique Session identity; zero only in an error raised before Session allocation. */
    identity: number;
    mode: "synchronized" | "unsynchronized";
    state: "opening" | "ready" | "closing" | "closed";
    /** Last captured native operation identity and generation; may remain as diagnostics after close. */
    operation: number | null;
    radioGeneration: number | null;
    activated: boolean;
    workerBusy: boolean;
    /** Native discovery started; synchronized mode also requires its netif. Does not prove peer discovery or a usable datapath. */
    ready: boolean;
    closeRequested: boolean;
    cleanupPending: boolean;
    /** Session startup deadline expired, independent of a ready()/close() wait timeout. */
    timedOut: boolean;
    nativeStartSeen: boolean;
    nativeStopSeen: boolean;
    /** Session policy preserved after transient startup configuration is cleared. */
    groupManagementProtection: boolean;
    startAttempted: boolean;
    startAccepted: boolean;
    stopped: boolean;
    nativeReset: boolean;
    netifRetired: boolean;
    observerRetired: boolean;
    modeRestored: boolean;
    storageRestored: boolean;
    /** Framework Session and retained Radio control allocations; excludes SDK heaps and Future allocations. */
    reservedBytes: number;
    error: number | null;
    stage: string | null;
    nativeStage: string | null;
    cleanupError: number | null;
    cleanupStage: string | null;
  }
  interface WiFiNanTxStatus {
    /** Shared tracking slots for actual SDK SD/NDP management buffers, not a service or driver queue limit. */
    capacity: 32;
    tracked: number;
    /** Includes construction or queued frames whose service key has not reached TX submission. */
    unidentified: number;
    /** Saturating submission/rejection counters for the current pool lifetime. */
    submissions: number;
    rejected: number;
    /** Internal-memory tracking storage only; SDK frame bytes remain driver-owned. */
    reservedBytes: number;
    closing: boolean;
    identityExhausted: boolean;
    error: number | null;
  }
  interface WiFiNanGlobalStatus {
    active: boolean;
    /** Includes closed handles and Future-retained Sessions until final release. */
    handles: number;
    workers: number;
    serviceHandles: number;
    activeServices: number;
    messageHandles: number;
    dataPathHandles: number;
    activeDataPaths: number;
    pairingHandles: number;
    activePairings: number;
    runtimeClosing: boolean;
    session: WiFiNanStatus | null;
    /** Sync buffer pool only; null for an active USD Session or a USD-only build. */
    tx: WiFiNanTxStatus | null;
    /** Entered native SD TX and service-match/replied/receive scopes; includes nested scopes. */
    serviceCallbacks: number;
  }
  interface WiFiNanCapabilities {
    apiVersion: "wifi-nan/1";
    stability: "candidate";
    synchronized: boolean;
    unsynchronized: boolean;
    /** Native Sync cache queries; absent methods in USD-only builds. */
    peerQueries: boolean;
    maxPeerRecordsPerService: 0 | 15;
    radioOwnership: "exclusive";
    maxSessions: 4;
    maxActiveSessions: 1;
    maxStartupTimeoutMs: 120000;
    maxServiceHandles: 8;
    maxActiveServices: 2;
    maxServiceSsiBytes: 512;
    maxReceivedSsiBytes: 2048;
    maxMessageHandles: 8;
    maxActiveMessages: 1;
    maxSentSsiBytes: 2048;
    dataPaths: boolean;
    maxActiveDataPaths: 0 | 2;
    maxDataPathHandles: 0 | 8;
    incomingDataPathTimeoutMs: 0 | 10000;
    /** Explicitly confirmed PIN/PASN pairing; check pinBootstrap for on-air request/receive. */
    pinPairing: boolean;
    pinBootstrap: boolean;
    maxPendingPairingRequests: 0 | 1;
    maxBootstrapPeersPerService: 0 | 8;
    cachedVerification: boolean;
    maxCachedPairings: 0 | 2;
    maxActivePairings: 0 | 1;
    maxPairingHandles: 0 | 8;
    vendorAttributes: boolean;
    maxVendorBodyBytes: 0 | 255;
    /** Whether the fixed NCS-SK-128 implementation is compiled in. */
    security: boolean;
    securityReason: string | null;
    maxCredentials: 0 | 4;
  }
  /** A current native cache selector, never a retained Service identity or mutation authority. */
  type WiFiNanServiceSelector = number | string;
  interface WiFiNanServiceInfo {
    serviceId: number;
    name: string;
    peerCount: number;
  }
  interface WiFiNanPeerInfo {
    serviceId: number;
    peerServiceId: number;
    peerType: "publish" | "subscribe";
    /** NAN Management Interface address (NMI), exactly six bytes. */
    peerMac: number[];
    /** Service-qualified native NDL record; does not prove establishment or IP reachability. */
    ndpId: number | null;
    /** NAN Data Interface address (NDI); null when ndpId is null. */
    peerDataMac: number[] | null;
  }
  interface WiFiNanPeerRecords extends WiFiNanServiceInfo {
    /** At most 15 entries. peerCount and peers are captured under the same native lock. */
    peers: WiFiNanPeerInfo[];
  }
  interface WiFiNanSession {
    /** Sync only, synchronous. ID 1..255 or name 1..255 UTF-8 bytes without NUL; null if missing. */
    getServiceInfo(service: WiFiNanServiceSelector): WiFiNanServiceInfo | null;
    /** Sync only. First native match when service is omitted; null if missing. MAC: six unicast bytes. */
    getPeerInfo(peerMac: ByteSource, service?: WiFiNanServiceSelector): WiFiNanPeerInfo | null;
    /** Sync only. null for missing service; an existing empty service has peers: []. */
    getPeerRecords(service: WiFiNanServiceSelector): WiFiNanPeerRecords | null;
    /** Requires a ready Session. Returns a handle before background creation finishes. */
    publish(options: WiFiNanPublishOptions): WiFiNanService;
    subscribe(options: WiFiNanSubscribeOptions): WiFiNanService;
    status(): WiFiNanStatus;
    /** Future-capable. Returns a fresh ready snapshot; wait cancellation/timeout leaves discovery active. */
    ready(options?: WiFiNanWaitOptions): WiFiNanStatus;
    /** Future-capable. Success means native retirement and Radio release; wait cancellation/timeout leaves cleanup active. */
    close(options?: WiFiNanWaitOptions): void;
    /** Requests close immediately without waiting for native retirement. */
    cancel(): void;
  }
  interface WiFiNanVendorAttribute {
    /** Exactly 3 bytes. OUI values are vendor identifiers, not MAC addresses. */
    oui: ByteSource;
    /** 0..255 bytes, copied before native submission. */
    body: ByteSource;
  }
  type WiFiNanCredential = { cipher?: "ncs-sk-128" } & (
    { passphrase: string; pmk?: never } | { pmk: ByteSource; passphrase?: never }
  );
  interface WiFiNanSecurityOptions {
    /** 1..4 credentials, at most 3 with pairing enabled. Passphrase: 8..63 UTF-8 bytes without NUL; PMK: exactly 32 bytes. */
    credentials: WiFiNanCredential[];
    /** Default false. Advertises group data protection; activation requires matching peer capabilities. */
    groupDataProtection?: boolean;
  }
  interface WiFiNanServiceOptions {
    /** Sync + pairing builds only. Advertise PASN security, PIN setup and cached verification; default false. Never auto-accept Auth1. */
    pairing?: boolean;
    /** Nonempty UTF-8 service name, at most 255 bytes, no embedded NUL. */
    name: string;
    /** SDK comma-separated matching filter, at most 255 UTF-8 bytes, no embedded NUL. */
    matchingFilter?: string;
    /** Default false. SDK single match/replied semantics. */
    singleEvent?: boolean;
    /** Opt in to data paths; default false. Incoming requests require explicit response within 10 seconds. */
    dataPath?: boolean;
    /** Static NCS-SK-128 credentials. Without these, pairing uses PASN; without either option, the service is open. */
    security?: WiFiNanSecurityOptions;
    /** Attribute added to this service's publish/subscribe frames. */
    vendor?: WiFiNanVendorAttribute;
    /** Service-specific bytes; maximum 512 bytes. */
    ssi?: ByteSource;
    /** Creation deadline, 1..120000 ms; default 10000. */
    timeoutMs?: number;
    /** Events retained before the caller starts receiving, 1..16; default 4. */
    queueCapacity?: number;
    /** USD only: lifetime in seconds, integer 0..2147483647; default 60. Zero means one publish transmission or first subscribe match. */
    ttlSeconds?: number;
    /** USD only: default channel, default 6; regulatory/target limits apply. */
    channel?: number;
    /** USD only: 1..42 distinct 2.4/5 GHz channel numbers; 5 GHz requires target support. */
    channels?: number[];
  }
  interface WiFiNanPublishOptions extends WiFiNanServiceOptions {
    type?: "unsolicited" | "solicited";
    /** USD only: single-/multi-channel dwell range in 100 TU units; min/max 1..255, defaults 5/10. */
    dwell?: { nMin?: number; nMax?: number; mMin?: number; mMax?: number };
  }
  interface WiFiNanSubscribeOptions extends WiFiNanServiceOptions {
    type?: "active" | "passive";
  }
  interface WiFiNanServiceStatus {
    pairingEnabled: boolean;
    pairingRequestPending: boolean;
    pairingRequestsDropped: number;
    securityRequired: boolean;
    /** Explicit static credentials; excludes the internally added PASN cipher descriptor. */
    credentialCount: number;
    /** Advertised support; does not prove a peer negotiated group keys. */
    groupDataProtection: boolean;
    groupManagementProtection: boolean;
    vendorBytes: number;
    identity: number;
    sessionIdentity: number;
    serviceId: number | null;
    kind: "publish" | "subscribe";
    state: "opening" | "ready" | "closing" | "closed";
    ready: boolean;
    creating: boolean;
    closeRequested: boolean;
    nativeCancelled: boolean;
    cleanupPending: boolean;
    timedOut: boolean;
    droppedBeforeQueue: number;
    /** Non-null until the current message's native storage retires. */
    sendIdentity: number | null;
    sendPending: boolean;
    sendCleanupPending: boolean;
    reservedBytes: number;
    error: number | null;
    cleanupError: number | null;
  }
  interface WiFiNanServiceEvent {
    kind: "match" | "replied" | "message";
    /** Boot-unique framework service identity; raw service IDs may be reused after retirement. */
    identity: number;
    sequence: number;
    serviceId: number;
    peerServiceId: number;
    peerMac: number[];
    /** Copied bytes; up to 2048 bytes for received follow-up messages. */
    ssi: number[];
    /** The following peer capabilities are available for match events; otherwise null. */
    ssiVersion: number | null;
    datapathRequired: boolean | null;
    securityRequired: boolean | null;
    furtherDiscovery: boolean | null;
    gas: boolean | null;
    ndpe: boolean | null;
  }
  interface WiFiNanService {
    /** Pairing builds only. Reserves one unconfirmed operation; subscriber initiates, publisher responds. */
    preparePairing(options: WiFiNanPairingOptions): WiFiNanPairing;
    /** Pairing builds, subscriber only. Queues PIN bootstrap; local confirmation is still required for authentication. */
    requestPairing(options: WiFiNanBootstrapOptions): WiFiNanPairing;
    /** Pairing builds, publisher only, Future-capable. Unconfirmed request or null on wait timeout. */
    receivePairing(options?: WiFiNanWaitOptions): WiFiNanPairing | null;
    /** Pairing builds only, Future-capable. Completed, unexpired RAM credentials for this service hash; no key bytes. */
    pairingCredentials(options?: WiFiNanWaitOptions): WiFiNanPairingCredential[];
    /** Only registered with NAN-Sync. Ready data-path-enabled subscriber required. Returns before native submission. */
    requestDataPath(options: WiFiNanDataPathOptions): WiFiNanDataPath;
    /** Only registered with NAN-Sync. Future-capable. Ready data-path-enabled publisher required. Returns null on wait timeout. */
    receiveDataPath(options?: WiFiNanWaitOptions): WiFiNanDataPath | null;
    readonly events: EventQueue<WiFiNanServiceEvent>;
    status(): WiFiNanServiceStatus;
    /** Future-capable. Waiting timeout/cancellation does not cancel service creation. */
    ready(options?: WiFiNanWaitOptions): WiFiNanServiceStatus;
    /** Future-capable. Copies input during capture. Timeout/cancellation cannot undo a submitted transmission. */
    send(options: WiFiNanSendOptions): WiFiNanSendResult;
    /** Future-capable. Success includes callback/TX retirement; a timed-out wait leaves cleanup active. */
    close(options?: WiFiNanWaitOptions): void;
    cancel(): void;
  }
  interface WiFiNanPairingCredential {
    /** Boot-local identity; replaced keys receive a new ID. Session close invalidates all its cached credentials. */
    credentialId: number;
    /** Last successfully authenticated address; a rediscovered peer may use a randomized address. */
    peerMac: number[];
    /** Device monotonic microseconds, null when the peer supplied no finite lifetime. */
    expiresAtUs: number | null;
  }
  interface WiFiNanBootstrapOptions {
    peerServiceId: number;
    peerMac: ByteSource;
    /** Entire coordination, confirmation and pairing deadline, 1..120000 ms; default 30000. */
    timeoutMs?: number;
  }
  interface WiFiNanPairingOptions {
    /** Select cached verification; omit for a new PIN pairing. Revalidated before native authentication. */
    credentialId?: number;
    /** Exact remote service ID from a discovery event, 1..255. */
    peerServiceId: number;
    /** Six-byte, nonzero unicast NAN interface address. Copied before activation. */
    peerMac: ByteSource;
    /** Entire confirmation and pairing deadline, 1..120000 ms; default 30000. */
    timeoutMs?: number;
  }
  type WiFiNanPairingConfirmation = { accept: false; pin?: never } | { accept: true; pin?: never } | {
    accept: true;
    /** Required only for PIN mode; forbidden for cached verification. Exactly six ASCII digits. */
    pin: string;
  };
  interface WiFiNanPairingStatus {
    identity: number;
    sessionIdentity: number;
    serviceIdentity: number;
    serviceId: number;
    peerServiceId: number;
    peerMac: number[];
    role: "initiator" | "responder";
    mode: "pin" | "verification";
    bootstrap: boolean;
    incoming: boolean;
    delivered: boolean;
    bootstrapAttempted: boolean;
    /** Actual bootstrap TX success and buffer retirement. Does not mean PASN success. */
    bootstrapSent: boolean;
    bootstrapTxPending: boolean;
    /** Unauthenticated remote coordination result; cannot replace local confirmation or PASN. */
    peerResponded: boolean;
    peerAccepted: boolean;
    /** Selected ID while verifying; committed replacement ID after ready. */
    credentialId: number | null;
    state: "confirmation-pending" | "pairing" | "paired" | "closing" | "closed";
    confirmed: boolean;
    rejected: boolean;
    startAttempted: boolean;
    ready: boolean;
    closeRequested: boolean;
    cleanupPending: boolean;
    timedOut: boolean;
    nativeIdentity: number | null;
    nativeActive: boolean;
    authenticated: boolean;
    /** Native protocol milestone; ready also requires final follow-up completion/retirement. */
    paired: boolean;
    trafficPending: boolean;
    /** PASN context retirement alone does not prove all frames retired. Successful close does. */
    nativeRetired: boolean;
    error: number | null;
    cleanupError: number | null;
    /** This retained framework object only; shared TX and SDK allocations have separate owners. */
    reservedBytes: number;
  }
  interface WiFiNanPairing {
    status(): WiFiNanPairingStatus;
    /** Single explicit decision. PIN mode requires pin; verification must omit it. Queues the existing worker. */
    confirm(options: WiFiNanPairingConfirmation): void;
    /** Future-capable. Wait timeout/cancellation does not revoke confirmation; the operation deadline remains. */
    ready(options?: WiFiNanWaitOptions): WiFiNanPairingStatus;
    /** Future-capable. Wait timeout/cancellation leaves native cleanup active. */
    close(options?: WiFiNanWaitOptions): void;
    /** Revokes confirmation and requests cleanup without waiting. */
    cancel(): void;
  }
  interface WiFiNanSendOptions {
    /** Attribute added to this follow-up frame. */
    vendor?: WiFiNanVendorAttribute;
    /** Exact peer service ID from a discovery event, integer 1..255. */
    peerServiceId: number;
    /** Exactly six bytes, nonzero unicast NAN interface address. */
    peerMac: ByteSource;
    /** Optional follow-up payload, 0..2048 bytes. */
    ssi?: ByteSource;
    /** 1..120000 ms, default 10000; ends the wait, retains native ownership. */
    timeoutMs?: number;
  }
  interface WiFiNanSendSnapshot {
    identity: number;
    serviceIdentity: number;
    serviceId: number;
    peerServiceId: number;
    peerMac: number[];
    bytes: number;
    /** Worker entered submission; false txSubmitted alone does not prove no transmission. */
    submitStarted: boolean;
    /** SDK acceptance; successful and failed completion results both set this. */
    txSubmitted: boolean;
    /** Null only in pre-completion error snapshots; sync NAN has native:null. */
    completion: TxCompletion<WiFiActionNativeCode | null> | null;
    /** May still be false when TX completion ends the public Future. */
    bufferRetired: boolean;
    cancelRequested: boolean;
  }
  interface WiFiNanSendResult extends WiFiNanSendSnapshot {
    completion: TxCompletion<WiFiActionNativeCode | null>;
  }
  interface WiFiNanDataPathOptions {
    peerServiceId: number;
    peerMac: ByteSource;
    /** Default true; false is rejected for secured services before native submission. */
    confirmRequired?: boolean;
    /** Establishment deadline, 1..120000 ms, default 10000. */
    timeoutMs?: number;
  }
  interface WiFiNanDataPathResponseOptions extends WiFiNanWaitOptions {
    /** Required boolean; never defaults to accept. */
    accept: boolean;
    /** At most 512 bytes. */
    ssi?: ByteSource;
  }
  interface WiFiNanDataPathStatus {
    identity: number;
    sessionIdentity: number;
    serviceIdentity: number;
    nativeIdentity: number | null;
    dataPathId: number | null;
    publisherId: number;
    direction: "outgoing" | "incoming";
    state: "pending" | "connecting" | "connected" | "closing" | "closed";
    peerMac: number[];
    peerDataMac: number[];
    localDataMac: number[];
    ipv6Identifier: number[];
    ssi: number[];
    ready: boolean;
    closeRequested: boolean;
    cleanupPending: boolean;
    timedOut: boolean;
    responseRequested: boolean;
    acceptRequested: boolean | null;
    requestSubmitted: boolean;
    responseAttempted: boolean;
    endAttempted: boolean;
    endSubmitted: boolean;
    nativeDeleted: boolean;
    framesRetired: boolean;
    nativeStatus: number;
    error: number | null;
    cleanupError: number | null;
    reservedBytes: number;
  }
  interface WiFiNanDataPath {
    status(): WiFiNanDataPathStatus;
    /** Future-capable. Wait timeout/cancel leaves the connection's own deadline unchanged. */
    ready(options?: WiFiNanWaitOptions): WiFiNanDataPathStatus;
    /** Future-capable. Explicit, single response; waits for acceptance or rejected connection retirement. */
    respond(options: WiFiNanDataPathResponseOptions): WiFiNanDataPathStatus;
    /** Future-capable. Waits for native deletion and buffer/timer/host retirement. */
    close(options?: WiFiNanWaitOptions): void;
    /** Requests close immediately. Does not acknowledge native retirement. */
    cancel(): void;
  }
  interface WiFiNanModule {
    capabilities(): WiFiNanCapabilities;
    /** Retained cleanup remains observable after the last JS Session reference is collected. */
    status(): WiFiNanGlobalStatus;
    /** Requires a healthy stopped/uninitialized Radio with no other owner. Returns before background startup completes. */
    open(options?: WiFiNanOpenOptions): WiFiNanSession;
  }
  interface WiFiNanError extends Error {
    code: "WIFI_NAN_FAILED" | "WIFI_NAN_TIMEOUT" | "WIFI_NAN_CLOSED";
    operation: "wifi.nan.open" | "WiFiNanSession.ready" | "WiFiNanSession.close" |
      "WiFiNanSession.getServiceInfo" | "WiFiNanSession.getPeerInfo" | "WiFiNanSession.getPeerRecords" |
      "WiFiNanSession.publish" | "WiFiNanSession.subscribe" | "WiFiNanService.ready" | "WiFiNanService.close" | "WiFiNanService.send" |
      "WiFiNanService.preparePairing" | "WiFiNanService.requestPairing" | "WiFiNanService.receivePairing" | "WiFiNanService.pairingCredentials" | "WiFiNanPairing.confirm" | "WiFiNanPairing.ready" | "WiFiNanPairing.close" |
      "WiFiNanService.requestDataPath" | "WiFiNanService.receiveDataPath" | "WiFiNanDataPath.ready" | "WiFiNanDataPath.respond" | "WiFiNanDataPath.close";
    details: ((WiFiNanStatus | WiFiNanServiceStatus | WiFiNanDataPathStatus | WiFiNanPairingStatus) & { espCode: number; espName: string; waitTimedOut: boolean }) |
      (WiFiNanSendSnapshot & { native: EspNativeCode; waitTimedOut: boolean });
  }

  interface WiFiWapiStatus {
    requestedEnabled: boolean;
    /** Actual lifecycle observation; null while changing or after uncertain cleanup. */
    enabled: boolean | null;
    supplicantActive: boolean;
    policyApplied: boolean;
    busy: boolean;
    policyRevision: number;
    nativeGeneration: number;
    /** An uncertain native cleanup requires a device reboot, not runtime restart. */
    restartRequired: boolean;
    runtimeCleanupPending: boolean;
    error: number | null;
    cleanupError: number | null;
  }
  interface WiFiWapiCapabilities {
    apiVersion: "wifi-wapi/1";
    stability: "candidate";
    psk: true;
    certificate: false;
    softAp: false;
    exactAuthSelection: false;
    nativeOwner: "supplicant";
    rebuildsInitializedDriver: true;
    maxTimeoutMs: 2147483647;
  }
  interface WiFiWapiControlOptions {
    /** Shared lifecycle wait budget, integer 1..2147483647 ms; default 10000. */
    timeoutMs?: number;
  }
  interface WiFiWapiModule {
    capabilities(): WiFiWapiCapabilities;
    status(): WiFiWapiStatus;
    /** Before initialization, selects the next init policy. Otherwise requires a healthy
     * stopped Radio with zero owners; reconstructs and starts the saved interfaces. */
    enable(options?: WiFiWapiControlOptions): WiFiWapiStatus;
    /** Same lifecycle boundary as enable; does not clear stored Station credentials. */
    disable(options?: WiFiWapiControlOptions): WiFiWapiStatus;
  }
  interface WiFiWapiError extends Error {
    code: "WIFI_WAPI_FAILED";
    operation: "wifi.wapi.enable" | "wifi.wapi.disable";
    details: WiFiWapiStatus & { stage: string; espCode: number };
  }

  interface WiFiModule {
    /** Present with Wi-Fi and the reviewed SDK WAPI-PSK build. */
    readonly wapi?: WiFiWapiModule;
    readonly diagnostics: WiFiDiagnosticsModule;
    /** Present with Wi-Fi and either SDK NAN_SYNC_ENABLE or NAN_USD_ENABLE; independent of IPv4. */
    readonly mesh?: WiFiMeshModule;
    readonly nan?: WiFiNanModule;
    /** Present with Wi-Fi, BSD TCP/IP, IPv4 and SDK DPP support. Candidate enrollee provisioning and explicit connection API. */
    readonly dpp?: WiFiDppModule;
    /** Present with Wi-Fi, BSD TCP/IP and IPv4. Candidate Station enrollee and build-gated AP registrar API. */
    readonly wps?: WiFiWpsModule;
    /** Present with Wi-Fi, BSD TCP/IP and IPv4. Candidate credential acquisition API. */
    readonly smartConfig?: WiFiSmartConfigModule;
    /** Present only on the reviewed C5 HE SDK build. */
    readonly twt?: WiFiTwtModule;
    /** Present only with SDK Enterprise support. */
    readonly enterprise?: WiFiEnterpriseModule;
    /** Present when RRM, WNM or 11r SDK support is enabled. */
    readonly roaming?: WiFiRoamingModule;
    readonly driver: WiFiDriverModule;
    /** Present only when SDK FTM and initiator support are enabled; check capabilities().features.ftmInitiator. */
    readonly ftm?: WiFiFtmModule;
    readonly action: WiFiActionModule;
    readonly rawTx: WiFiRawTxModule;
    readonly vendorIe: WiFiVendorIeModule;
    readonly monitor: WiFiMonitorModule;
    /** Stop/configure/start transaction. Inspect status after result-allocation OOM before retrying. */
    configure(options: WiFiConfigureOptions): WiFiStatus;
    /** Observe Wi-Fi events; does not start or own Radio. Close the returned queue. */
    watch(options?: WiFiWatchOptions): EventQueue<WiFiEvent>;
    /** Read build/API availability and current regulatory channels without starting Radio. */
    capabilities(): WiFiCapabilities;
    /** Keep the already running Radio awake. At most 16 locks; does not start or connect. */
    acquireWakeLock(): WiFiWakeLock;
    /** Cold exclusive AP, or activate AP alongside Station. On the pinned
     * C3/S3/C5 SDK, new AP configuration is verified before native AP allocation.
     * allowDisconnect:true permits replacing AP configuration through a stopped
     * transaction. Recheck status after an error before retrying. */
    startAP(options: WiFiAccessPointOptions): WiFiAccessPointStartResult;
    /** Stop AP and retire its netif. APSTA retains Station; exclusive AP deinitializes Radio.
     * Shared wait budget is an integer 1..2147483647 ms, default 1000; SDK calls are not preemptible. */
    stopAP(options?: WiFiWaitOptions): WiFiStatus;
    /** Snapshot of this running AP's clients; optional later local DHCP IPv4 observation. */
    apClients(options?: WiFiAPClientsOptions): WiFiAPClient[];
    /** Target the MAC currently associated when the Wi-Fi-task command runs.
     * True means SDK request acceptance, not confirmed departure; false means absent.
     * Requires a healthy managed AP. Does not accept an AID or a broadcast selector. */
    deauthClient(address: string): boolean;
    /** Read the initialized driver's current interface MAC; never starts Wi-Fi. */
    getMac(iface: WiFiInterface): string;
    /** Change an initialized, fully stopped and owner-free driver's interface MAC. */
    setMac(iface: WiFiInterface, address: string): string;
    /** Set country on an already started, disconnected Station with no foreign owners. */
    setCountry(code: string, options?: WiFiCountryOptions): WiFiCountryStatus;
    /** Set an idle Station or exclusive AP channel; returns immediate native readback. */
    setChannel(channel: number, options?: WiFiSetChannelOptions): WiFiChannelStatus;
    readonly DEFAULT_TIMEOUT_MS: number;
    /** Bounded raw Channel State Information capture. */
    readonly csi: WiFiCsiModule;
    /** Start configured interfaces; preserves mode/storage by default, cold default Station/RAM. Does not connect Station. */
    start(options?: WiFiStartOptions): WiFiStatus;
    /** Stop after disconnect/scan completion and other feature owners exit. */
    /** An idle disconnected Enterprise binding is retired before helper release;
     * configuration survives. Failed cleanup retains the stop transaction. */
    stop(options?: WiFiWaitOptions): WiFiStatus;
    status(): WiFiStatus;
    /** Set station modem power saving and return the active mode. */
    setPowerSave(mode: WiFiPowerSaveMode): WiFiPowerSaveMode;
    /** Set the shared radio maximum TX power and return the mapped actual dBm. */
    setTxPower(dbm: number): number;
    connect(ssid: string | ByteSource, options?: WiFiConnectOptions): WiFiConnectResult;
    disconnect(options?: WiFiWaitOptions): WiFiStatus;
    scan(options?: WiFiScanOptions): WiFiScanResult;
  }

  type WiFiCsiConfigSchema = "wifi-csi-legacy/1" | "wifi-csi-he/1";
  type WiFiCsiSourceMode = "associated" | "promiscuous";
  type WiFiCsiSampleEncoding =
    | "signed-int8"
    | "signed-int12-le"
    | "signed-int12-packed"
    | "unknown";
  type WiFiCsiPhyFormat =
    | "legacy"
    | "ht"
    | "vht"
    | "he-su"
    | "he-mu"
    | "he-er-su"
    | "he-tb"
    | "unknown";

  interface WiFiCsiCapabilities {
    readonly apiVersion: "wifi-csi/1";
    readonly target: string;
    readonly idfVersion: string;
    readonly configSchema: WiFiCsiConfigSchema;
    readonly sources: WiFiCsiSourceMode[];
    readonly phyFormats: WiFiCsiPhyFormat[];
    readonly sampleEncodings: Exclude<WiFiCsiSampleEncoding, "unknown">[];
    readonly packetCapture: {
      header: true; full: true; required: true; requireComplete: true;
      maxHeaderBytes: number; maxPacketBytes: number;
      fcs: "unknown"; payloadRepresentation: "unknown"; qualification: "candidate";
    };
    readonly radio: {
      country: string | null;
      policy: "auto" | "manual" | null;
      bands: Array<{
        band: "2.4GHz" | "5GHz";
        allowedChannels: number[] | null;
      }>;
    };
    readonly limits: {
      maxCsiBytes: number;
      maxPoolCapacity: number;
      /** Shared slot budget across allocating, active, retained and retiring CSI pools. */
      maxTotalPoolCapacity: number;
      maxPoolGenerations: number;
      maxQueueCapacity: number;
      maxBatchFrames: number;
      scaleMinimum: number | null;
      scaleMaximum: number | null;
      shiftMinimum: number | null;
      shiftMaximum: number | null;
    };
    readonly supports: {
      fixedChannel: boolean;
      promiscuous: boolean;
      sourceMacFilter: boolean;
      destinationMacFilter: boolean;
      bssidFilter: boolean;
      frameTypeFilter: boolean;
      frameFilter: boolean;
      frameSubtypeFilter: boolean;
      rssiFilter: boolean;
      nativeDecimation: boolean;
      nativeRateLimit: boolean;
      vht: boolean;
      he: boolean;
      heStbcSelection: boolean;
      captureConfigReadback: boolean;
      manualScaling: boolean;
      lltfBitMode: boolean;
      layout: true;
    };
  }

  interface WiFiCsiLegacyCaptureConfig {
    schema: "wifi-csi-legacy/1";
    lltf?: boolean;
    htLtf?: boolean;
    stbcHtLtf2?: boolean;
    ltfMerge?: boolean;
    adjacentSubcarrierFilter?: boolean;
    scale?: "auto" | { shiftBits: number };
    dumpAck?: boolean;
  }

  interface WiFiCsiHeCaptureConfig {
    schema: "wifi-csi-he/1";
    enableLegacy?: boolean;
    forceLegacyLtf?: boolean;
    ht20?: boolean;
    ht40?: boolean;
    vht?: boolean;
    heSu?: boolean;
    heMu?: boolean;
    heDcm?: boolean;
    heBeamformed?: boolean;
    heStbcLtf?: "first" | "second" | "alternate";
    valueScale?: number;
    dumpAck?: boolean;
    lltfBits?: 8 | 12;
  }

  interface WiFiCsiCaptureReadback {
    radioGeneration: number;
    /** Actual HE configuration; enable is an SDK config bit, not Session liveness. */
    capture: Required<WiFiCsiHeCaptureConfig> & { enable: boolean };
  }

  type WiFiCsiSource =
    | { mode: "associated" }
    | { mode: "promiscuous"; channel?: "current" | number };

  interface WiFiCsiPacketCaptureOptions {
    /** Default none. Header capture keeps a complete parsed MAC header. */
    content?: "none" | "header" | "full";
    /** Default 1600, 36..16384 for header/full. */
    snapLength?: number;
    /** Drop the entire observation if no proven, parsed packet is available. */
    required?: boolean;
    /** Implies required and, when content is omitted, full. Completeness is relative to the driver report. */
    requireComplete?: boolean;
  }
  interface WiFiCsiPacketInfo extends WiFiPacketInfo {
    capture: WiFiPacketInfo["capture"] & {
      /** Proven readable prefix bounded by the driver's packet report. */
      readableLength: number;
      /** Raw SDK payload_len; not a body length or permission to read. */
      driverPayloadLength: number;
      payloadRepresentation: "unknown";
    };
  }

  interface WiFiCsiOpenOptions {
    packet?: WiFiCsiPacketCaptureOptions;
    source?: WiFiCsiSource;
    capture: WiFiCsiLegacyCaptureConfig | WiFiCsiHeCaptureConfig;
    filter?: WiFiRxFilter;
    buffering?: {
      /** Defaults to the build maximum; allocated before Radio changes. */
      poolCapacity?: number;
      /** Defaults to min(build queue default, poolCapacity). */
      queueCapacity?: number;
      overflow?: "drop-newest";
    };
  }

  type WiFiCsiState =
    | "running"
    | "stopped"
    | "stopping"
    | "faulted"
    | "closed";

  type WiFiCsiErrorCode =
    | "WIFI_CSI_NOT_COMPILED"
    | "WIFI_CSI_NOT_SUPPORTED"
    | "WIFI_CSI_ALREADY_OPEN"
    | "WIFI_CSI_NOT_OPEN"
    | "WIFI_CSI_NOT_RUNNING"
    | "WIFI_CSI_CLOSING"
    | "WIFI_CSI_CONFIG_SCHEMA_MISMATCH"
    | "WIFI_CSI_CONFIG_UNSUPPORTED"
    | "WIFI_CSI_CONFIG_INVALID"
    | "WIFI_CSI_RADIO_CONFLICT"
    | "WIFI_CSI_CHANNEL_CONFLICT"
    | "WIFI_CSI_REGULATORY_CONFLICT"
    | "WIFI_CSI_PROMISCUOUS_CONFLICT"
    | "WIFI_CSI_RESOURCE_EXHAUSTED"
    | "WIFI_CSI_IDENTITY_EXHAUSTED"
    | "WIFI_CSI_FRAME_TOO_LARGE"
    | "WIFI_CSI_STALE_FRAME"
    | "WIFI_CSI_DRIVER_ERROR"
    | "WIFI_CSI_CLEANUP_PENDING";

  interface WiFiCsiError extends NativeError {
    code: WiFiCsiErrorCode;
    operation: "wifi.csi";
    details: {
      stage?: string | null;
      espCode?: number;
      espName?: string;
      requestedChannel?: number;
      effectiveChannel?: number;
      radioGeneration?: number;
      configSchema?: WiFiCsiConfigSchema;
    };
  }

  interface WiFiCsiStatus {
    generation: number;
    state: WiFiCsiState;
    /** Normalized, detached configuration; valid input to configure/open subject to lifecycle admission. */
    requested: WiFiCsiOpenOptions;
    effective: {
      source: WiFiCsiSourceMode;
      channel: number;
      secondaryChannel: "none" | "above" | "below";
      radioGeneration: number;
      configSchema: WiFiCsiConfigSchema;
      maxCsiBytes: number;
      queueCapacity: number;
      poolCapacity: number;
      powerSave: string;
      timestampAccuracy: "callback-time";
    };
    lastError: WiFiCsiError | null;
  }

  interface WiFiCsiStats {
    packetUnavailable: number;
    packetMalformed: number;
    packetTruncated: number;
    droppedPacketRequired: number;
    droppedPacketIncomplete: number;
    receivedPacketBytes: number;
    callbacks: number;
    accepted: number;
    deliveredFrames: number;
    deliveredBatches: number;
    filteredMac: number;
    filteredBssid: number;
    filteredFrameType: number;
    filteredFrameSubtype: number;
    droppedIdentityExhausted: number;
    filteredRssi: number;
    filteredDecimation: number;
    filteredRateLimit: number;
    filteredFirstWordInvalid: number;
    filteredChannelEstimateInvalid: number;
    invalidCallbackData: number;
    droppedPoolFull: number;
    droppedQueueFull: number;
    droppedFrameTooLarge: number;
    droppedClosing: number;
    receivedBytes: number;
    leasedFrames: number;
    freePoolSlots: number;
    queue: EventQueueStats;
  }

  interface WiFiCsiSegment {
    type:
      | "lltf"
      | "ht-ltf"
      | "stbc-ht-ltf2"
      | "vht-ltf"
      | "he-ltf1"
      | "he-ltf2"
      | "mixed"
      | "unknown";
    offsetBytes: number;
    lengthBytes: number;
    iqPairCount: number;
    subcarrierRanges: Array<{ start: number; end: number }>;
    nullSubcarriers: number[];
  }

  interface WiFiCsiFrameInfo extends WiFiRxInfo {
    packet: WiFiCsiPacketInfo | null;
    generation: number;
    validity: {
      firstWordInvalid: boolean;
      channelEstimateValid: boolean | null;
      callbackDataValid: true;
      layoutKnown: boolean;
    };
    layout: {
      schema: string;
      componentOrder: "imaginary-real";
      sampleEncoding: WiFiCsiSampleEncoding;
      sampleBits: 8 | 12 | null;
      byteLength: number;
      iqPairCount: number;
      trailingPaddingBytes: number;
      segments: WiFiCsiSegment[];
    };
  }

  interface WiFiCsiBatchReceiveOptions {
    /** Defaults to min(build batch limit, this Session's queueCapacity). */
    maximumFrames?: number;
    minimumFrames?: number;
    timeoutMs?: number;
    maximumLatencyMs?: number;
  }

  class WiFiCsiFrame {
    private constructor();
    readonly info: WiFiCsiFrameInfo;
    samples(): ByteView;
    copySamples(): ByteView;
    /** One-shot raw imaginary-real samples; no wire envelope. */
    sampleSource(): ByteSpanSource;
    /** Same slot/generation as samples; null when packet capture was unavailable or disabled. */
    packetBytes(): ByteView | null;
    copyPacketBytes(): ByteView | null;
    packetSource(): ByteSpanSource | null;
    /** One-frame sole-v1 wire envelope; independently retains the native payload. */
    source(options: { format: "esp32qjs-csi/1" }): ByteSpanSource;
    close(): boolean;
  }

  class WiFiCsiBatch {
    private constructor();
    readonly frameCount: number;
    info(index: number): WiFiCsiFrameInfo;
    samples(index: number): ByteView;
    packetBytes(index: number): ByteView | null;
    source(options: { format: "esp32qjs-csi/1" }): ByteSpanSource;
    close(): boolean;
  }

  class WiFiCsiSession {
    private constructor();
    status(): WiFiCsiStatus;
    /** Requires supports.captureConfigReadback, a live CSI Radio lease and a stable started driver. */
    getCaptureConfig(): WiFiCsiCaptureReadback;
    stats(): WiFiCsiStats;
    receive(timeoutMs?: number): WiFiCsiFrame | null;
    receiveBatch(options?: WiFiCsiBatchReceiveOptions): WiFiCsiBatch | null;
    stop(): WiFiCsiStatus;
    configure(options: WiFiCsiOpenOptions): WiFiCsiStatus;
    start(): WiFiCsiStatus;
    close(): boolean;
  }

  interface WiFiCsiModule {
    capabilities(): WiFiCsiCapabilities;
    open(options: WiFiCsiOpenOptions): WiFiCsiSession;
  }

  type EspNowAddress = string;
  type EspNowChannel = "current" | number;

  interface EspNowCapabilities {
    readonly apiVersion: "v1";
    readonly target: string;
    readonly idfVersion: string;
    readonly supported: true;
    readonly maxPeers: number;
    readonly maxEncryptedPeers: number;
    readonly maxV1PayloadBytes: 250;
    readonly maxPayloadBytes: number;
    readonly v2Payloads: boolean;
    readonly stationInterface: true;
    readonly softApInterface: false;
    readonly powerSave: boolean;
    readonly peerRateConfig: true;
    readonly broadcastRateConfig: true;
    readonly txQueue: true;
    readonly maxTxQueuePackets: number;
  }

  type EspNowPowerSaveOptions =
    | {
        enabled?: true;
        wakeWindowMs: number;
        wakeIntervalMs: number;
      }
    | {
        enabled: false;
        wakeWindowMs?: never;
        wakeIntervalMs?: never;
      };

  interface EspNowOpenOptions {
    interface?: "station";
    channel?: EspNowChannel;
    maxPayloadBytes?: number;
    receiveCapacity?: number;
    sendTimeoutMs?: number;
    pmk?: ByteSource;
    powerSave?: EspNowPowerSaveOptions;
    broadcastRateConfig?: EspNowPeerRateConfig;
    txQueue?: {
      capacityPackets: number;
      overflow?: PacketQueueOverflow;
    };
  }

  interface EspNowTxQueueStatus {
    enabled: boolean;
    capacityPackets: number;
    overflow: PacketQueueOverflow | null;
    active: boolean;
    queuedBatches: number;
    queuedPackets: number;
    highWaterPackets: number;
    acceptedBatches: number;
    acceptedPackets: number;
    rejectedBatches: number;
    rejectedPackets: number;
    evictedBatches: number;
    evictedPackets: number;
    /** Batches settled by the queue completion worker, including submit rejection. */
    settledBatches: number;
    completedPackets: number;
    failedPackets: number;
    submittedPackets: number;
    /** Completed callbacks plus submit rejections settled by the worker. */
    settledPackets: number;
    succeededPackets: number;
    unknownPackets: number;
    submitRejectedPackets: number;
    /** Deadline observations; do not imply that native transmission was prevented. */
    timedOutPackets: number;
    lastCompletion: EspNowCompletion | null;
    lastError: EspNativeCode | null;
  }

  interface EspNowStatus {
    open: boolean;
    interface: "station";
    channel: number;
    channelGeneration: number;
    channelSynchronized: boolean;
    maxPayloadBytes: number;
    v1Compatible: boolean;
    broadcastRateConfig: EspNowPeerRateConfig | null;
    peerCount: number;
    encryptedPeerCount: number;
    pendingSends: number;
    txRecovering: boolean;
    recoveryRequired: boolean;
    receivedPackets: number;
    receivedBytes: number;
    droppedPackets: number;
    malformedPackets: number;
    sentPackets: number;
    sentBytes: number;
    sendSuccesses: number;
    /** Only ESP_NOW_SEND_FAIL callbacks. */
    sendFailures: number;
    sendUnknowns: number;
    sendRejections: number;
    sendTimeouts: number;
    txQueue: EspNowTxQueueStatus;
    powerSave: {
      /** Last accepted request below; faulted means close is required. */
      faulted: boolean;
      restorePending: boolean;
      enabled: boolean;
      wakeWindowMs: number;
      wakeIntervalMs: number;
    };
  }

  interface EspNowPeerRateConfig {
    phyMode: "ht20" | "ht40" | "he20";
    mcs: 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9;
    guardInterval: "long" | "short";
    ersu?: boolean;
    dcm?: boolean;
  }

  interface EspNowPeerOptions {
    address: EspNowAddress;
    channel?: EspNowChannel;
    encrypted?: boolean;
    lmk?: ByteSource;
    rateConfig?: EspNowPeerRateConfig;
  }

  interface EspNowPeerUpdateOptions {
    channel?: EspNowChannel;
    encrypted?: boolean;
    lmk?: ByteSource;
  }

  interface EspNowPeerStatus {
    open: boolean;
    address: EspNowAddress;
    channel: number | "current";
    encrypted: boolean;
    rateConfig: EspNowPeerRateConfig | null;
  }

  interface EspNowReceiveEvent {
    type: "message";
    sequence: number;
    timestampUs: number;
    sourceAddress: EspNowAddress;
    destinationAddress: EspNowAddress;
    broadcast: boolean;
    rssi: number;
    channel: number;
    data: ByteView;
  }

  interface EspNowSendOptions {
    timeoutMs?: number;
  }

  interface EspNowSendResult {
    address: EspNowAddress;
    bytes: number;
    completion: EspNowCompletion;
    completedAtUs: number;
  }

  type EspNowTxPayload = ByteSource | ByteSpanSource;

  type EspNowTxPacket =
    | { data: EspNowTxPayload; parts?: never }
    | { data?: never; parts: ArrayLike<EspNowTxPayload> };

  interface EspNowEnqueueResult {
    accepted: boolean;
    reason: "queue-full" | null;
    batchSequence: number | null;
    packets: number;
    bytes: number;
    evictedBatches: number;
    evictedPackets: number;
    queuedBatches: number;
    queuedPackets: number;
  }

  type EspNowErrorCode =
    | "ESPNOW_NOT_SUPPORTED"
    | "ESPNOW_POWER_SAVE_FAILED"
    | "ESPNOW_NOT_OPEN"
    | "ESPNOW_ALREADY_OPEN"
    | "ESPNOW_STALE_SESSION"
    | "ESPNOW_STALE_PEER"
    | "ESPNOW_INVALID_ADDRESS"
    | "ESPNOW_PEER_NOT_FOUND"
    | "ESPNOW_PEER_TABLE_FULL"
    | "ESPNOW_ENCRYPTED_PEER_LIMIT"
    | "ESPNOW_INVALID_KEY"
    | "ESPNOW_ENCRYPTED_BROADCAST"
    | "ESPNOW_PAYLOAD_TOO_LARGE"
    | "ESPNOW_CHANNEL_MISMATCH"
    | "ESPNOW_CHANNEL_CONFLICT"
    | "ESPNOW_SEND_FAILED"
    | "ESPNOW_SEND_TIMEOUT"
    | "ESPNOW_RECOVERY_PENDING"
    | "ESPNOW_RECOVERY_FAILED"
    | "ESPNOW_QUEUE_FULL"
    | "ESPNOW_TX_QUEUE_DISABLED"
    | "ESPNOW_CLOSING"
    | "ESPNOW_CLEANUP_PENDING";

  interface EspNowError extends NativeError {
    code: EspNowErrorCode;
    operation: "espnow";
    details: {
      native: EspNativeCode;
      address: string | null;
      channel: number | null;
    };
  }

  class EspNowPeer {
    private constructor();
    status(): EspNowPeerStatus;
    send(data: ByteSource, options?: EspNowSendOptions): EspNowSendResult;
    enqueue(packet: EspNowTxPacket): EspNowEnqueueResult;
    enqueueBatch(packets: ArrayLike<EspNowTxPacket>): EspNowEnqueueResult;
    update(options: EspNowPeerUpdateOptions): EspNowPeerStatus;
    remove(): boolean;
  }

  class EspNowSession implements EventQueue<EspNowReceiveEvent> {
    private constructor();
    receive(timeoutMs?: number): EspNowReceiveEvent | null;
    stats(): EventQueueStats;
    status(): EspNowStatus;
    addPeer(options: EspNowPeerOptions): EspNowPeer;
    peer(address: EspNowAddress): EspNowPeer | null;
    peers(): EspNowPeerStatus[];
    broadcast(data: ByteSource,
              options?: EspNowSendOptions): EspNowSendResult;
    enqueueBroadcast(packet: EspNowTxPacket): EspNowEnqueueResult;
    enqueueBroadcastBatch(
      packets: ArrayLike<EspNowTxPacket>): EspNowEnqueueResult;
    flushTx(timeoutMs?: number): boolean;
    setPowerSave(options: EspNowPowerSaveOptions): boolean;
    recover(): EspNowStatus;
    /** Native cleanup must finish before success. Failure/timeout retains closing state; close can retry. */
    close(): boolean;
  }

  interface EspNowModule {
    readonly BROADCAST_ADDRESS: "ff:ff:ff:ff:ff:ff";
    readonly MAX_PAYLOAD_V1: 250;
    readonly MAX_PAYLOAD_V2: number;
    capabilities(): EspNowCapabilities;
    open(options?: EspNowOpenOptions): EspNowSession;
  }

  type BLERole = "central" | "peripheral";
  type BLEAddressType =
    | "public"
    | "random-static"
    | "random-private-resolvable"
    | "random-private-nonresolvable";

  interface BLEAddress {
    address: string;
    type: BLEAddressType;
  }

  interface BLECapabilities {
    readonly apiVersion: "v1";
    readonly target: string;
    readonly idfVersion: string;
    readonly supported: true;
    readonly classic: false;
    readonly central: boolean;
    readonly peripheral: boolean;
    readonly observer: boolean;
    readonly broadcaster: boolean;
    readonly legacyAdvertising: true;
    readonly extendedAdvertising: false;
    readonly maxConnections: number;
    readonly maxMtu: number;
    readonly bonding: boolean;
    readonly privacy: boolean;
    readonly concurrentScanAdvertising: boolean;
  }

  type BLEIoCapability =
    | "none"
    | "display-only"
    | "keyboard-only"
    | "display-keyboard"
    | "display-yes-no";

  interface BLESecurityOptions {
    bonding?: boolean;
    secureConnections?: boolean;
    mitm?: boolean;
    ioCapability?: BLEIoCapability;
    pairingTimeoutMs?: number;
  }

  type BLEGattProperty =
    | "broadcast"
    | "read"
    | "write"
    | "write-without-response"
    | "notify"
    | "indicate"
    | "authenticated-signed-write";

  type BLEGattPermission =
    | "encrypted-read"
    | "encrypted-write"
    | "authenticated-read"
    | "authenticated-write";

  interface BLELocalCharacteristicDefinition {
    id: string;
    uuid: string;
    properties: BLEGattProperty[];
    permissions?: BLEGattPermission[];
    maxLength: number;
    initialValue?: ByteSource;
    storeWrites?: boolean;
  }

  interface BLELocalServiceDefinition {
    id: string;
    uuid: string;
    primary?: boolean;
    characteristics: BLELocalCharacteristicDefinition[];
  }

  interface BLEGattServerDefinition {
    services: BLELocalServiceDefinition[];
  }

  interface BLEOpenOptions {
    roles?: BLERole[];
    deviceName?: string;
    ownAddressType?: "public" | "random-static" | "rpa";
    preferredMtu?: number;
    maxConnections?: number;
    security?: BLESecurityOptions;
    server?: BLEGattServerDefinition;
  }

  interface BLEAdapterStatus {
    open: boolean;
    synchronized: boolean;
    address: string;
    addressType: BLEAddressType;
    deviceName: string;
    roles: BLERole[];
    connections: number;
    maxConnections: number;
    scanning: boolean;
    advertising: boolean;
    bondedDevices: number;
    resetCount: number;
    droppedScanReports: number;
    droppedConnectionEvents: number;
    /** Last native failure terminating an unclaimed connection; zero if none. Reset on adapter open. */
    rejectedConnectionError: number;
    droppedServerEvents: number;
  }

  interface BLEBondInfo {
    peer: BLEAddress;
    authenticated: boolean;
    secureConnections: boolean;
  }

  type BLEAdvertisementEventType =
    | "advertisement"
    | "scan-response"
    | "directed-advertisement";

  interface BLEScanOptions {
    active?: boolean;
    intervalMs?: number;
    windowMs?: number;
    durationMs?: number;
    filterDuplicates?: boolean;
    limited?: boolean;
    capacity?: number;
  }

  interface BLEScanEvent {
    type: "report";
    sequence: number;
    timestampUs: number;
    peer: BLEAddress;
    eventType: BLEAdvertisementEventType;
    rssi: number;
    connectable: boolean;
    scannable: boolean;
    directed: boolean;
    data: ByteView;
  }

  interface BLEScannerStatus {
    open: boolean;
    active: boolean;
    startedAtUs: number;
    reports: number;
    dropped: number;
    malformed: number;
    stopReason: "running" | "completed" | "closed" | "error";
  }

  class BLEScanner implements EventQueue<BLEScanEvent> {
    private constructor();
    receive(timeoutMs?: number): BLEScanEvent | null;
    stats(): EventQueueStats;
    status(): BLEScannerStatus;
    close(): boolean;
  }

  interface BLEAdvertisingOptions {
    connectable?: boolean;
    scannable?: boolean;
    intervalMinMs?: number;
    intervalMaxMs?: number;
    durationMs?: number;
    data: ByteSource;
    scanResponse?: ByteSource;
    capacity?: number;
  }

  interface BLEAdvertiserEvent {
    type: "connection";
    sequence: number;
    timestampUs: number;
    connection: BLEConnection;
  }

  interface BLEAdvertiserStatus {
    open: boolean;
    active: boolean;
    connectable: boolean;
    scannable: boolean;
    incomingConnections: number;
    dropped: number;
    stopReason: "running" | "connected" | "completed" | "closed" | "error";
  }

  class BLEAdvertiser implements EventQueue<BLEAdvertiserEvent> {
    private constructor();
    receive(timeoutMs?: number): BLEAdvertiserEvent | null;
    stats(): EventQueueStats;
    status(): BLEAdvertiserStatus;
    close(): boolean;
  }

  interface BLEConnectOptions {
    timeoutMs?: number;
    preferredMtu?: number;
  }

  interface BLEConnectionSecurityStatus {
    encrypted: boolean;
    authenticated: boolean;
    bonded: boolean;
    secureConnections: boolean;
  }

  interface BLEConnectionStatus {
    open: boolean;
    connectionId: number;
    peer: BLEAddress;
    role: BLERole;
    mtu: number;
    rssi: number | null;
    security: BLEConnectionSecurityStatus;
    gattQueued: number;
    gattActive: boolean;
  }

  type BLEPairingAction =
    | "display-passkey"
    | "input-passkey"
    | "numeric-comparison";

  type BLEConnectionEvent =
    | { type: "disconnected"; sequence: number; timestampUs: number;
        reasonCode: number; reasonName: string }
    | { type: "mtu"; sequence: number; timestampUs: number; mtu: number }
    | { type: "security"; sequence: number; timestampUs: number;
        status: BLEConnectionSecurityStatus }
    | { type: "pairing-request"; sequence: number; timestampUs: number;
        requestId: number; action: BLEPairingAction; passkey?: number;
        expiresAtUs: number };

  interface BLEDiscoverOptions {
    includeDescriptors?: boolean;
    maxServices?: number;
    maxCharacteristics?: number;
    maxDescriptors?: number;
    timeoutMs?: number;
  }

  interface BLEGattReadOptions { timeoutMs?: number; maxBytes?: number }
  interface BLEGattWriteOptions { response?: boolean; timeoutMs?: number }
  interface BLESubscribeOptions {
    mode?: "notify" | "indicate" | "auto";
    capacity?: number;
    timeoutMs?: number;
  }

  interface BLEServiceRecord {
    readonly uuid: string;
    readonly startHandle: number;
    readonly endHandle: number;
    readonly characteristicStart: number;
    readonly characteristicCount: number;
  }

  interface BLECharacteristicRecord {
    readonly uuid: string;
    readonly declarationHandle: number;
    readonly valueHandle: number;
    readonly properties: BLEGattProperty[];
    readonly serviceIndex: number;
    readonly descriptorStart: number;
    readonly descriptorCount: number;
  }

  interface BLEDescriptorRecord {
    readonly uuid: string;
    readonly handle: number;
    readonly characteristicIndex: number;
  }

  interface BLEGattDiscovery {
    readonly generation: number;
    readonly services: BLEServiceRecord[];
    readonly characteristics: BLECharacteristicRecord[];
    readonly descriptors: BLEDescriptorRecord[];
  }

  class BLEConnection implements EventQueue<BLEConnectionEvent> {
    private constructor();
    receive(timeoutMs?: number): BLEConnectionEvent | null;
    stats(): EventQueueStats;
    status(): BLEConnectionStatus;
    pair(options?: { timeoutMs?: number }): BLEConnectionSecurityStatus;
    respondPairing(requestId: number, response: boolean | number): boolean;
    exchangeMtu(mtu?: number, timeoutMs?: number): number;
    readRssi(timeoutMs?: number): number;
    discover(options?: BLEDiscoverOptions): BLEGattDiscovery;
    readHandle(handle: number, options?: BLEGattReadOptions): ByteView;
    writeHandle(handle: number, data: ByteSource,
                options?: BLEGattWriteOptions): number;
    subscribeHandle(valueHandle: number, cccdHandle: number,
                    options?: BLESubscribeOptions): BLENotificationStream;
    close(): boolean;
  }

  interface BLEValueEvent {
    type: "value";
    sequence: number;
    timestampUs: number;
    indication: boolean;
    data: ByteView;
  }

  interface BLENotificationStatus {
    open: boolean;
    mode: "notify" | "indicate";
    received: number;
    dropped: number;
  }

  class BLENotificationStream implements EventQueue<BLEValueEvent> {
    private constructor();
    receive(timeoutMs?: number): BLEValueEvent | null;
    stats(): EventQueueStats;
    status(): BLENotificationStatus;
    close(): boolean;
  }

  interface BLEServerNotifyOptions {
    connection?: BLEConnection;
    indication?: boolean;
    timeoutMs?: number;
  }

  interface BLEServerNotifyResult {
    attemptedConnections: number;
    submittedConnections: number;
    bytes: number;
    indication: boolean;
  }

  type BLEServerEvent =
    | { type: "write"; sequence: number; timestampUs: number;
        connection: BLEConnection; characteristicId: string;
        offset: number; data: ByteView }
    | { type: "subscription"; sequence: number; timestampUs: number;
        connection: BLEConnection; characteristicId: string;
        notify: boolean; indicate: boolean };

  interface BLEGattServerStatus {
    open: boolean;
    services: number;
    characteristics: number;
    activeConnections: number;
    writes: number;
    notifications: number;
    indications: number;
    droppedEvents: number;
  }

  class BLELocalCharacteristic {
    private constructor();
    readonly id: string;
    readonly uuid: string;
    readonly maxLength: number;
    readonly properties: BLEGattProperty[];
    value(): ByteView;
    setValue(data: ByteSource): number;
    notify(data?: ByteSource,
           options?: BLEServerNotifyOptions): BLEServerNotifyResult;
  }

  class BLEGattServer {
    private constructor();
    status(): BLEGattServerStatus;
    watch(options?: { capacity?: number }): EventQueue<BLEServerEvent>;
    characteristic(id: string): BLELocalCharacteristic;
  }

  type BLEErrorCode =
    | "BLE_NOT_SUPPORTED" | "BLE_NOT_OPEN" | "BLE_ALREADY_OPEN"
    | "BLE_STALE_ADAPTER" | "BLE_STALE_CONNECTION" | "BLE_STALE_ATTRIBUTE"
    | "BLE_STALE_SUBSCRIPTION" | "BLE_GAP_CONFLICT" | "BLE_TIMEOUT"
    | "BLE_QUEUE_FULL" | "BLE_CONNECTION_FAILED" | "BLE_DISCONNECTED"
    | "BLE_GATT_ERROR" | "BLE_SECURITY_ERROR" | "BLE_PAIRING_EXPIRED"
    | "BLE_PAYLOAD_TOO_LARGE" | "BLE_SERVER_LIMIT" | "BLE_CLOSING"
    | "BLE_PHY_RESTART_REQUIRED";

  interface BLEError extends NativeError {
    code: BLEErrorCode;
    operation: "ble";
    details: {
      hostCode: number;
      attCode: number | null;
      connectionId: number | null;
      attributeHandle: number | null;
    };
  }

  class BLEAdapter {
    private constructor();
    status(): BLEAdapterStatus;
    scan(options?: BLEScanOptions): BLEScanner;
    connect(peer: BLEAddress, options?: BLEConnectOptions): BLEConnection;
    advertise(options: BLEAdvertisingOptions): BLEAdvertiser;
    server(): BLEGattServer | null;
    bonds(): BLEBondInfo[];
    removeBond(peer: BLEAddress): boolean;
    clearBonds(): number;
    close(): boolean;
  }

  interface BLEModule {
    capabilities(): BLECapabilities;
    open(options?: BLEOpenOptions): BLEAdapter;
  }

  /**
   * HTTP server creation options.
   */
  interface HttpServerOptions {
    port?: number;
    host?: string;
  }

  /**
   * Fetch options accepted by `http.fetch(...)`.
   */
  interface FetchOptions extends RequestInit {
    timeoutMs?: number;
    maxBodyBytes?: number;
  }

  type HTTPErrorCode =
    | "HTTP_QUEUE_FULL"
    | "HTTP_WORKER_START_FAILED"
    | "HTTP_INTERNAL"
    | "HTTP_CANCELLED"
    | "HTTP_TIMEOUT"
    | "HTTP_REQUEST_FAILED";

  interface HTTPError extends NativeError {
    code: HTTPErrorCode;
    operation: "http.fetch";
    details: {
      espCode: number;
      espName: string;
    };
  }

  type FetchInput = string | Request;
  interface HttpFetchFunction {
    (input: FetchInput, options?: FetchOptions): Response;
  }

  /**
   * HTTP server instance created by `http.server(...)`.
   *
   * @example
   * ```js
   * var server = http.server({ port: 8080, host: "0.0.0.0" });
   * server.route("GET", "/ping");
   * server.start();
   * var request = server.receive(1000);
   * if (request !== null) server.respond(request, Response.text("pong"));
   * ```
   */
  class HttpServer implements EventQueue<Request> {
    private constructor();
    readonly port: number;
    readonly ctrlPort: number;
    readonly host: string;
    started: boolean;
    closed: boolean;
    start(): boolean;
    stop(): boolean;
    close(): boolean;
    route(method: string, path: string): boolean;
    receive(timeoutMs?: number): Request | null;
    stats(): EventQueueStats;
    respond(request: Request, response: Response): boolean;
    removeRoute(path: string, method?: string): number;
    clearRoutes(): number;
  }

  /**
   * HTTP client/server namespace. Individual helpers are feature-gated.
   *
   * @example
   * ```js
   * var response = http.fetch("https://example.com");
   * print(response.status, response.text().length);
   * ```
   *
   * @example
   * ```js
   * var server = http.server({ port: 8080 });
   * server.route("GET", "/core");
   * server.start();
   * ```
   */
  interface HttpModule {
    readonly DEFAULT_TIMEOUT_MS?: number;
    readonly MAX_BODY_BYTES?: number;
    fetch?(input: FetchInput, options?: FetchOptions): Response;
    server?(options?: HttpServerOptions): HttpServer;
  }

  type RPCPrimitive = null | boolean | number | string;
  type RPCValue =
    | RPCPrimitive
    | ByteView
    | ByteSpanSource
    | RPCValue[]
    | { [key: string]: RPCValue };

  interface RPCCodecOptions {
    /** Ordered application schema fields mapped to deterministic CBOR integer keys. */
    fields: string[];
    /** Fields whose nested maps retain text keys. */
    dynamicFields?: string[];
    /** Allow text keys in the root payload map. Defaults to false. */
    allowStringKeys?: boolean;
    /** Directory used for temporary files created by inbound streamed values. */
    streamDirectory?: string;
  }

  interface RPCMessage {
    opcode: number;
    requestId: number;
    flags: number;
    logicalLength: number;
    payload: RPCValue;
  }

  interface RPCDecoderStatus {
    open: true;
    messages: number;
    errors: number;
    streamActive: boolean;
    streamReceivedBytes: number;
    streamExpectedBytes: number;
  }

  interface RPCSourceInfo {
    size: number;
    crc32: string | null;
  }

  /** File-backed span source created or received by the RPC stream layer. */
  interface RPCFileSource extends ByteSpanSource {
    readonly __rpcFileSourceBrand: never;
  }

  interface RPCStatus {
    protocol: "esp32qjs.rpc/1";
    activeCodecs: number;
    activeDecoders: number;
    messages: number;
    errors: number;
  }

  class RPCCodec {
    private constructor();
    createDecoder(): RPCDecoder;
    encode(
      opcode: number,
      requestId: number,
      flags: number,
      payload: RPCValue,
    ): ByteView[] | ByteSpanSource;
    close(): boolean;
  }

  class RPCDecoder {
    private constructor();
    feed(data: ByteSource): RPCMessage[];
    reset(): boolean;
    status(): RPCDecoderStatus;
    close(): boolean;
  }

  interface RPCModule {
    readonly PROTOCOL: "esp32qjs.rpc/1";
    readonly RESPONSE: 4;
    readonly ERROR: 8;
    readonly MAX_FRAME_BYTES: 7740;
    readonly MAX_MESSAGE_BYTES: 65536;
    readonly MAX_STREAM_BYTES: 33554432;
    readonly SEGMENT_PAYLOAD_BYTES: 7680;
    createCodec(options: RPCCodecOptions): RPCCodec;
    bytes(value: ByteSource): ByteView;
    fileSource(path: string): RPCFileSource;
    sourceInfo(source: RPCFileSource): RPCSourceInfo;
    adoptFile(source: RPCFileSource, path: string): boolean;
    status(): RPCStatus;
  }
}

  const Headers: typeof ESP32QJS.Headers;
  const Request: typeof ESP32QJS.Request;
  const Response: typeof ESP32QJS.Response;
  const Stream: typeof ESP32QJS.Stream;
  const _ByteView: { readonly prototype: ESP32QJS.ByteView };
  const _ByteSpanSource: { readonly prototype: ESP32QJS.ByteSpanSource };
  const _BitmapSpanSource: { readonly prototype: ESP32QJS.BitmapSpanSource };
  const FsVolume: { readonly prototype: ESP32QJS.FsVolume };
  const HttpServer: typeof ESP32QJS.HttpServer;
  const DisplayFont: ESP32QJS.DisplayFontConstructor;
  const Bitmap: typeof ESP32QJS.Bitmap;
  const DisplayCommandBuffer: typeof ESP32QJS.DisplayCommandBuffer;
  const RMTSymbolBuffer: typeof ESP32QJS.RMTSymbolBuffer;
  const RMTChannel: typeof ESP32QJS.RMTChannel;
  const I2SChannel: typeof ESP32QJS.I2SChannel;
  const Camera: typeof ESP32QJS.Camera;
  const CameraFrame: typeof ESP32QJS.CameraFrame;
  const USBSerialHandle: {
    readonly prototype: ESP32QJS.USBSerialTextHandle | ESP32QJS.USBSerialBinaryHandle;
  };
  const WebSocketClientHandle: {
    readonly prototype: ESP32QJS.WebSocketClientHandle;
  };
  const I2CBus: { readonly prototype: ESP32QJS.I2CBus };
  const I2CDevice: { readonly prototype: ESP32QJS.I2CDevice };
  const SPIBus: { readonly prototype: ESP32QJS.SPIBus };
  const SPIDevice: { readonly prototype: ESP32QJS.SPIDevice };
  const EspNowSession: { readonly prototype: ESP32QJS.EspNowSession };
  const EspNowPeer: { readonly prototype: ESP32QJS.EspNowPeer };
  const WiFiWakeLock: { readonly prototype: ESP32QJS.WiFiWakeLock };
  const WiFiMonitorSession: { readonly prototype: ESP32QJS.WiFiMonitorSession };
  const WiFiFtmSession: { readonly prototype: ESP32QJS.WiFiFtmSession };
  const WiFiSmartConfigSession: { readonly prototype: ESP32QJS.WiFiSmartConfigSession };
  const WiFiWpsSession: { readonly prototype: ESP32QJS.WiFiWpsSession };
  const WiFiDppSession: { readonly prototype: ESP32QJS.WiFiDppSession };
  const WiFiMeshSession: { readonly prototype: ESP32QJS.WiFiMeshSession };
  const WiFiNanSession: { readonly prototype: ESP32QJS.WiFiNanSession };
  const WiFiNanService: { readonly prototype: ESP32QJS.WiFiNanService };
  const WiFiNanPairing: { readonly prototype: ESP32QJS.WiFiNanPairing };
  const WiFiNanDataPath: { readonly prototype: ESP32QJS.WiFiNanDataPath };
  const WiFiWpsAPSession: { readonly prototype: ESP32QJS.WiFiWpsAPSession };
  /** C5 HE only; obtain objects from wifi.twt.setupIndividual() or setupBroadcast(). */
  const WiFiTwtAgreement: { readonly prototype: ESP32QJS.WiFiTwtAgreement };
  /** RRM builds only. Obtain instances from wifi.roaming.requestNeighborReport(). */
  const WiFiNeighborReportRequest: { readonly prototype: ESP32QJS.WiFiNeighborReportRequest };
  const WiFiRocSession: { readonly prototype: ESP32QJS.WiFiRocSession };
  const WiFiRawTxSession: { readonly prototype: ESP32QJS.WiFiRawTxSession };
  const WiFiRawPeriodicTx: { readonly prototype: ESP32QJS.WiFiRawPeriodicTx };
  const WiFiMonitorFrame: { readonly prototype: ESP32QJS.WiFiMonitorFrame };
  const WiFiMonitorBatch: { readonly prototype: ESP32QJS.WiFiMonitorBatch };
  const WiFiCsiSession: { readonly prototype: ESP32QJS.WiFiCsiSession };
  const WiFiCsiFrame: { readonly prototype: ESP32QJS.WiFiCsiFrame };
  const WiFiCsiBatch: { readonly prototype: ESP32QJS.WiFiCsiBatch };
  const BLEAdapter: { readonly prototype: ESP32QJS.BLEAdapter };
  const BLEScanner: { readonly prototype: ESP32QJS.BLEScanner };
  const BLEAdvertiser: { readonly prototype: ESP32QJS.BLEAdvertiser };
  const BLEConnection: { readonly prototype: ESP32QJS.BLEConnection };
  const BLENotificationStream: { readonly prototype: ESP32QJS.BLENotificationStream };
  const BLEGattServer: { readonly prototype: ESP32QJS.BLEGattServer };
  const BLELocalCharacteristic: { readonly prototype: ESP32QJS.BLELocalCharacteristic };
  const UARTPort: { readonly prototype: ESP32QJS.UARTPort };
  const TCPSocket: { readonly prototype: ESP32QJS.TCPSocket };
  const TCPListener: { readonly prototype: ESP32QJS.TCPListener };
  const UDPSocket: { readonly prototype: ESP32QJS.UDPSocket };
  const RPCCodec: typeof ESP32QJS.RPCCodec;
  const RPCDecoder: typeof ESP32QJS.RPCDecoder;
  const Future: ESP32QJS.FutureFactory;
  const EventQueue: {
    readonly prototype: ESP32QJS.EventQueue<unknown>;
  };

  /**
   * Print a hint pointing to the generated API docs and declaration files.
   *
   * @example
   * ```js
   * help();
   * ```
   */
  function help(): void;

  /**
   * Print one line to the serial console.
   *
   * @example
   * ```js
   * print("hello", 123);
   * ```
   */
  function print(...values: unknown[]): void;

  /** Force a JavaScript garbage collection cycle. */
  function gc(): void;

  /**
   * Load and evaluate a LittleFS script.
   *
   * @example
   * ```js
   * load("_sys/display.js");
   * ```
   */
  function load(path: string): unknown;

  /** Block the runtime for the given number of milliseconds. */
  function sleep(ms: number): number;

  function setTimeout(fn: () => void, ms: number): ESP32QJS.TimerHandle;
  function clearTimeout(handle: ESP32QJS.TimerHandle): void;
  function setInterval(fn: () => void, ms: number): ESP32QJS.TimerHandle;
  function clearInterval(handle: ESP32QJS.TimerHandle): void;

  /** Immutable filesystem volume; initially bound to `/littlefs`. */
  var fs: ESP32QJS.FsVolume;
  /** Read-only system framework loader rooted below `/_sys`. */
  var framework: ESP32QJS.FrameworkModule;
  /** Bounded strings in the default NVS partition. */
  var nvs: ESP32QJS.NVSModule;
  /** GPIO helpers for the optional wiring profile. */
  var gpio: ESP32QJS.GpioModule;
  /** LEDC PWM timer/channel helpers. */
  var ledc: ESP32QJS.LedcModule;
  /** ADC oneshot helpers. */
  var adc: ESP32QJS.AdcModule;
  /** DAC oneshot helpers. Exposed only when `sys.info.features.dac` is enabled. */
  var dac: ESP32QJS.DacModule;
  /** System runtime information and deadline helpers. */
  var sys: ESP32QJS.SysModule;
  /** Bounded runtime log ring used by headless Agent profiles. */
  var runtimeLogs: ESP32QJS.RuntimeLogsModule;
  /** Shared I2C bus helpers. */
  var i2c: ESP32QJS.I2CModule;
  /** SPI master bus/device helpers. */
  var spi: ESP32QJS.SPIModule;
  /** Synchronous UART port helpers. */
  var uart: ESP32QJS.UARTModule;
  /** Generic RMT receive/transmit channels and native symbol buffers. */
  var rmt: ESP32QJS.RMTModule;
  /** Standard-I2S receive/transmit/duplex and receive-only PDM channels. */
  var i2s: ESP32QJS.I2SModule;
  /** Explicit single-frame camera capture. Available only on supported targets. */
  var camera: ESP32QJS.CameraModule;
  /** Transport-neutral status for every registered ESP-NETIF interface. */
  var net: ESP32QJS.NetModule;
  /** Headless USB Serial/JTAG framed transport; unavailable when the REPL is compiled in. */
  var usbSerial: ESP32QJS.USBSerialModule;
  /** Generic TCP and UDP socket namespace. */
  var socket: ESP32QJS.SocketModule;
  /** Outbound WebSocket text/binary client. */
  var websocketClient: ESP32QJS.WebSocketClientModule;
  /** Native Bitmap helpers. Exposed only when `sys.info.features.bitmap` is enabled. */
  var bitmap: ESP32QJS.BitmapModule;
  /** Wi-Fi station helpers. */
  var wifi: ESP32QJS.WiFiModule;
  /** Station-interface ESP-NOW sessions, peers, receive queues, and sends. */
  var espNow: ESP32QJS.EspNowModule;
  /** Generic ESP-NimBLE central, peripheral, GATT, and security API. */
  var ble: ESP32QJS.BLEModule;
  /** HTTP client/server namespace. Exposed when either `sys.info.features.http` or `.httpServer` is enabled. */
  var http: ESP32QJS.HttpModule;
  /** Generic deterministic CBOR and COBS application-protocol codec. */
  var rpc: ESP32QJS.RPCModule;

}

export {};
