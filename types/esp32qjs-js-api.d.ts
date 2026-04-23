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
    spacing?: number;
  }

  /**
   * Monochrome bitmap payload accepted by `drawBitmap(...)`.
   */
  interface Bitmap {
    width: number;
    height: number;
    pixels: ArrayLike<number | boolean>;
  }

  /**
   * Base display surface.
   */
  class Surface {
    constructor(options?: SurfaceOptions);
    driver: string;
    width: number;
    height: number;
    pixelFormat: string;
    ready: boolean;
    init(): this;
    flush(): this;
    measureText(text: string, style?: TextStyle): TextMetrics;
  }

  interface MonoSurfaceOptions extends SurfaceOptions {
    spacing?: number;
  }

  /**
   * Generic 1-bit framebuffer surface.
   *
   * @example
   * ```js
   * var oled = display.open({ driver: "ssd1306", sda: 5, scl: 6, address: 0x3c });
   * oled.clear();
   * oled.drawText(0, 0, "HELLO");
   * oled.flush();
   * ```
   */
  class MonoSurface extends Surface {
    constructor(options?: MonoSurfaceOptions);
    pages: number;
    spacing: number;
    buffer: number[];
    clear(enabled?: boolean): this;
    fill(enabled?: boolean): this;
    setPixel(x: number, y: number, enabled: boolean): this;
    getPixel(x: number, y: number): boolean;
    fillRect(
      x: number,
      y: number,
      width: number,
      height: number,
      enabled: boolean,
    ): this;
    drawLine(
      x0: number,
      y0: number,
      x1: number,
      y1: number,
      enabled: boolean,
    ): this;
    drawRect(
      x: number,
      y: number,
      width: number,
      height: number,
      enabled: boolean,
    ): this;
    drawBitmap(x: number, y: number, bitmap: Bitmap, enabled?: boolean): this;
    drawChar(x: number, y: number, ch: string, enabled: boolean): this;
    drawText(
      x: number,
      y: number,
      text: string,
      enabled?: boolean,
      spacing?: number,
    ): this;
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
    readonly FONT_5X7: Record<string, number[]>;
    readonly Surface: typeof Surface;
    readonly MonoSurface: typeof MonoSurface;
    measureText(text: string, style?: TextStyle): TextMetrics;
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
