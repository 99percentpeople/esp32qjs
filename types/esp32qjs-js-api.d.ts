declare namespace ESP32QJS {
  type ColorValue = boolean | number | null | undefined;

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
    width: number;
    height: number;
    advance: number;
    lineHeight: number;
    native?: unknown;
  }

  interface DisplayFontSet {
    load(size: string | number, name?: string): DisplayFont;
  }

  /**
   * Base display surface options.
   */
  interface SurfaceOptions {
    driver?: string;
    width?: number;
    height?: number;
    pixelFormat?: string;
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

  interface BitmapStyle {
    color?: ColorValue;
    background?: ColorValue;
  }

  /**
   * Monochrome bitmap payload accepted by `drawBitmap(...)`.
   */
  interface Bitmap {
    width: number;
    height: number;
    pixels: ArrayLike<number | boolean>;
  }

  type PointInput = Point | [number, number];
  type PointList = ArrayLike<number> | ArrayLike<PointInput>;

  interface CurveOptions {
    segments?: number;
  }

  /**
   * Base display surface. Concrete drivers implement drawing methods.
   */
  class Surface {
    constructor(options?: SurfaceOptions);
    driver: string;
    width: number;
    height: number;
    pixelFormat: string;
    ready: boolean;
    clear(color?: ColorValue): this;
    fill(color?: ColorValue): this;
    setPixel(x: number, y: number, color: ColorValue): this;
    getPixel(x: number, y: number): number;
    fillRect(
      x: number,
      y: number,
      width: number,
      height: number,
      color: ColorValue,
    ): this;
    drawLine(
      x0: number,
      y0: number,
      x1: number,
      y1: number,
      color: ColorValue,
    ): this;
    drawRect(
      x: number,
      y: number,
      width: number,
      height: number,
      color: ColorValue,
    ): this;
    drawCircle(cx: number, cy: number, radius: number, color: ColorValue): this;
    fillCircle(cx: number, cy: number, radius: number, color: ColorValue): this;
    drawEllipse(
      cx: number,
      cy: number,
      rx: number,
      ry: number,
      color: ColorValue,
      options?: CurveOptions,
    ): this;
    fillEllipse(
      cx: number,
      cy: number,
      rx: number,
      ry: number,
      color: ColorValue,
    ): this;
    drawRoundRect(
      x: number,
      y: number,
      width: number,
      height: number,
      radius: number,
      color: ColorValue,
    ): this;
    fillRoundRect(
      x: number,
      y: number,
      width: number,
      height: number,
      radius: number,
      color: ColorValue,
    ): this;
    drawPolyline(points: PointList, color: ColorValue): this;
    drawPolygon(points: PointList, color: ColorValue): this;
    fillPolygon(points: PointList, color: ColorValue): this;
    drawTriangle(
      x0: number,
      y0: number,
      x1: number,
      y1: number,
      x2: number,
      y2: number,
      color: ColorValue,
    ): this;
    fillTriangle(
      x0: number,
      y0: number,
      x1: number,
      y1: number,
      x2: number,
      y2: number,
      color: ColorValue,
    ): this;
    drawQuadraticBezier(
      x0: number,
      y0: number,
      cx: number,
      cy: number,
      x1: number,
      y1: number,
      color: ColorValue,
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
      color: ColorValue,
      options?: CurveOptions,
    ): this;
    drawBitmap(x: number, y: number, bitmap: Bitmap, options?: BitmapStyle): this;
    drawChar(x: number, y: number, ch: string, options?: TextStyle): this;
    drawText(x: number, y: number, text: string, options?: TextStyle): this;
    init(): this;
    beginBatch?(options?: unknown): Surface;
    endBatch?(batch: Surface, dirty?: Rect | null): this;
    flush(): this;
    flushRect?(x: number, y: number, width: number, height: number): this;
    flushRects?(
      rects: ArrayLike<Rect | [number, number, number, number]>,
      options?: { merge?: boolean },
    ): this;
    measureText(text: string, style?: TextStyle): TextMetrics;
  }

  /**
   * Display creation options shared by the JS display helpers.
   */
  interface DisplayOpenOptions extends I2COpenOptions {
    driver: string;
    address?: number;
    width?: number;
    height?: number;
    spacing?: number;
    [key: string]: unknown;
  }

  type DisplayDriverFactory<T extends Surface = Surface> = (
    options: DisplayOpenOptions,
  ) => T;

  /**
   * Display helper module loaded from `_sys/display.js`.
   *
   * @example
   * ```js
   * load("_sys/display.js");
   * var oled = display.open({ driver: "ssd1306", sda: 5, scl: 6, address: 0x3c });
   * oled.drawText(0, 0, "HELLO");
   * oled.flush();
   * ```
   */
  interface DisplayModule {
    readonly VERSION: string;
    readonly Surface: typeof Surface;
    readonly fonts: Record<string, DisplayFont>;
    readonly defaultFont: DisplayFont;
    mono1(value: number): number;
    gray4(value: number): number;
    gray8(value: number): number;
    rgb565(red: number, green: number, blue: number): number;
    measureText(text: string, style?: TextStyle): TextMetrics;
    encodeText(text: string, font?: DisplayFont): string;
    fontNeedsTextMapping(font?: DisplayFont): boolean;
    registerFont(name: string, font: DisplayFont): DisplayFont;
    loadFont(path: string, name?: string): DisplayFont;
    loadFontSet(path: string): DisplayFontSet;
    loadMappedFont(path: string, size: string | number, name?: string): DisplayFont;
    listDrivers(): string[];
    registerDriver<T extends Surface = Surface>(
      name: string,
      factory: DisplayDriverFactory<T>,
    ): this;
    create<T extends Surface = Surface>(options: DisplayOpenOptions): T;
    open<T extends Surface = Surface>(options: DisplayOpenOptions): T;
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

  type UIAlign = "start" | "center" | "end" | "stretch";
  type UISoftkeyResult = "left" | "center" | "right" | null;

  interface UITouchInput {
    x?: number;
    y?: number;
    pressed?: boolean;
  }

  interface UIInput {
    up?: boolean;
    down?: boolean;
    left?: boolean;
    right?: boolean;
    ok?: boolean;
    back?: boolean;
    encoderDelta?: number;
    touch?: UITouchInput | null;
  }

  interface UITheme {
    background?: ColorValue;
    foreground?: ColorValue;
    muted?: ColorValue;
    panel?: ColorValue;
    control?: ColorValue;
    border?: ColorValue;
    accent?: ColorValue;
    accentText?: ColorValue;
    danger?: ColorValue;
    focus?: ColorValue;
    pressed?: ColorValue;
  }

  interface UIFrameOptions extends Partial<Rect> {
    clear?: boolean;
    clearColor?: ColorValue;
    flush?: boolean;
    partial?: boolean;
    gcBeforeFlush?: boolean;
    batch?: boolean | unknown;
    focusVisible?: boolean;
    input?: UIInput;
    theme?: UITheme;
    gap?: number;
    align?: UIAlign;
    font?: DisplayFont;
  }

  interface UILayoutOptions extends Partial<Rect> {
    id?: string;
    padding?: number | InsetsInput;
    gap?: number;
    align?: UIAlign;
    background?: ColorValue;
    border?: boolean;
    borderColor?: ColorValue;
    radius?: number;
    borderRadius?: number;
  }

  interface UISpacerOptions extends Partial<Rect> {
    size?: number;
  }

  interface UISeparatorOptions extends Partial<Rect> {
    id?: string;
    thickness?: number;
    color?: ColorValue;
  }

  interface UITextOptions extends UILayoutOptions, TextStyle {
    valign?: UIAlign;
    textAlign?: UIAlign;
  }

  interface UIStyleOptions extends Partial<Rect>, TextStyle {
    padding?: number | InsetsInput;
    gap?: number;
    align?: UIAlign;
    background?: ColorValue;
    border?: boolean;
    borderColor?: ColorValue;
    radius?: number;
    borderRadius?: number;
    valign?: UIAlign;
    textAlign?: UIAlign;
    outline?: boolean;
    outlineColor?: ColorValue;
    outlineWidth?: number;
  }

  interface UIControlOptions extends UIStyleOptions {
    id?: string;
    label?: string;
    left?: string;
    title?: string;
    center?: string;
    right?: string;
    selected?: boolean;
    min?: number;
    max?: number;
    step?: number;
    visibleCount?: number;
    rowHeight?: number;
  }

  interface UIFpsOptions extends UIStyleOptions {
    enabled?: boolean;
    sampleMs?: number;
    precision?: number;
    charWidth?: number;
  }

  interface UIComponent<T = Rect> extends Partial<Rect> {
    readonly rendered: boolean;
    readonly result: T;
    readonly value: T;
    readonly rect: Rect | null;
    style(options?: UIStyleOptions): this;
    valueOf(): T;
    toString(): string;
  }

  type UIControlCommand = "up" | "down" | "left" | "right" | "ok" | "back";

  interface UIControlButtonBinding {
    pin: number;
    command?: UIControlCommand;
    activeLow?: boolean;
    pull?: string | false | null;
  }

  interface UIControlButtonOptions {
    activeLow?: boolean;
    pull?: string | false | null;
    configure?: boolean;
  }

  interface UIControlIndicatorOptions extends UIControlOptions {
    idle?: string;
  }

  interface UIControlDriver {
    press(command: UIControlCommand): this;
    hold(command: UIControlCommand, active?: boolean): this;
    encoder(delta: number): this;
    touch(x: number, y: number, pressed?: boolean): this;
    bindButtons(
      map: Record<string, number | UIControlButtonBinding | false | null | undefined>,
      options?: UIControlButtonOptions,
    ): this;
    read(extra?: UIInput): UIInput;
    last(): string;
    clear(): this;
    indicator(options?: UIControlIndicatorOptions): Rect;
  }

  interface UIContext {
    surface: Surface;
    focusedId: string | null;
    focusVisible: boolean;
    editingId: string | null;
    input: UIInput;
    theme: Required<UITheme>;
  }

  /** Immediate-mode UI helpers loaded from `_sys/ui.js`. */
  interface UIModule {
    readonly VERSION: string;
    readonly theme: {
      dark: Required<UITheme>;
      mono: Required<UITheme>;
      [name: string]: Required<UITheme>;
    };
    input(input?: UIInput): UIInput;
    begin(surface: Surface, options?: UIFrameOptions): UIContext;
    endFrame(): UIContext;
    frame(
      surface: Surface,
      render: (context: UIContext) => void,
      options?: UIFrameOptions,
    ): UIContext;
    row(options?: UILayoutOptions): UIComponent<Rect>;
    column(options?: UILayoutOptions): UIComponent<Rect>;
    group(options?: UILayoutOptions): UIComponent<Rect>;
    panel(options?: UILayoutOptions): UIComponent<Rect>;
    end(): Rect;
    spacer(sizeOrOptions?: number | UISpacerOptions): Rect;
    separator(options?: UISeparatorOptions): UIComponent<Rect>;
    text(value: string | number | boolean, options?: UITextOptions): UIComponent<Rect>;
    value(
      label: string | number | boolean,
      value: string | number | boolean,
      options?: UITextOptions,
    ): UIComponent<Rect>;
    badge(text: string | number | boolean, options?: UIControlOptions): UIComponent<Rect>;
    icon(name: string, options?: UITextOptions): UIComponent<Rect>;
    statusBar(options?: UIControlOptions): UIComponent<Rect>;
    progress(id: string, value: number, options?: UIControlOptions): UIComponent<Rect>;
    /** Optional helper loaded from `_sys/ui/control.js`. */
    control?: UIControlDriver;
    /** Optional helper loaded from `_sys/ui/fps.js`. */
    fps?: (id: string, options?: UIFpsOptions) => number;
    gauge(id: string, value: number, options?: UIControlOptions): UIComponent<Rect>;
    button(
      id: string,
      label: string | number | boolean,
      options?: UIControlOptions,
    ): UIComponent<boolean>;
    iconButton(id: string, icon: string, options?: UIControlOptions): UIComponent<boolean>;
    toggle(
      id: string,
      label: string | number | boolean,
      value: boolean,
      options?: UIControlOptions,
    ): UIComponent<boolean>;
    checkbox(
      id: string,
      label: string | number | boolean,
      value: boolean,
      options?: UIControlOptions,
    ): UIComponent<boolean>;
    slider(id: string, value: number, options?: UIControlOptions): UIComponent<number>;
    stepper(id: string, value: number, options?: UIControlOptions): UIComponent<number>;
    list(
      id: string,
      items: ArrayLike<string | number | boolean>,
      selectedIndex: number,
      options?: UIControlOptions,
    ): UIComponent<number>;
    menu(
      id: string,
      items: ArrayLike<string | number | boolean>,
      selectedIndex: number,
      options?: UIControlOptions,
    ): UIComponent<number>;
    tabs(
      id: string,
      tabs: ArrayLike<string | number | boolean>,
      selectedIndex: number,
      options?: UIControlOptions,
    ): UIComponent<number>;
    softkeys(
      left?: string | number | boolean,
      center?: string | number | boolean,
      right?: string | number | boolean,
      options?: UIControlOptions,
    ): UIComponent<UISoftkeyResult>;
  }
}

declare global {
  /** JavaScript display helpers loaded from `_sys/display.js`. */
  const display: ESP32QJS.DisplayModule;
  /** JavaScript UI/layout helpers loaded from `_sys/ui.js`. */
  const ui: ESP32QJS.UIModule;
}

export {};
