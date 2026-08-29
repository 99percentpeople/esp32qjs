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
    receiverPending: boolean;
  }

  /**
   * Byte payload accepted by low-level transports.
   *
   * Plain array-like values are copied by the transport. Native `ByteView`
   * values returned by modules such as `bitmap.readRect(...)` can be
   * passed directly without first converting them to JavaScript arrays.
   */
  type ByteSource = ArrayLike<number> | ByteView;

  /**
   * Native byte view with a read-only JavaScript surface.
   *
   * The view keeps its native owner alive while synchronous transports consume
   * it. Some producers reuse their backing storage on later exports, so keep a
   * view only for immediate synchronous use unless the producer documents a
   * snapshot. Use `toArray()` for inspection, compatibility code, or when a
   * stable JavaScript copy is required. Call `close()` after the last consumer
   * or conversion so owned native storage is released deterministically.
   */
  interface ByteView {
    readonly length: number;
    readonly byteLength: number;
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
    /** Close the source and release its producer-owned resources. Idempotent. */
    close(): boolean;
  }

  /** Retained Bitmap byte span source. */
  interface BitmapSpanSource extends ByteSpanSource {
    setRect(x: number, y: number, width: number, height: number): this;
  }

  type BitmapFormat = "mono1" | "gray8" | "rgb565" | "rgb888";
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
    /** Available only for `gray8` and `mono1` outputs. */
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

  interface BitmapReadRectOptions {
    byteOrder?: DisplayByteOrder;
  }

  interface BitmapReadRectChunksOptions
    extends BitmapReadRectOptions {
    chunkBytes?: number;
    /**
     * Let direct full-row exports reuse the internal chunk array and ByteView
     * wrappers. Use only for immediate synchronous writes; reused chunks are not
     * snapshots.
     */
    reuse?: boolean;
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
   * HTTP request object shared by `fetch(...)` and `http.server(...)`.
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
   * HTTP response object shared by `fetch(...)` and `http.server(...)`.
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
   * Pixel colors are packed numeric values: mono1 uses 0/1, gray8 uses 8-bit
   * intensity, rgb565 uses 16-bit RGB565, and rgb888 uses 0xRRGGBB. Drawing
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
    readonly wifi: boolean;
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

  interface SysMemoryManagerStatus {
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
  }

  interface SysAsyncPollerResourceStatus {
    registered: number;
    capacity: number;
  }

  interface SysRuntimeResourcesStatus {
    timers: SysTimerResourceStatus;
    futures: SysFutureResourceStatus;
    eventQueues: SysEventQueueResourceStatus;
    asyncPollers: SysAsyncPollerResourceStatus;
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
  type CameraSensorModel = "ov2640" | "ov3660";

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

  interface USBSerialTextHandle extends EventQueue<string> {
    send(text: string): number;
    status(): USBSerialStatus;
  }

  interface USBSerialBinaryHandle extends EventQueue<ByteView> {
    send(data: ByteSource | ByteSpanSource): number;
    status(): USBSerialStatus;
  }

  /** Headless USB Serial/JTAG NDJSON transport; mutually exclusive with the REPL. */
  interface USBSerialModule {
    readonly MAX_FRAME_BYTES: number;
    open(options?: USBSerialTextOptions): USBSerialTextHandle;
    open(options: USBSerialBinaryOptions): USBSerialBinaryHandle;
    close(): boolean;
    send(data: string | ByteSource | ByteSpanSource): number;
    status(): USBSerialStatus;
  }

  interface WebSocketClientOpenOptions {
    url: string;
    authorization?: string;
    subprotocol?: string;
    autoReconnect?: boolean;
    reconnectMs?: number;
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
        reconnecting: boolean;
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

  interface WebSocketClientHandle extends EventQueue<WebSocketClientEvent> {
    send(data: string | ByteSource | ByteSpanSource): number;
    status(): WebSocketClientStatus;
  }

  /** Singleton outbound WebSocket text/binary client. */
  interface WebSocketClientModule {
    readonly MAX_MESSAGE_BYTES: number;
    open(options: WebSocketClientOpenOptions): WebSocketClientHandle;
    close(): boolean;
    send(data: string | ByteSource | ByteSpanSource): number;
    status(): WebSocketClientStatus;
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
  type WiFiPowerSaveMode = "none" | "minimum" | "maximum";

  interface WiFiRadioStatus {
    generation: number;
    initialized: boolean;
    starting: boolean;
    started: boolean;
    mode: WiFiRadioMode;
    channel: number | null;
    channelGeneration: number;
    maxTxPowerDbm: number | null;
    powerSave: WiFiPowerSaveMode | null;
    clients: {
      total: number;
      wifiStation: number;
      espNow: number;
    };
  }

  interface WiFiStatus {
    initialized: boolean;
    started: boolean;
    connected: boolean;
    scanning: boolean;
    ssid: string;
    lastDisconnectReason: number;
    lastDisconnectReasonName: WiFiDisconnectReasonName;
    radio: WiFiRadioStatus;
  }

  /**
   * One access-point result from `wifi.scan()`.
   */
  interface WiFiScanResult {
    ssid: string;
    bssid: string;
    rssi: number;
    channel: number;
    authMode: WiFiAuthMode;
    hidden: boolean;
  }

  interface WiFiScanOptions {
    /** Limit the scan to one target-supported channel. Omit to scan all channels. */
    channel?: number;
    /** Include access points that do not advertise an SSID. Defaults to true. */
    showHidden?: boolean;
    /** Use a passive scan instead of transmitting probe requests. */
    passive?: boolean;
    /** Per-channel scan duration in milliseconds, from 1 through 1500. */
    dwellMs?: number;
    /** Whole-operation deadline in milliseconds, from 1 through 60000. */
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

  interface WiFiConnectOptions {
    password?: string;
    timeoutMs?: number;
    /** Lock association to one BSSID. */
    bssid?: string;
    /** Hint or lock association to one target-supported channel. */
    channel?: number;
    scanMethod?: "fast" | "all";
    sortMethod?: "signal" | "security";
    minimumRssi?: number;
    minimumAuthMode?: WiFiConnectAuthMode;
    pmf?: "disabled" | "capable" | "required";
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
    operation: "wifi" | "wifi.scan" | "wifi.connect" | "wifi.disconnect";
    details: {
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
  interface WiFiModule {
    readonly DEFAULT_TIMEOUT_MS: number;
    status(): WiFiStatus;
    /** Set station modem power saving and return the active mode. */
    setPowerSave(mode: WiFiPowerSaveMode): WiFiPowerSaveMode;
    /** Set the shared radio maximum TX power and return the mapped actual dBm. */
    setTxPower(dbm: number): number;
    connect(ssid: string, options?: WiFiConnectOptions): WiFiStatus;
    disconnect(timeoutMs?: number): WiFiStatus;
    scan(options?: WiFiScanOptions): WiFiScanResult[];
  }

  type EspNowAddress = string;
  type EspNowChannel = "current" | number;

  interface EspNowCapabilities {
    readonly maxPeers: number;
    readonly maxEncryptedPeers: number;
    readonly maxV1PayloadBytes: 250;
    readonly maxPayloadBytes: number;
    readonly v2Payloads: boolean;
    readonly stationInterface: true;
    readonly softApInterface: false;
    readonly powerSave: boolean;
    readonly peerRateConfig: false;
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
  }

  interface EspNowStatus {
    open: boolean;
    interface: "station";
    channel: number;
    channelGeneration: number;
    channelSynchronized: boolean;
    maxPayloadBytes: number;
    v1Compatible: boolean;
    peerCount: number;
    encryptedPeerCount: number;
    pendingSends: number;
    txRecovering: boolean;
    receivedPackets: number;
    receivedBytes: number;
    droppedPackets: number;
    malformedPackets: number;
    sentPackets: number;
    sentBytes: number;
    sendSuccesses: number;
    sendFailures: number;
    sendTimeouts: number;
    powerSave: {
      enabled: boolean;
      wakeWindowMs: number;
      wakeIntervalMs: number;
    };
  }

  interface EspNowPeerOptions {
    address: EspNowAddress;
    channel?: EspNowChannel;
    encrypted?: boolean;
    lmk?: ByteSource;
  }

  interface EspNowPeerStatus {
    open: boolean;
    address: EspNowAddress;
    channel: number | "current";
    encrypted: boolean;
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
    macDelivered: boolean;
    completedAtUs: number;
  }

  type EspNowErrorCode =
    | "ESPNOW_NOT_SUPPORTED"
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
    | "ESPNOW_CLOSING"
    | "ESPNOW_CLEANUP_PENDING";

  interface EspNowError extends NativeError {
    code: EspNowErrorCode;
    operation: "espnow";
    details: {
      espCode: number;
      address: string | null;
      channel: number | null;
    };
  }

  class EspNowPeer {
    private constructor();
    status(): EspNowPeerStatus;
    send(data: ByteSource, options?: EspNowSendOptions): EspNowSendResult;
    update(options: Omit<EspNowPeerOptions, "address">): EspNowPeerStatus;
    close(): boolean;
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
    setPowerSave(options: EspNowPowerSaveOptions): boolean;
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
    autoPair?: boolean;
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

  class BLEConnection implements EventQueue<BLEConnectionEvent> {
    private constructor();
    receive(timeoutMs?: number): BLEConnectionEvent | null;
    stats(): EventQueueStats;
    status(): BLEConnectionStatus;
    pair(options?: { timeoutMs?: number }): BLEConnectionSecurityStatus;
    respondPairing(requestId: number, response: boolean | number): boolean;
    exchangeMtu(mtu?: number, timeoutMs?: number): number;
    readRssi(timeoutMs?: number): number;
    discover(options?: BLEDiscoverOptions): BLEService[];
    close(): boolean;
  }

  class BLEService {
    private constructor();
    readonly uuid: string;
    readonly startHandle: number;
    readonly endHandle: number;
    characteristics(): BLECharacteristic[];
  }

  class BLECharacteristic {
    private constructor();
    readonly uuid: string;
    readonly declarationHandle: number;
    readonly valueHandle: number;
    readonly properties: BLEGattProperty[];
    descriptors(): BLEDescriptor[];
    read(options?: BLEGattReadOptions): ByteView;
    write(data: ByteSource, options?: BLEGattWriteOptions): number;
    subscribe(options?: BLESubscribeOptions): BLENotificationStream;
  }

  class BLEDescriptor {
    private constructor();
    readonly uuid: string;
    readonly handle: number;
    read(options?: BLEGattReadOptions): ByteView;
    write(data: ByteSource, options?: BLEGattWriteOptions): number;
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
    | "BLE_PAYLOAD_TOO_LARGE" | "BLE_SERVER_LIMIT" | "BLE_CLOSING";

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
   * Fetch options accepted by `fetch(...)` and `http.fetch(...)`.
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
   * var response = fetch("https://example.com");
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
    fileSource(path: string): ByteSpanSource;
    sourceInfo(source: ByteSpanSource): RPCSourceInfo;
    adoptFile(source: ByteSpanSource, path: string): boolean;
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
  const I2CBus: { readonly prototype: ESP32QJS.I2CBus };
  const I2CDevice: { readonly prototype: ESP32QJS.I2CDevice };
  const SPIBus: { readonly prototype: ESP32QJS.SPIBus };
  const SPIDevice: { readonly prototype: ESP32QJS.SPIDevice };
  const EspNowSession: { readonly prototype: ESP32QJS.EspNowSession };
  const EspNowPeer: { readonly prototype: ESP32QJS.EspNowPeer };
  const BLEAdapter: { readonly prototype: ESP32QJS.BLEAdapter };
  const BLEScanner: { readonly prototype: ESP32QJS.BLEScanner };
  const BLEAdvertiser: { readonly prototype: ESP32QJS.BLEAdvertiser };
  const BLEConnection: { readonly prototype: ESP32QJS.BLEConnection };
  const BLEService: { readonly prototype: ESP32QJS.BLEService };
  const BLECharacteristic: { readonly prototype: ESP32QJS.BLECharacteristic };
  const BLEDescriptor: { readonly prototype: ESP32QJS.BLEDescriptor };
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

  /** Alias of `sleep(...)`. */
  function delay(ms: number): number;

  /**
   * Global HTTP fetch helper.
   *
   * @example
   * ```js
   * var response = fetch("https://example.com");
   * print(response.status, response.text().length);
   * ```
   */
  function fetch(
    input: ESP32QJS.FetchInput,
    options?: ESP32QJS.FetchOptions,
  ): ESP32QJS.Response;

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
