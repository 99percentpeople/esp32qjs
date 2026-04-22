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
  setDriveStrength(pin: number, strength: GpioDriveStrength): GpioDriveStrength;
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
  timerConfig(timer: number, options: LedcTimerConfigOptions): LedcTimerStatus;
  channelConfig(channel: number, options: LedcChannelConfigOptions): LedcChannelStatus;
  setDuty(channel: number, duty: number): LedcChannelStatus;
  setDutyWithHpoint(channel: number, duty: number, hpoint: number): LedcChannelStatus;
  setDutyAndUpdate(channel: number, duty: number, hpoint?: number): LedcChannelStatus;
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
  configure(unit: AdcUnit, channel: number, options: AdcConfigureOptions): AdcStatus;
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
 * Runtime information returned by `esp32.info()`.
 */
interface Esp32Features {
  fs: boolean;
  gpio: boolean;
  ledc: boolean;
  adc: boolean;
  dac: boolean;
  i2c: boolean;
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
 * Shared I2C bus state.
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

/**
 * I2C bus helpers.
 *
 * @example
 * ```js
 * i2c.open({ sda: 5, scl: 6, freqHz: 400000 });
 * print(JSON.stringify(i2c.scan()));
 * ```
 */
interface I2CModule {
  readonly DEFAULT_SDA: number;
  readonly DEFAULT_SCL: number;
  readonly DEFAULT_FREQ_HZ: number;
  readonly DEFAULT_TIMEOUT_MS: number;
  open(options?: I2COpenOptions): I2CStatus;
  close(): boolean;
  status(): I2CStatus;
  scan(): number[];
  write(addr: number, data: ArrayLike<number>): number;
  read(addr: number, length: number): number[];
  writeRead(
    addr: number,
    writeData: ArrayLike<number>,
    readLength: number,
  ): number[];
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
  lastDisconnectReasonName: string;
}

/**
 * One access-point result from `wifi.scan()`.
 */
interface WiFiScanResult {
  ssid: string;
  bssid: string;
  rssi: number;
  channel: number;
  authMode: string;
  hidden: boolean;
}

type WiFiConnectCallback = (
  status?: WiFiStatus,
  error?: unknown,
) => void;
type WiFiScanCallback = (
  results?: WiFiScanResult[],
  error?: unknown,
) => void;

/**
 * Wi-Fi station helpers.
 *
 * @example
 * ```js
 * print(JSON.stringify(wifi.status()));
 * wifi.scan(function (results, error) { print(error === undefined, results.length); });
 * ```
 */
interface WiFiModule {
  readonly DEFAULT_TIMEOUT_MS: number;
  status(): WiFiStatus;
  connect(ssid: string, password: string, timeoutMs?: number): WiFiStatus;
  connect(ssid: string, password: string, callback: WiFiConnectCallback): void;
  connect(
    ssid: string,
    password: string,
    timeoutMs: number,
    callback: WiFiConnectCallback,
  ): void;
  disconnect(): WiFiStatus;
  scan(): WiFiScanResult[];
  scan(callback: WiFiScanCallback): void;
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
type FetchCallback = (
  response?: Response,
  error?: unknown,
) => void;

interface HttpFetchFunction {
  (input: FetchInput, options?: FetchOptions): Response;
  (input: FetchInput, callback: FetchCallback): void;
  (
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
  server?(options?: HttpServerOptions): HttpServer;
  staticFileHandler?(root: string): StaticFileHandler;
}

declare global {
  const Headers: typeof ESP32QJS.Headers;
  const Request: typeof ESP32QJS.Request;
  const Response: typeof ESP32QJS.Response;
  const Stream: typeof ESP32QJS.Stream;
  const HttpServer: typeof ESP32QJS.HttpServer;
  const StaticFileHandler: typeof ESP32QJS.StaticFileHandler;

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
  function fetch(
    input: ESP32QJS.FetchInput,
    callback: ESP32QJS.FetchCallback,
  ): void;
  function fetch(
    input: ESP32QJS.FetchInput,
    options: ESP32QJS.FetchOptions,
    callback: ESP32QJS.FetchCallback,
  ): void;

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
