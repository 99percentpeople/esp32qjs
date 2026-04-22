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
 * gpio.pinMode(gpio.LED_BUILTIN, gpio.OUTPUT);
 * gpio.led(true);
 * gpio.led(false);
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
  reset(pin: number): number;
  led(value: boolean): boolean;
}

/**
 * Runtime information returned by `esp32.info()`.
 */
interface Esp32Info {
  board: string;
  chip: string;
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
 * HTTP client and server helpers.
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
  readonly DEFAULT_TIMEOUT_MS: number;
  fetch(input: FetchInput, options?: FetchOptions): Response;
  fetch(input: FetchInput, callback: FetchCallback): void;
  fetch(
    input: FetchInput,
    options: FetchOptions,
    callback: FetchCallback,
  ): void;
  server(options?: HttpServerOptions): HttpServer;
  staticFileHandler(root: string): StaticFileHandler;
}

declare global {
  const Headers: typeof ESP32QJS.Headers;
  const Request: typeof ESP32QJS.Request;
  const Response: typeof ESP32QJS.Response;
  const Stream: typeof ESP32QJS.Stream;
  const HttpServer: typeof ESP32QJS.HttpServer;
  const StaticFileHandler: typeof ESP32QJS.StaticFileHandler;

  /** LittleFS script root exposed to JavaScript. */
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
  /** ESP32 runtime information helpers. */
  const esp32: ESP32QJS.Esp32Module;
  /** Shared I2C bus helpers. */
  const i2c: ESP32QJS.I2CModule;
  /** Wi-Fi station helpers. */
  const wifi: ESP32QJS.WiFiModule;
  /** HTTP client and server helpers. */
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
  function staticFileHandler(root: string): ESP32QJS.StaticFileHandler;
}
