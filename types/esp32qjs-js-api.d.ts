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

  /**
   * Insets used by the UI layout helpers.
   */
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

  type Align = "start" | "center" | "end" | "stretch";
  type Justify = "start" | "center" | "end" | "space-between";

  interface UIBaseProps {
    width?: number;
    height?: number;
    flex?: number;
    gap?: number;
    padding?: number | InsetsInput;
    background?: ColorValue;
    border?: boolean;
    borderColor?: ColorValue;
    align?: Align;
    valign?: Align;
    justify?: Justify;
    spacing?: number;
    color?: ColorValue;
  }

  interface UITextProps extends UIBaseProps {
    text?: string;
  }

  interface UISpacerProps {
    size?: number;
    width?: number;
    height?: number;
  }

  /**
   * UI layout node returned by `ui.box(...)`, `ui.row(...)`, and friends.
   */
  interface UINode<P extends object = Record<string, unknown>> {
    type: string;
    props: P;
    children: UINode[];
    frame: Rect | null;
    contentFrame: Rect | null;
  }

  /**
   * Render options accepted by `ui.render(...)`.
   */
  interface UIRenderOptions extends Partial<Rect> {
    clear?: boolean;
    clearColor?: ColorValue;
    flush?: boolean;
  }

  /**
   * UI/layout helpers loaded from `_sys/ui.js`.
   *
   * @example
   * ```js
   * load("_sys/display.js");
   * load("_sys/ui.js");
   * var oled = display.open({ driver: "ssd1306", sda: 5, scl: 6, address: 0x3c });
   * var screen = ui.column(
   *   { gap: 2, padding: 2 },
   *   ui.text("HELLO"),
   *   ui.row(ui.box({ width: 12, height: 12, border: true }), ui.text("WIFI OK"))
   * );
   * ui.render(oled, screen);
   * ```
   */
  interface UIModule {
    readonly VERSION: string;
    node(
      type: string,
      props?: Record<string, unknown>,
      children?: UINode[],
    ): UINode;
    box(
      props?: UIBaseProps,
      ...children: Array<
        | UINode
        | string
        | number
        | boolean
        | Array<UINode | string | number | boolean>
      >
    ): UINode<UIBaseProps>;
    row(
      props?: UIBaseProps,
      ...children: Array<
        | UINode
        | string
        | number
        | boolean
        | Array<UINode | string | number | boolean>
      >
    ): UINode<UIBaseProps>;
    column(
      props?: UIBaseProps,
      ...children: Array<
        | UINode
        | string
        | number
        | boolean
        | Array<UINode | string | number | boolean>
      >
    ): UINode<UIBaseProps>;
    text(
      value: string | number | boolean,
      props?: UITextProps,
    ): UINode<UITextProps>;
    spacer(sizeOrProps: number | UISpacerProps): UINode<UISpacerProps>;
    padding(
      insets: number | InsetsInput,
      child: UINode | string | number | boolean,
      props?: UIBaseProps,
    ): UINode<UIBaseProps>;
    measure(surface: Surface, node: UINode | string | number | boolean): Size;
    layout(
      surface: Surface,
      node: UINode | string | number | boolean,
      options?: Partial<Rect>,
    ): UINode;
    render(
      surface: Surface,
      node: UINode | string | number | boolean,
      options?: UIRenderOptions,
    ): UINode;
  }
}

declare global {
  /** JavaScript display helpers loaded from `_sys/display.js`. */
  const display: ESP32QJS.DisplayModule;
  /** JavaScript UI/layout helpers loaded from `_sys/ui.js`. */
  const ui: ESP32QJS.UIModule;
}

export {};
