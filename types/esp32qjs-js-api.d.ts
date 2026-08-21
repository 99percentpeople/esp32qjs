declare global {
namespace ESP32QJS {
  interface RGBColor {
    r?: number;
    g?: number;
    b?: number;
  }

  type ColorValue = number | RGBColor | null | undefined;

  interface Point {
    x: number;
    y: number;
  }

  interface Rect {
    x: number;
    y: number;
    width: number;
    height: number;
  }

  interface Size {
    width: number;
    height: number;
  }

  interface TextMetrics extends Size {
    lines: number;
  }

  interface DisplayFont {
    name: string;
    map?: Record<string, number>;
    fallbackCode?: number;
    width: number;
    height: number;
    advance: number;
    lineHeight: number;
    native?: unknown;
  }

  interface DisplayFontSet {
    load(size: string | number, name?: string): DisplayFont;
  }

  interface SurfaceMetadata {
    width: number;
    height: number;
    pixelFormat: BitmapFormat;
    layout?: BitmapLayout;
  }

  interface SurfaceCommandBufferOptions {
    commandCapacity?: number;
    textBytes?: number;
  }

  /** Hardware-independent framebuffer options. */
  interface SurfaceOptions {
    storage?: BitmapStorage;
    fallbackStorage?: BitmapStorage;
    chunkBytes?: number;
    foreground?: ColorValue;
    background?: ColorValue;
    spacing?: number;
    commandBuffer?: boolean | SurfaceCommandBufferOptions;
  }

  /**
   * Text measurement and drawing style.
   */
  interface TextStyle {
    color?: ColorValue;
    background?: ColorValue;
    font?: DisplayFont;
    spacing?: number;
  }

  interface MaskStyle {
    color?: ColorValue;
    background?: ColorValue;
  }

  /**
   * Monochrome bitmap payload accepted by `drawMask(...)`.
   */
  interface MonoMask {
    width: number;
    height: number;
    pixels: ArrayLike<number | boolean>;
  }

  type PointInput = Point | [number, number];
  type PointList = ArrayLike<number> | ArrayLike<PointInput>;

  interface CurveOptions {
    segments?: number;
  }

  interface DisplayStats {
    enabled: boolean;
    presents: number;
    regions: number;
    pixels: number;
    bytes: number;
    chunks: number;
    directTransfers: number;
    totalUs: number;
    prepareUs: number;
    panelUs: number;
    transferUs: number;
    driver: Record<string, unknown>;
    transport: Record<string, unknown>;
  }

  /**
   * Reduced drawing target returned by command-buffer-backed surfaces.
   */
  interface SurfaceBatch {
    clear(color?: ColorValue): this;
    fill(color?: ColorValue): this;
    fillRect(x: number, y: number, width: number, height: number, color?: ColorValue): this;
    drawLine(x0: number, y0: number, x1: number, y1: number, color?: ColorValue): this;
    drawRect(x: number, y: number, width: number, height: number, color?: ColorValue): this;
    drawRoundRect(
      x: number,
      y: number,
      width: number,
      height: number,
      radius: number,
      color?: ColorValue,
    ): this;
    fillRoundRect(
      x: number,
      y: number,
      width: number,
      height: number,
      radius: number,
      color?: ColorValue,
    ): this;
    drawChar(x: number, y: number, ch: string, options?: TextStyle): this;
    drawText(x: number, y: number, text: string, options?: TextStyle): this;
    measureText(text: string, style?: TextStyle): TextMetrics;
    flush(): this;
  }

  type DisplayObjectState = "created" | "opening" | "open" | "closing" | "closed";

  interface DisplayTransportCapabilities {
    chunks: boolean;
    source: boolean;
    reset: boolean;
    backlight: boolean;
  }

  interface DisplayTransport {
    readonly kind: string;
    state: DisplayObjectState;
    readonly capabilities: DisplayTransportCapabilities;
    open(): this;
    command(command: number | ArrayLike<number>, data?: ByteSource): unknown;
    write(data: ByteSource): unknown;
    writeChunks?(chunks: ArrayLike<ByteSource>, options?: SPIWriteOptions): SPIWriteStats | I2CWriteChunksStats;
    writeSource?(source: ByteSpanSource, options?: SPIWriteOptions): SPIWriteStats;
    reset?(): this;
    setBacklight?(enabled: boolean): this;
    stats(): Record<string, unknown>;
    resetStats(): this;
    close(): boolean;
  }

  interface I2CDisplayTransportOptions {
    bus?: I2CBus;
    busOptions?: I2COpenOptions;
    address?: number;
    commandPrefix?: number;
    dataPrefix?: number;
  }

  interface SPI4WirePins {
    dc: number;
    reset?: number;
    backlight?: number;
  }

  interface SPI4WireDisplayTransportOptions {
    bus?: SPIBus;
    device?: SPIDevice;
    busOptions?: SPIOpenBusOptions;
    deviceOptions?: SPIOpenDeviceOptions;
    pins: SPI4WirePins;
    backlightActive?: boolean;
  }

  type DisplayTransportFactory = (options?: Record<string, unknown>) => DisplayTransport;

  interface DisplayTransportRegistry {
    register(name: string, factory: DisplayTransportFactory): this;
    has(name: string): boolean;
    list(): string[];
    create(name: "i2c", options: I2CDisplayTransportOptions): DisplayTransport;
    create(name: "spi4wire", options: SPI4WireDisplayTransportOptions): DisplayTransport;
    create(name: string, options?: Record<string, unknown>): DisplayTransport;
  }

  interface PanelDriverCapabilities {
    partialPresent: boolean;
    multiRegion: boolean;
    power: boolean;
    inversion: boolean;
    contrast: boolean;
    backlight: boolean;
  }

  interface DisplayFrameSource {
    readonly width: number;
    readonly height: number;
    readonly pixelFormat: string;
    readonly layout: string;
    readonly chunkBytes: number;
    readRect(x: number, y: number, width: number, height: number, options?: BitmapReadRectOptions): ByteView;
    readRectChunks(
      x: number,
      y: number,
      width: number,
      height: number,
      options?: BitmapReadRectChunksOptions,
    ): ByteView[];
    getSpanSource(options?: DisplaySpanSourceOptions): BitmapSpanSource | null;
  }

  interface DisplayPresentResult {
    regions?: number;
    pixels?: number;
    bytes?: number;
    chunks?: number;
    directTransfers?: number;
    direct?: boolean;
    totalUs?: number;
    prepareUs?: number;
    panelUs?: number;
    transferUs?: number;
  }

  interface DisplayPresentOptions {
    merge?: boolean;
    mergeCoverage?: number;
    mergeAreaRatio?: number;
    mergePixelBudget?: number;
    queueDepth?: number;
    metrics?: boolean;
  }

  interface PanelDriver {
    readonly name: string;
    readonly transport: DisplayTransport;
    readonly width: number;
    readonly height: number;
    readonly pixelFormat: BitmapFormat;
    readonly layout: BitmapLayout;
    readonly byteOrder: string;
    readonly capabilities: PanelDriverCapabilities;
    state: DisplayObjectState;
    open(): this;
    present(
      frame: DisplayFrameSource,
      regions: ArrayLike<Rect>,
      options?: DisplayPresentOptions,
    ): DisplayPresentResult;
    setPower?(enabled: boolean): this;
    setInverted?(enabled: boolean): this;
    setContrast?(value: number): this;
    setBacklight?(enabled: boolean): this;
    stats?(): Record<string, unknown>;
    resetStats?(): this;
    close(): boolean;
  }

  interface SSD1306DriverOptions {
    transport: DisplayTransport;
    width?: number;
    height?: number;
  }

  interface ST7789DriverOptions {
    transport: DisplayTransport;
    width?: number;
    height?: number;
    columnOffset?: number;
    rowOffset?: number;
    rotation?: number;
    bgr?: boolean;
    inverted?: boolean;
  }

  type PanelDriverFactory = (options?: Record<string, unknown>) => PanelDriver;

  interface PanelDriverRegistry {
    register(name: string, factory: PanelDriverFactory): this;
    has(name: string): boolean;
    list(): string[];
    create(name: "ssd1306", options: SSD1306DriverOptions): PanelDriver;
    create(name: "st7789", options: ST7789DriverOptions): PanelDriver;
    create(name: string, options?: Record<string, unknown>): PanelDriver;
  }

  interface SPI4WireProfileTransportOptions {
    bus?: SPIBus;
    device?: SPIDevice;
    busOptions?: SPIOpenBusOptions;
    deviceOptions?: SPIOpenDeviceOptions;
    pins?: Partial<SPI4WirePins>;
    backlightActive?: boolean;
  }

  interface WLK1501SPI8PProfileOptions {
    transport?: SPI4WireProfileTransportOptions;
    driver?: Omit<ST7789DriverOptions, "transport">;
    surface?: SurfaceOptions;
    display?: Omit<DisplayOptions, "surface" | "profileName">;
  }

  type DisplayProfileFactory = (
    options?: Record<string, unknown>,
  ) => { driver: PanelDriver; options?: DisplayOptions } | Display;

  interface DisplayProfileRegistry {
    register(name: string, factory: DisplayProfileFactory): this;
    has(name: string): boolean;
    list(): string[];
    create(name: "wlk1501spi8p", options?: WLK1501SPI8PProfileOptions): Display;
    create(name: string, options?: Record<string, unknown>): Display;
    open(name: "wlk1501spi8p", options?: WLK1501SPI8PProfileOptions): Display;
    open(name: string, options?: Record<string, unknown>): Display;
  }

  /** Hardware-independent renderer around one native Bitmap. */
  class Surface {
    constructor(metadata: SurfaceMetadata, options?: SurfaceOptions);
    readonly width: number;
    readonly height: number;
    readonly pixelFormat: string;
    readonly layout: string;
    readonly foreground: number;
    readonly background: number;
    readonly spacing: number;
    readonly chunkBytes: number;
    readonly frame: DisplayFrameSource;
    readonly bitmap: Bitmap;
    readonly commandBuffer: DisplayCommandBuffer | null;
    readonly commandBufferEnabled: boolean;
    ready: boolean;
    closed: boolean;
    clear(color?: ColorValue): this;
    fill(color?: ColorValue): this;
    setPixel(x: number, y: number, color?: ColorValue): this;
    getPixel(x: number, y: number): number;
    fillRect(
      x: number,
      y: number,
      width: number,
      height: number,
      color?: ColorValue,
    ): this;
    drawLine(
      x0: number,
      y0: number,
      x1: number,
      y1: number,
      color?: ColorValue,
    ): this;
    drawRect(
      x: number,
      y: number,
      width: number,
      height: number,
      color?: ColorValue,
    ): this;
    drawCircle(cx: number, cy: number, radius: number, color?: ColorValue): this;
    fillCircle(cx: number, cy: number, radius: number, color?: ColorValue): this;
    drawEllipse(
      cx: number,
      cy: number,
      rx: number,
      ry: number,
      color?: ColorValue,
      options?: CurveOptions,
    ): this;
    fillEllipse(
      cx: number,
      cy: number,
      rx: number,
      ry: number,
      color?: ColorValue,
    ): this;
    drawRoundRect(
      x: number,
      y: number,
      width: number,
      height: number,
      radius: number,
      color?: ColorValue,
    ): this;
    fillRoundRect(
      x: number,
      y: number,
      width: number,
      height: number,
      radius: number,
      color?: ColorValue,
    ): this;
    drawPolyline(points: PointList, color?: ColorValue): this;
    drawPolygon(points: PointList, color?: ColorValue): this;
    fillPolygon(points: PointList, color?: ColorValue): this;
    drawTriangle(
      x0: number,
      y0: number,
      x1: number,
      y1: number,
      x2: number,
      y2: number,
      color?: ColorValue,
    ): this;
    fillTriangle(
      x0: number,
      y0: number,
      x1: number,
      y1: number,
      x2: number,
      y2: number,
      color?: ColorValue,
    ): this;
    drawQuadraticBezier(
      x0: number,
      y0: number,
      cx: number,
      cy: number,
      x1: number,
      y1: number,
      color?: ColorValue,
      options?: CurveOptions,
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
      color?: ColorValue,
      options?: CurveOptions,
    ): this;
    drawMask(x: number, y: number, mask: MonoMask, options?: MaskStyle): this;
    blit(source: BitmapSource, options?: BitmapBlitOptions): this;
    drawChar(x: number, y: number, ch: string, options?: TextStyle): this;
    drawText(x: number, y: number, text: string, options?: TextStyle): this;
    getDirty(): Rect | null;
    clearDirty(): this;
    markDirty(x: number, y: number, width: number, height: number): this;
    beginBatch(options?: unknown): SurfaceBatch | null;
    endBatch(batch: SurfaceBatch): this;
    measureText(text: string, style?: TextStyle): TextMetrics;
    close(): boolean;
  }

  type DisplayCapability =
    | "partialPresent"
    | "multiRegion"
    | "power"
    | "inversion"
    | "contrast"
    | "backlight"
    | "batch"
    | "directSource";

  interface DisplayOptions {
    surface?: SurfaceOptions;
    present?: DisplayPresentOptions;
    metrics?: boolean;
    profileName?: string;
  }

  type DisplayRectInput = Rect | [number, number, number, number];

  /** Public drawing facade composed from a Surface and PanelDriver. */
  class Display {
    constructor(driver: PanelDriver, options?: DisplayOptions);
    readonly driver: PanelDriver;
    readonly driverName: string;
    readonly profileName: string | null;
    readonly surface: Surface;
    readonly width: number;
    readonly height: number;
    readonly pixelFormat: string;
    readonly foreground: number;
    readonly background: number;
    readonly capabilities: Record<DisplayCapability, boolean>;
    state: DisplayObjectState;
    ready: boolean;
    open(): this;
    close(): boolean;
    clear(color?: ColorValue): this;
    fill(color?: ColorValue): this;
    setPixel(x: number, y: number, color?: ColorValue): this;
    getPixel(x: number, y: number): number;
    fillRect(x: number, y: number, width: number, height: number, color?: ColorValue): this;
    drawLine(x0: number, y0: number, x1: number, y1: number, color?: ColorValue): this;
    drawRect(x: number, y: number, width: number, height: number, color?: ColorValue): this;
    drawCircle(cx: number, cy: number, radius: number, color?: ColorValue): this;
    fillCircle(cx: number, cy: number, radius: number, color?: ColorValue): this;
    drawEllipse(cx: number, cy: number, rx: number, ry: number, color?: ColorValue, options?: CurveOptions): this;
    fillEllipse(cx: number, cy: number, rx: number, ry: number, color?: ColorValue): this;
    drawRoundRect(x: number, y: number, width: number, height: number, radius: number, color?: ColorValue): this;
    fillRoundRect(x: number, y: number, width: number, height: number, radius: number, color?: ColorValue): this;
    drawPolyline(points: PointList, color?: ColorValue): this;
    drawPolygon(points: PointList, color?: ColorValue): this;
    fillPolygon(points: PointList, color?: ColorValue): this;
    drawTriangle(x0: number, y0: number, x1: number, y1: number, x2: number, y2: number, color?: ColorValue): this;
    fillTriangle(x0: number, y0: number, x1: number, y1: number, x2: number, y2: number, color?: ColorValue): this;
    drawQuadraticBezier(x0: number, y0: number, cx: number, cy: number, x1: number, y1: number, color?: ColorValue, options?: CurveOptions): this;
    drawCubicBezier(x0: number, y0: number, c1x: number, c1y: number, c2x: number, c2y: number, x1: number, y1: number, color?: ColorValue, options?: CurveOptions): this;
    drawMask(x: number, y: number, mask: MonoMask, options?: MaskStyle): this;
    blit(source: BitmapSource, options?: BitmapBlitOptions): this;
    drawChar(x: number, y: number, ch: string, options?: TextStyle): this;
    drawText(x: number, y: number, text: string, options?: TextStyle): this;
    measureText(text: string, style?: TextStyle): TextMetrics;
    beginBatch(options?: unknown): SurfaceBatch | null;
    endBatch(batch: SurfaceBatch): this;
    present(
      regions?: DisplayRectInput | ArrayLike<DisplayRectInput>,
      options?: DisplayPresentOptions,
    ): this;
    flush(): this;
    flushRect(x: number, y: number, width: number, height: number): this;
    flushRects(
      regions: ArrayLike<DisplayRectInput>,
      options?: DisplayPresentOptions,
    ): this;
    supports(capability: DisplayCapability | string): boolean;
    setPower(enabled: boolean): this;
    setInverted(enabled: boolean): this;
    setContrast(value: number): this;
    setBacklight(enabled: boolean): this;
    stats(): DisplayStats;
    resetStats(): this;
  }

  /** Layered display helper module loaded from `_sys/display.js`. */
  interface DisplayModule {
    readonly VERSION: "0.5.0";
    __loaded: boolean;
    Display: typeof Display;
    Surface: typeof Surface;
    readonly transports: DisplayTransportRegistry;
    readonly drivers: PanelDriverRegistry;
    readonly profiles: DisplayProfileRegistry;
    fonts: Record<string, DisplayFont>;
    defaultFont: DisplayFont;
    mono1(value: number): number;
    gray4(value: number): number;
    gray8(value: number): number;
    rgb565(red: number, green: number, blue: number): number;
    rgb888(red: number, green: number, blue: number): number;
    measureText(text: string, style?: TextStyle): TextMetrics;
    encodeText(text: string, font?: DisplayFont): string;
    fontNeedsTextMapping(font?: DisplayFont): boolean;
    registerFont(name: string, font: DisplayFont): DisplayFont;
    loadFont(path: string, name?: string): DisplayFont;
    loadFontSet(path: string): DisplayFontSet;
    loadMappedFont(path: string, size: string | number, name?: string): DisplayFont;
    create(driver: PanelDriver, options?: DisplayOptions): Display;
    open(driver: PanelDriver, options?: DisplayOptions): Display;
  }

  /** Insets used by the UI helpers. */
  interface Insets {
    top: number;
    right: number;
    bottom: number;
    left: number;
  }

  interface InsetsInput {
    top?: number;
    right?: number;
    bottom?: number;
    left?: number;
    x?: number;
    y?: number;
  }

}

  /** JavaScript display helpers loaded from `_sys/display.js`. */
  var display: ESP32QJS.DisplayModule;
}

export {};
