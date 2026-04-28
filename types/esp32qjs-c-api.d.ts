declare namespace ESP32QJS {
  /**
   * JSON-like value supported by `Response.json(...)` and the HTTP helpers.
   */
  type JsonPrimitive = string | number | boolean | null;
  type JsonValue = JsonPrimitive | JsonValue[] | { [key: string]: JsonValue };

  type HeaderRecord = Record<string, string>;
  type HeadersInit = Headers | HeaderRecord;
  type RequestBody = string | Stream | null | undefined;
  type RoutePattern = string | RegExp;

  /**
   * Byte payload accepted by low-level transports.
   *
   * Plain array-like values are copied by the transport. Native `ByteView`
   * values returned by modules such as `displayBuffer.readRect(...)` can be
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
   * stable JavaScript copy is required.
   */
  interface ByteView {
    readonly length: number;
    readonly byteLength: number;
    toArray(): number[];
  }

  /**
   * Retained native byte span source.
   *
   * Transports open byte spans on demand from this opaque capability.
   * Producer-specific controls live on subtypes such as
   * `DisplayBufferSpanSource`.
   */
  interface ByteSpanSource {
    readonly __byteSpanSourceBrand: never;
  }

  /**
   * Retained display-buffer byte span source.
   */
  interface DisplayBufferSpanSource extends ByteSpanSource {
    setRect(x: number, y: number, width: number, height: number): this;
  }

  type DisplayBufferFormat = "mono1" | "rgb565";
  type DisplayBufferLayout = "linear" | "page-y8";
  type DisplayBufferStorage = "auto" | "internal" | "psram" | "dma";
  type DisplayByteOrder = "be" | "le" | "rgb565be" | "rgb565le";
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

  interface DisplayBufferCreateOptions {
    width: number;
    height: number;
    format: DisplayBufferFormat;
    layout?: DisplayBufferLayout;
    storage?: DisplayBufferStorage;
    stride?: number;
    pageHeight?: number;
    chunkBytes?: number;
    foreground?: DisplayColor;
    background?: DisplayColor;
  }

  interface DisplayBufferReadRectOptions {
    byteOrder?: DisplayByteOrder;
  }

  interface DisplayBufferReadRectChunksOptions
    extends DisplayBufferReadRectOptions {
    chunkBytes?: number;
    /**
     * Let direct full-row exports reuse the internal chunk array and ByteView
     * wrappers. Use only for immediate synchronous writes; reused chunks are not
     * snapshots.
     */
    reuse?: boolean;
  }

  interface DisplaySpanSourceOptions extends DisplayBufferReadRectOptions {
    chunkBytes?: number;
  }

  interface DisplayBezierOptions {
    /** Segment count is clamped by the runtime to the supported range. */
    segments?: number;
  }

  interface DisplayBitmap {
    width: number;
    height: number;
    pixels: ArrayLike<number>;
  }

  interface DisplayBitmapOptions {
    color?: DisplayColor;
    /** Omit or pass `null` to keep off pixels transparent. */
    background?: DisplayColor | null;
  }

  interface DisplayTextOptions {
    /** Native font returned by `displayBuffer.loadFont(path)`. */
    font: DisplayFont;
    color?: DisplayColor;
    /** Omit or pass `null` to keep glyph backgrounds transparent. */
    background?: DisplayColor | null;
    spacing?: number;
  }

  /**
   * Deferred helper created by the global `defer()` helper.
   *
   * @example
   * ```js
   * var d = defer();
   * setTimeout(function () { d.resolve("ok"); }, 50);
   * print(d.wait(1000));
   * ```
   */
  interface Deferred<T = unknown> {
    readonly settled: boolean;
    readonly done: boolean;
    readonly ok: boolean;
    readonly value: T | undefined;
    readonly error: unknown;
    resolve(value?: T): T | undefined;
    reject(error?: unknown): unknown;
    callback(value?: T, error?: unknown): void;
    wait(timeoutMs?: number): T;
  }

  /**
   * Start function accepted by the global `waitFor(...)` helper.
   *
   * @example
   * ```js
   * var result = waitFor(function (resolve) {
   *   setTimeout(function () { resolve(123); }, 50);
   * }, 1000);
   * print(result);
   * ```
   */
  type WaitForStart<T> = (
    resolve: (value: T) => void,
    reject: (error?: unknown) => void,
    deferred: Deferred<T>,
  ) => void | (() => void);

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
    json<T = unknown>(): T;
    static text(text: string, init?: ResponseInit): Response;
    static json(value: JsonValue, init?: ResponseInit): Response;
    static stream(stream: Stream, init?: ResponseInit): Response;
  }

  /**
   * Unified stream interface used by files, requests, and responses.
   *
   * @example
   * ```js
   * var stream = fs.open("_sys/ui/core.js", "r");
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

    read(size?: number): string | null;
    write(text: string): number;
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

  /**
   * LittleFS helpers restricted to `/littlefs`.
   *
   * @example
   * ```js
   * fs.writeText("notes.txt", "hello\\n");
   * print(fs.readText("notes.txt"));
   * print(JSON.stringify(fs.list(".")));
   * fs.remove("notes.txt");
   * ```
   */
  interface FsModule {
    readonly ROOT: string;
    open(path: string, mode?: FsOpenMode): Stream;
    list(path?: string): FsEntry[];
    stat(path: string): FsEntry;
    exists(path: string): boolean;
    readText(path: string): string;
    writeText(path: string, text: string): number;
    appendText(path: string, text: string): number;
    mkdir(path: string): boolean;
    rename(fromPath: string, toPath: string): boolean;
    remove(path: string): boolean;
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
   * GPIO helpers bound to the active board profile.
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
   * // Arduino-style interrupt registration. The callback runs later on the JS thread.
   * gpio.attachInterrupt(pin, function (event) {
   *   print(event.pin, event.mode, event.level);
   * }, gpio.CHANGE);
   * gpio.detachInterrupt(pin);
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
    /**
     * Register one interrupt callback for a GPIO.
     *
     * The callback is scheduled onto the JavaScript thread after the ISR queues
     * an event, so higher-level behaviors such as debounce should be implemented
     * in JavaScript rather than inside the native binding.
     */
    attachInterrupt(
      pin: number,
      callback: (event: GpioInterruptEvent) => void,
      mode?: GpioInterruptMode | 0 | 1,
    ): GpioStatus;
    /** Remove the interrupt callback for a GPIO. */
    detachInterrupt(pin: number): GpioStatus;
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

  interface LedcTimerConfigOptions {
    freqHz: number;
    dutyResolution: number;
    clock?: LedcClock;
    deconfigure?: boolean;
  }

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
   * Native EQF1 fixed bitmap font loaded by `displayBuffer.loadFont(...)`.
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
   * `displayBuffer.loadFont(path)`.
   */
  interface DisplayFontConstructor {
    readonly prototype: DisplayFont;
  }

  /**
   * Native pixel buffer for low-level display drivers.
   *
   * Pixel colors are packed numeric values: `mono1` uses `0` or `1`, while
   * `rgb565` uses 16-bit RGB565 values. Drawing methods mutate the buffer, mark
   * the affected dirty rectangle, and return the same buffer for chaining.
   */
  class DisplayBuffer {
    private constructor();
    readonly width: number;
    readonly height: number;
    readonly format: DisplayBufferFormat;
    readonly layout: DisplayBufferLayout;
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
    drawBitmap(
      x: number,
      y: number,
      bitmap: DisplayBitmap,
      options?: DisplayBitmapOptions,
    ): this;
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
      options?: DisplayBufferReadRectOptions,
    ): ByteView;
    readRectChunks(
      x: number,
      y: number,
      width: number,
      height: number,
      options?: DisplayBufferReadRectChunksOptions,
    ): ByteView[];
    createSpanSource(options?: DisplaySpanSourceOptions): DisplayBufferSpanSource;
  }

  /**
   * Native display-buffer module. Exposed only when
   * `esp32.info().features.displayBuffer` is enabled.
   */
  interface DisplayBufferModule {
    readonly MONO1: "mono1";
    readonly RGB565: "rgb565";
    create(options: DisplayBufferCreateOptions): DisplayBuffer;
    loadFont(path: string): DisplayFont;
  }

  /**
   * Runtime information returned by `esp32.info()`.
   */
  interface Esp32Features {
    fs: boolean;
    gpio: boolean;
    ledc: boolean;
    adc: boolean;
    dac: boolean;
    i2c: boolean;
    spi: boolean;
    uart: boolean;
    displayBuffer: boolean;
    wifi: boolean;
    http: boolean;
    httpServer: boolean;
    staticFileHandler: boolean;
  }

  /**
   * Runtime information returned by `esp32.info()`.
   */
  interface Esp32Info {
    board: string;
    chip: string;
    features: Esp32Features;
    userLedPin: number;
    userLedActiveLow: boolean;
    scriptsDir: string;
    flashSize: number;
    psramEnabled: boolean;
    psramSize: number;
    freePsram: number;
    totalInternalHeap: number;
    freeInternalHeap: number;
    jsHeapSize: number;
    jsHeapRegion: string;
    littlefsMounted: boolean;
    replEnabled: boolean;
    autoRunIndexJs: boolean;
    formatLittlefsOnMountFail: boolean;
    freeHeap: number;
    jsTimeMs: number;
  }

  /**
   * ESP32 runtime helpers.
   *
   * @example
   * ```js
   * print(JSON.stringify(esp32.info()));
   * print(esp32.millis());
   * ```
   */
  interface Esp32Module {
    info(): Esp32Info;
    millis(): number;
    micros(): number;
    freeHeap(): number;
  }

  /**
   * I2C bus state.
   */
  interface I2CStatus {
    opened: boolean;
    sda: number;
    scl: number;
    freqHz: number;
    timeoutMs: number;
    internalPullup: boolean;
  }

  /**
   * I2C open options.
   */
  interface I2COpenOptions {
    sda?: number;
    scl?: number;
    freqHz?: number;
    timeoutMs?: number;
    internalPullup?: boolean;
  }

  interface I2CWriteChunksStats {
    chunks: number;
    bytes: number;
    totalUs: number;
  }

  /**
   * Open I2C bus handle.
   *
   * @example
   * ```js
   * var bus = i2c.open({ sda: 5, scl: 6, freqHz: 400000 });
   * print(JSON.stringify(bus.scan()));
   * ```
   */
  interface I2CBus {
    close(): boolean;
    status(): I2CStatus;
    scan(): number[];
    /** Write one byte source to a 7-bit device address. */
    write(addr: number, data: ByteSource): number;
    /**
     * Write byte-source chunks with one temporary device handle.
     *
     * This is useful for chunks returned by `DisplayBuffer.readRectChunks(...)`
     * and for other producers that already split payloads.
     */
    writeChunks(
      addr: number,
      chunks: ArrayLike<ByteSource>,
    ): I2CWriteChunksStats;
    read(addr: number, length: number): number[];
    writeRead(
      addr: number,
      writeData: ByteSource,
      readLength: number,
    ): number[];
  }

  /**
   * I2C factory and constants.
   */
  interface I2CModule {
    readonly DEFAULT_SDA: number;
    readonly DEFAULT_SCL: number;
    readonly DEFAULT_FREQ_HZ: number;
    readonly DEFAULT_TIMEOUT_MS: number;
    open(options?: I2COpenOptions): I2CBus;
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
    freqHz: number;
    queueSize: number;
    csHigh: boolean;
    lsbFirst: boolean;
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
  }

  interface SPIWriteOptions {
    /** Number of queued transactions to keep in flight. */
    queueDepth?: number;
  }

  /**
   * Timing and transfer counters returned by SPI bulk-write helpers.
   */
  interface SPIWriteStats {
    chunks: number;
    bytes: number;
    prepUs: number;
    queueUs: number;
    waitUs: number;
    transferUs: number;
    totalUs: number;
    queueDepth: number;
    direct: boolean;
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
    transfer(data: ByteSource): number[];
    write(data: ByteSource): number;
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
    read(length: number, fillByte?: number): number[];
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

  /**
   * Open synchronous UART port handle.
   */
  interface UARTPort {
    close(): boolean;
    status(): UARTStatus;
    write(data: ByteSource): number;
    writeChunks(chunks: ArrayLike<ByteSource>): UARTWriteStats;
    writeSource(source: ByteSpanSource): UARTWriteStats;
    read(length: number, timeoutMs?: number): number[];
    available(): number;
    flush(timeoutMs?: number): boolean;
    clearRx(): boolean;
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

  /**
   * Current Wi-Fi station status.
   */
  interface WiFiStatus {
    initialized: boolean;
    started: boolean;
    connected: boolean;
    scanning: boolean;
    ssid: string;
    hostname: string;
    ip: string;
    netmask: string;
    gateway: string;
    lastDisconnectReason: number;
    lastDisconnectReasonName: WiFiDisconnectReasonName;
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

  type WiFiConnectCallback = (status?: WiFiStatus, error?: unknown) => void;
  type WiFiScanCallback = (results?: WiFiScanResult[], error?: unknown) => void;

  /**
   * Wi-Fi station helpers.
   *
   * @example
   * ```js
   * print(JSON.stringify(wifi.status()));
   * wifi.async.scan(function (results, error) { print(error === undefined, results.length); });
   * ```
   */
  interface WiFiAsyncModule {
    connect(
      ssid: string,
      password: string,
      callback: WiFiConnectCallback,
    ): void;
    connect(
      ssid: string,
      password: string,
      timeoutMs: number,
      callback: WiFiConnectCallback,
    ): void;
    scan(callback: WiFiScanCallback): void;
  }

  interface WiFiModule {
    readonly DEFAULT_TIMEOUT_MS: number;
    status(): WiFiStatus;
    connect(ssid: string, password: string, timeoutMs?: number): WiFiStatus;
    disconnect(): WiFiStatus;
    scan(): WiFiScanResult[];
    async: WiFiAsyncModule;
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
  }

  type FetchInput = string | Request;
  type FetchCallback = (response?: Response, error?: unknown) => void;

  interface HttpFetchFunction {
    (input: FetchInput, options?: FetchOptions): Response;
  }

  interface HttpAsyncModule {
    fetch(input: FetchInput, callback: FetchCallback): void;
    fetch(
      input: FetchInput,
      options: FetchOptions,
      callback: FetchCallback,
    ): void;
  }

  /**
   * HTTP route handler object accepted by `server.get(...)` and friends.
   *
   * @example
   * ```js
   * server.get("/assets/*", staticFileHandler("./_sys"));
   * ```
   */
  interface HttpRouteHandlerObject {
    handle(request: Request): Response;
  }

  type HttpRouteHandler =
    | ((request: Request) => Response)
    | HttpRouteHandlerObject;

  /**
   * HTTP server instance created by `http.server(...)`.
   *
   * @example
   * ```js
   * var server = http.server({ port: 8080, host: "0.0.0.0" });
   * server.get("/ping", function () {
   *   return Response.text("pong");
   * });
   * server.start();
   * ```
   */
  class HttpServer {
    private constructor();
    readonly serverId: number;
    readonly port: number;
    readonly ctrlPort: number;
    readonly host: string;
    started: boolean;
    start(): void;
    stop(): void;
    get(pathOrPattern: RoutePattern, handler: HttpRouteHandler): void;
    post(pathOrPattern: RoutePattern, handler: HttpRouteHandler): void;
    put(pathOrPattern: RoutePattern, handler: HttpRouteHandler): void;
    patch(pathOrPattern: RoutePattern, handler: HttpRouteHandler): void;
    delete(pathOrPattern: RoutePattern, handler: HttpRouteHandler): void;
    head(pathOrPattern: RoutePattern, handler: HttpRouteHandler): void;
    options(pathOrPattern: RoutePattern, handler: HttpRouteHandler): void;
    all(pathOrPattern: RoutePattern, handler: HttpRouteHandler): void;
  }

  /**
   * Static-file handler returned by `staticFileHandler(root)`.
   */
  class StaticFileHandler implements HttpRouteHandlerObject {
    private constructor();
    readonly root: string;
    handle(request: Request): Response;
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
   * server.get("/core", function () {
   *   return Response.stream(fs.open("_sys/display/core.js", "rb"), {
   *     headers: { "content-type": "application/javascript; charset=utf-8" }
   *   });
   * });
   * server.start();
   * ```
   */
  interface HttpModule {
    readonly DEFAULT_TIMEOUT_MS?: number;
    fetch?: HttpFetchFunction;
    async?: HttpAsyncModule;
    server?(options?: HttpServerOptions): HttpServer;
    staticFileHandler?(root: string): StaticFileHandler;
  }
}

declare global {
  const Headers: typeof ESP32QJS.Headers;
  const Request: typeof ESP32QJS.Request;
  const Response: typeof ESP32QJS.Response;
  const Stream: typeof ESP32QJS.Stream;
  const HttpServer: typeof ESP32QJS.HttpServer;
  const StaticFileHandler: typeof ESP32QJS.StaticFileHandler;
  const DisplayFont: ESP32QJS.DisplayFontConstructor;
  const DisplayBuffer: typeof ESP32QJS.DisplayBuffer;

  /** LittleFS script root exposed to JavaScript when `esp32.info().features.fs` is enabled. */
  const SCRIPTS_DIR: string;

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

  /** Create a deferred helper for callback-style async work. */
  function defer<T = unknown>(): ESP32QJS.Deferred<T>;

  /**
   * Run an async starter function and block while host events continue to pump.
   * The callback may optionally return a cancel function.
   */
  function waitFor<T>(start: ESP32QJS.WaitForStart<T>, timeoutMs?: number): T;

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

  function setTimeout(fn: () => void, ms: number): number;
  function clearTimeout(id: number): void;
  function setInterval(fn: () => void, ms: number): number;
  function clearInterval(id: number): void;

  /** File-system helpers bound to `/littlefs`. */
  const fs: ESP32QJS.FsModule;
  /** GPIO helpers for the active board profile. */
  const gpio: ESP32QJS.GpioModule;
  /** LEDC PWM timer/channel helpers. */
  const ledc: ESP32QJS.LedcModule;
  /** ADC oneshot helpers. */
  const adc: ESP32QJS.AdcModule;
  /** DAC oneshot helpers. Exposed only when `esp32.info().features.dac` is enabled. */
  const dac: ESP32QJS.DacModule;
  /** ESP32 runtime information helpers. */
  const esp32: ESP32QJS.Esp32Module;
  /** Shared I2C bus helpers. */
  const i2c: ESP32QJS.I2CModule;
  /** SPI master bus/device helpers. */
  const spi: ESP32QJS.SPIModule;
  /** Synchronous UART port helpers. */
  const uart: ESP32QJS.UARTModule;
  /** Native display-buffer helpers. Exposed only when `esp32.info().features.displayBuffer` is enabled. */
  const displayBuffer: ESP32QJS.DisplayBufferModule;
  /** Wi-Fi station helpers. */
  const wifi: ESP32QJS.WiFiModule;
  /** HTTP client/server namespace. Exposed when either `esp32.info().features.http` or `.httpServer` is enabled. */
  const http: ESP32QJS.HttpModule;

  /**
   * Create a static-file route handler rooted under LittleFS.
   *
   * @example
   * ```js
   * var server = http.server({ port: 8080 });
   * server.get("/assets/*", staticFileHandler("./_sys"));
   * server.start();
   * ```
   */
  const staticFileHandler:
    | ((root: string) => ESP32QJS.StaticFileHandler)
    | undefined;
}

export {};
