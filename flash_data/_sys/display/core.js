(function (global) {
  var owns = Object.prototype.hasOwnProperty;
  var FONT_5X7 = {
    " ": [0x00, 0x00, 0x00, 0x00, 0x00],
    "!": [0x00, 0x00, 0x5f, 0x00, 0x00],
    "\"": [0x00, 0x07, 0x00, 0x07, 0x00],
    "#": [0x14, 0x7f, 0x14, 0x7f, 0x14],
    "%": [0x23, 0x13, 0x08, 0x64, 0x62],
    "&": [0x36, 0x49, 0x55, 0x22, 0x50],
    "'": [0x00, 0x05, 0x03, 0x00, 0x00],
    "(": [0x00, 0x1c, 0x22, 0x41, 0x00],
    ")": [0x00, 0x41, 0x22, 0x1c, 0x00],
    "*": [0x14, 0x08, 0x3e, 0x08, 0x14],
    "+": [0x08, 0x08, 0x3e, 0x08, 0x08],
    ",": [0x00, 0x50, 0x30, 0x00, 0x00],
    "-": [0x08, 0x08, 0x08, 0x08, 0x08],
    ".": [0x00, 0x60, 0x60, 0x00, 0x00],
    "/": [0x20, 0x10, 0x08, 0x04, 0x02],
    "0": [0x3e, 0x51, 0x49, 0x45, 0x3e],
    "1": [0x00, 0x42, 0x7f, 0x40, 0x00],
    "2": [0x42, 0x61, 0x51, 0x49, 0x46],
    "3": [0x21, 0x41, 0x45, 0x4b, 0x31],
    "4": [0x18, 0x14, 0x12, 0x7f, 0x10],
    "5": [0x27, 0x45, 0x45, 0x45, 0x39],
    "6": [0x3c, 0x4a, 0x49, 0x49, 0x30],
    "7": [0x01, 0x71, 0x09, 0x05, 0x03],
    "8": [0x36, 0x49, 0x49, 0x49, 0x36],
    "9": [0x06, 0x49, 0x49, 0x29, 0x1e],
    ":": [0x00, 0x36, 0x36, 0x00, 0x00],
    ";": [0x00, 0x56, 0x36, 0x00, 0x00],
    "<": [0x08, 0x14, 0x22, 0x41, 0x00],
    "=": [0x14, 0x14, 0x14, 0x14, 0x14],
    ">": [0x00, 0x41, 0x22, 0x14, 0x08],
    "?": [0x02, 0x01, 0x51, 0x09, 0x06],
    "@": [0x32, 0x49, 0x79, 0x41, 0x3e],
    "A": [0x7e, 0x11, 0x11, 0x11, 0x7e],
    "B": [0x7f, 0x49, 0x49, 0x49, 0x36],
    "C": [0x3e, 0x41, 0x41, 0x41, 0x22],
    "D": [0x7f, 0x41, 0x41, 0x22, 0x1c],
    "E": [0x7f, 0x49, 0x49, 0x49, 0x41],
    "F": [0x7f, 0x09, 0x09, 0x09, 0x01],
    "G": [0x3e, 0x41, 0x49, 0x49, 0x7a],
    "H": [0x7f, 0x08, 0x08, 0x08, 0x7f],
    "I": [0x00, 0x41, 0x7f, 0x41, 0x00],
    "J": [0x20, 0x40, 0x41, 0x3f, 0x01],
    "K": [0x7f, 0x08, 0x14, 0x22, 0x41],
    "L": [0x7f, 0x40, 0x40, 0x40, 0x40],
    "M": [0x7f, 0x02, 0x0c, 0x02, 0x7f],
    "N": [0x7f, 0x04, 0x08, 0x10, 0x7f],
    "O": [0x3e, 0x41, 0x41, 0x41, 0x3e],
    "P": [0x7f, 0x09, 0x09, 0x09, 0x06],
    "Q": [0x3e, 0x41, 0x51, 0x21, 0x5e],
    "R": [0x7f, 0x09, 0x19, 0x29, 0x46],
    "S": [0x46, 0x49, 0x49, 0x49, 0x31],
    "T": [0x01, 0x01, 0x7f, 0x01, 0x01],
    "U": [0x3f, 0x40, 0x40, 0x40, 0x3f],
    "V": [0x1f, 0x20, 0x40, 0x20, 0x1f],
    "W": [0x7f, 0x20, 0x18, 0x20, 0x7f],
    "X": [0x63, 0x14, 0x08, 0x14, 0x63],
    "Y": [0x03, 0x04, 0x78, 0x04, 0x03],
    "Z": [0x61, 0x51, 0x49, 0x45, 0x43],
    "[": [0x00, 0x7f, 0x41, 0x41, 0x00],
    "\\": [0x02, 0x04, 0x08, 0x10, 0x20],
    "]": [0x00, 0x41, 0x41, 0x7f, 0x00],
    "^": [0x04, 0x02, 0x01, 0x02, 0x04],
    "_": [0x40, 0x40, 0x40, 0x40, 0x40]
  };
  var system = global.__displaySystem;
  var drivers;
  var display;

  if (system && system.coreLoaded) {
    return;
  }

  function own(obj, key) {
    return owns.call(obj, key);
  }

  function inherit(childCtor, parentCtor) {
    childCtor.prototype = Object.create(parentCtor.prototype);
    childCtor.prototype.constructor = childCtor;
  }

  function toBool(value, fallback) {
    if (value === undefined) {
      return fallback;
    }
    return !!value;
  }

  function toColor(value) {
    return value === false || value === 0 || value === null ? 0 : 1;
  }

  function clampInt(value, minValue, maxValue) {
    var number = value | 0;

    if (number < minValue) {
      return minValue;
    }
    if (number > maxValue) {
      return maxValue;
    }
    return number;
  }

  function normalizeChar(ch) {
    if (!ch || ch.length === 0) {
      return " ";
    }
    if (ch >= "a" && ch <= "z") {
      return ch.toUpperCase();
    }
    if (!own(FONT_5X7, ch)) {
      return "?";
    }
    return ch;
  }

  function measureText(text, style) {
    var str = String(text);
    var spacing = style && own(style, "spacing") ? style.spacing : 0;
    var lineWidth = 0;
    var maxWidth = 0;
    var lines = 1;
    var i;

    for (i = 0; i < str.length; i += 1) {
      if (str.charAt(i) === "\n") {
        if (lineWidth > maxWidth) {
          maxWidth = lineWidth;
        }
        lineWidth = 0;
        lines += 1;
        continue;
      }
      lineWidth += 6 + spacing;
    }

    if (lineWidth > maxWidth) {
      maxWidth = lineWidth;
    }
    if (maxWidth > 0) {
      maxWidth -= spacing;
    }

    return {
      width: maxWidth,
      height: lines * 8,
      lines: lines
    };
  }

  function Surface(options) {
    options = options || {};

    this.driver = own(options, "driver") ? options.driver : "unknown";
    this.width = own(options, "width") ? options.width : 0;
    this.height = own(options, "height") ? options.height : 0;
    this.pixelFormat = own(options, "pixelFormat") ? options.pixelFormat : "mono1";
    this.ready = false;
  }

  Surface.prototype.init = function () {
    this.ready = true;
    return this;
  };

  Surface.prototype.flush = function () {
    throw new Error("display surface does not implement flush()");
  };

  Surface.prototype.measureText = function (text, style) {
    return measureText(text, style);
  };

  function MonoSurface(options) {
    options = options || {};

    Surface.call(this, {
      driver: options.driver,
      width: options.width,
      height: options.height,
      pixelFormat: "mono1"
    });
    this.pages = Math.ceil(this.height / 8);
    this.spacing = own(options, "spacing") ? options.spacing : 0;
    this.buffer = new Array(this.width * this.pages);
    this.clear(false);
  }

  inherit(MonoSurface, Surface);

  MonoSurface.prototype.clear = function (enabled) {
    var fillByte = toColor(enabled) ? 0xff : 0x00;
    var i;

    for (i = 0; i < this.buffer.length; i += 1) {
      this.buffer[i] = fillByte;
    }
    return this;
  };

  MonoSurface.prototype.fill = function (enabled) {
    return this.clear(enabled);
  };

  MonoSurface.prototype._indexFor = function (x, y) {
    return x + (this.width * (y >> 3));
  };

  MonoSurface.prototype.setPixel = function (x, y, enabled) {
    var index;
    var mask;

    x = x | 0;
    y = y | 0;
    if (x < 0 || y < 0 || x >= this.width || y >= this.height) {
      return this;
    }

    index = this._indexFor(x, y);
    mask = 1 << (y & 7);
    if (toColor(enabled)) {
      this.buffer[index] |= mask;
    } else {
      this.buffer[index] &= (0xff ^ mask);
    }
    return this;
  };

  MonoSurface.prototype.getPixel = function (x, y) {
    var index;
    var mask;

    x = x | 0;
    y = y | 0;
    if (x < 0 || y < 0 || x >= this.width || y >= this.height) {
      return false;
    }

    index = this._indexFor(x, y);
    mask = 1 << (y & 7);
    return (this.buffer[index] & mask) !== 0;
  };

  MonoSurface.prototype.fillRect = function (x, y, width, height, enabled) {
    var xx;
    var yy;

    x = x | 0;
    y = y | 0;
    width = width | 0;
    height = height | 0;
    if (width <= 0 || height <= 0) {
      return this;
    }

    for (yy = y; yy < y + height; yy += 1) {
      for (xx = x; xx < x + width; xx += 1) {
        this.setPixel(xx, yy, enabled);
      }
    }
    return this;
  };

  MonoSurface.prototype.drawLine = function (x0, y0, x1, y1, enabled) {
    var dx = Math.abs(x1 - x0);
    var sx = x0 < x1 ? 1 : -1;
    var dy = -Math.abs(y1 - y0);
    var sy = y0 < y1 ? 1 : -1;
    var err = dx + dy;
    var e2;

    while (true) {
      this.setPixel(x0, y0, enabled);
      if (x0 === x1 && y0 === y1) {
        break;
      }
      e2 = err << 1;
      if (e2 >= dy) {
        err += dy;
        x0 += sx;
      }
      if (e2 <= dx) {
        err += dx;
        y0 += sy;
      }
    }
    return this;
  };

  MonoSurface.prototype.drawRect = function (x, y, width, height, enabled) {
    if ((width | 0) <= 0 || (height | 0) <= 0) {
      return this;
    }

    this.drawLine(x, y, x + width - 1, y, enabled);
    this.drawLine(x, y + height - 1, x + width - 1, y + height - 1, enabled);
    this.drawLine(x, y, x, y + height - 1, enabled);
    this.drawLine(x + width - 1, y, x + width - 1, y + height - 1, enabled);
    return this;
  };

  MonoSurface.prototype.drawBitmap = function (x, y, bitmap, enabled) {
    var pixels;
    var width;
    var height;
    var row;
    var col;
    var index = 0;

    if (!bitmap || !own(bitmap, "width") || !own(bitmap, "height") || !own(bitmap, "pixels")) {
      throw new Error("drawBitmap(x, y, bitmap) expects { width, height, pixels }");
    }

    width = bitmap.width | 0;
    height = bitmap.height | 0;
    pixels = bitmap.pixels;
    for (row = 0; row < height; row += 1) {
      for (col = 0; col < width; col += 1) {
        this.setPixel(x + col, y + row, pixels[index] ? enabled !== false : false);
        index += 1;
      }
    }
    return this;
  };

  MonoSurface.prototype.drawChar = function (x, y, ch, enabled) {
    var glyph = FONT_5X7[normalizeChar(ch)];
    var col;
    var row;
    var bits;

    this.fillRect(x, y, 6, 8, false);
    for (col = 0; col < glyph.length; col += 1) {
      bits = glyph[col];
      for (row = 0; row < 7; row += 1) {
        if ((bits & (1 << row)) !== 0) {
          this.setPixel(x + col, y + row, enabled !== false);
        }
      }
    }
    return this;
  };

  MonoSurface.prototype.drawText = function (x, y, text, enabled, spacing) {
    var str = String(text);
    var cursorX = x | 0;
    var cursorY = y | 0;
    var gap = spacing === undefined ? this.spacing : spacing;
    var step = 6 + gap;
    var i;
    var ch;

    for (i = 0; i < str.length; i += 1) {
      ch = str.charAt(i);
      if (ch === "\n") {
        cursorX = x | 0;
        cursorY += 8;
        continue;
      }
      this.drawChar(cursorX, cursorY, ch, enabled);
      cursorX += step;
    }
    return this;
  };

  drivers = {};
  display = global.display || {};
  system = {
    coreLoaded: true,
    drivers: drivers,
    own: own,
    inherit: inherit,
    toBool: toBool,
    toColor: toColor,
    clampInt: clampInt,
    normalizeChar: normalizeChar,
    font5x7: FONT_5X7
  };

  display.VERSION = "0.2.0";
  display.FONT_5X7 = FONT_5X7;
  display.Surface = Surface;
  display.MonoSurface = MonoSurface;
  display.measureText = measureText;
  display.listDrivers = function () {
    return Object.keys(drivers);
  };
  display.registerDriver = function (name, factory) {
    if (!name || typeof name !== "string") {
      throw new Error("display.registerDriver(name, factory) expects a driver name");
    }
    if (typeof factory !== "function") {
      throw new Error("display.registerDriver(name, factory) expects a factory function");
    }
    drivers[name] = factory;
    return display;
  };
  display.create = function (options) {
    var settings = options || {};
    var driverName;
    var factory;

    if (!own(settings, "driver") || typeof settings.driver !== "string" || settings.driver.length === 0) {
      throw new Error("display.create(options) requires options.driver");
    }
    driverName = settings.driver;
    factory = drivers[driverName];
    if (!factory) {
      throw new Error("display driver not registered: " + driverName);
    }
    return factory(settings);
  };
  display.open = function (options) {
    return display.create(options).init();
  };
  display.__loaded = false;

  global.display = display;
  global.__displaySystem = system;
})(globalThis);
