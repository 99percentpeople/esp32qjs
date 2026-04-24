(function (global) {
  var owns = Object.prototype.hasOwnProperty;
  var DEFAULT_FONT_PATH = "_sys/display/fonts/mono5x7.eqf";
  var DEFAULT_FONT_NAME = "mono5x7";
  var system = global.__displaySystem;
  var drivers;
  var display;
  var defaultFont;
  var fontSetCache = {};

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

  function byteAt(data, index) {
    return data.charCodeAt(index) & 0xff;
  }

  function readU32LE(data, index) {
    return byteAt(data, index) |
      (byteAt(data, index + 1) << 8) |
      (byteAt(data, index + 2) << 16) |
      (byteAt(data, index + 3) << 24);
  }

  function basename(path) {
    var slash = String(path).lastIndexOf("/");

    return slash < 0 ? String(path) : String(path).slice(slash + 1);
  }

  function dirname(path) {
    var slash = String(path).lastIndexOf("/");

    return slash < 0 ? "" : String(path).slice(0, slash);
  }

  function joinPath(base, path) {
    path = String(path);
    if (path.charAt(0) === "/" || base === "") {
      return path;
    }
    return base + "/" + path;
  }

  function nextChar(str, index) {
    var first = str.charCodeAt(index);
    var second;

    if (first >= 0xd800 && first <= 0xdbff && index + 1 < str.length) {
      second = str.charCodeAt(index + 1);
      if (second >= 0xdc00 && second <= 0xdfff) {
        return {
          ch: str.charAt(index) + str.charAt(index + 1),
          next: index + 2
        };
      }
    }
    return {
      ch: str.charAt(index),
      next: index + 1
    };
  }

  function uniqueChars(text) {
    var str = String(text);
    var chars = [];
    var seen = {};
    var item;
    var i = 0;

    while (i < str.length) {
      item = nextChar(str, i);
      i = item.next;
      if (!own(seen, item.ch)) {
        seen[item.ch] = true;
        chars.push(item.ch);
      }
    }
    return chars;
  }

  function makeCharMap(chars, slotFirst) {
    var map = {};
    var i;

    for (i = 0; i < chars.length; i += 1) {
      map[chars[i]] = slotFirst + i;
    }
    return map;
  }

  function encodeTextForFont(text, font) {
    var str = String(text);
    var out = "";
    var item;
    var code;
    var i = 0;

    if (!font || !font.map) {
      return str;
    }
    while (i < str.length) {
      item = nextChar(str, i);
      i = item.next;
      if (item.ch === "\n") {
        out += "\n";
        continue;
      }
      if (own(font.map, item.ch)) {
        code = font.map[item.ch];
      } else {
        code = font.fallbackCode;
      }
      out += String.fromCharCode(code);
    }
    return out;
  }

  function parseEqfFont(data, name, nativeFont) {
    var first;
    var last;
    var width;
    var height;
    var bytesPerColumn;
    var glyphLength;

    if (typeof data !== "string" || data.length < 16 ||
        data.charAt(0) !== "E" || data.charAt(1) !== "Q" ||
        data.charAt(2) !== "F" || data.charAt(3) !== "1" ||
        byteAt(data, 4) !== 1 || byteAt(data, 5) !== 0) {
      throw new Error("invalid EQF1 font: " + name);
    }

    first = byteAt(data, 6);
    last = byteAt(data, 7);
    width = byteAt(data, 8);
    height = byteAt(data, 9);
    bytesPerColumn = (height + 7) >> 3;
    glyphLength = readU32LE(data, 12);
    if (last < first || width <= 0 || height <= 0 ||
        data.length < 16 + glyphLength ||
        glyphLength !== (last - first + 1) * width * bytesPerColumn) {
      throw new Error("invalid EQF1 metrics: " + name);
    }

    return {
      name: name,
      first: first,
      last: last,
      width: width,
      height: height,
      advance: byteAt(data, 10),
      lineHeight: byteAt(data, 11),
      bytesPerColumn: bytesPerColumn,
      glyphData: data,
      glyphOffset: 16,
      native: nativeFont || null
    };
  }

  function wrapNativeFont(nativeFont, name) {
    return {
      name: name || nativeFont.name || "font",
      width: nativeFont.width,
      height: nativeFont.height,
      advance: nativeFont.advance,
      lineHeight: nativeFont.lineHeight,
      native: nativeFont
    };
  }

  function resolveFont(font) {
    if (!font) {
      font = defaultFont;
    }
    if (font && (font.glyphData || font.native)) {
      return font;
    }
    throw new Error("display font is not loaded");
  }

  function fontFromStyle(style) {
    if (style && typeof style === "object" && own(style, "font")) {
      return resolveFont(style.font);
    }
    return resolveFont(defaultFont);
  }

  function normalizeCode(font, ch) {
    var code;

    if (!ch || ch.length === 0) {
      code = 32;
    } else {
      code = ch.charCodeAt(0) & 0xff;
    }
    if (code >= 97 && code <= 122) {
      code -= 32;
    }
    if (code < font.first || code > font.last) {
      code = "?".charCodeAt(0);
    }
    if (code < font.first || code > font.last) {
      code = font.first;
    }
    return code;
  }

  function glyphByte(font, ch, col, rowByte) {
    var code = normalizeCode(font, ch);
    var glyphIndex = code - font.first;
    var offset = font.glyphOffset + ((glyphIndex * font.width + col) * font.bytesPerColumn) + rowByte;

    if (!font.glyphData) {
      throw new Error("display font glyph data is not available in JavaScript");
    }
    return byteAt(font.glyphData, offset);
  }

  function measureText(text, style) {
    var font = fontFromStyle(style);
    var str = encodeTextForFont(text, font);
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
      lineWidth += font.advance + spacing;
    }

    if (lineWidth > maxWidth) {
      maxWidth = lineWidth;
    }
    if (maxWidth > 0) {
      maxWidth -= spacing;
    }

    return {
      width: maxWidth,
      height: lines * font.lineHeight,
      lines: lines
    };
  }

  function spacingFromStyle(style, fallback) {
    if (style && typeof style === "object") {
      return own(style, "spacing") ? style.spacing : fallback;
    }
    return style === undefined ? fallback : style;
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
    var font = resolveFont(defaultFont);
    var encoded = encodeTextForFont(String(ch).charAt(0), font);
    var col;
    var row;
    var bits;

    this.fillRect(x, y, font.advance, font.lineHeight, false);
    for (col = 0; col < font.width; col += 1) {
      for (row = 0; row < font.height; row += 1) {
        bits = glyphByte(font, encoded.charAt(0), col, row >> 3);
        if ((bits & (1 << (row & 7))) !== 0) {
          this.setPixel(x + col, y + row, enabled !== false);
        }
      }
    }
    return this;
  };

  MonoSurface.prototype.drawText = function (x, y, text, enabled, spacing) {
    var font = fontFromStyle(spacing);
    var str = encodeTextForFont(text, font);
    var cursorX = x | 0;
    var cursorY = y | 0;
    var gap = spacingFromStyle(spacing, this.spacing);
    var step = font.advance + gap;
    var i;
    var ch;
    var col;
    var row;
    var bits;

    for (i = 0; i < str.length; i += 1) {
      ch = str.charAt(i);
      if (ch === "\n") {
        cursorX = x | 0;
        cursorY += font.lineHeight;
        continue;
      }
      this.fillRect(cursorX, cursorY, font.advance, font.lineHeight, false);
      for (col = 0; col < font.width; col += 1) {
        for (row = 0; row < font.height; row += 1) {
          bits = glyphByte(font, ch, col, row >> 3);
          if ((bits & (1 << (row & 7))) !== 0) {
            this.setPixel(cursorX + col, cursorY + row, enabled !== false);
          }
        }
      }
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
    clampInt: clampInt
  };

  display.VERSION = "0.3.0";
  display.fonts = display.fonts || {};
  display.Surface = Surface;
  display.MonoSurface = MonoSurface;
  display.measureText = measureText;
  display.encodeText = encodeTextForFont;
  display.fontNeedsTextMapping = function (font) {
    return !!(font && font.map);
  };
  display.registerFont = function (name, font) {
    if (!name || typeof name !== "string") {
      throw new Error("display.registerFont(name, font) expects a font name");
    }
    if (!font) {
      throw new Error("display.registerFont(name, font) expects a font");
    }
    display.fonts[name] = font;
    return font;
  };
  display.loadFont = function (path, name) {
    var nativeFont = null;
    var font;
    var fontName = name === undefined || name === null ? basename(path) : String(name);

    if (global.displayBuffer && typeof global.displayBuffer.loadFont === "function") {
      nativeFont = global.displayBuffer.loadFont(path);
      font = wrapNativeFont(nativeFont, fontName);
      return display.registerFont(fontName, font);
    }
    if (!global.fs || typeof global.fs.readText !== "function") {
      throw new Error("display.loadFont(path) requires fs.readText()");
    }
    font = parseEqfFont(global.fs.readText(path), fontName, nativeFont);
    return display.registerFont(fontName, font);
  };
  display.loadFontSet = function (path) {
    var manifest;
    var chars;
    var slotFirst;
    var slotLast;
    var map;
    var fallback;
    var fontSet;
    var dir;

    path = String(path);
    if (fontSetCache[path]) {
      return fontSetCache[path];
    }
    if (!global.fs || typeof global.fs.readText !== "function") {
      throw new Error("display.loadFontSet(path) requires fs.readText()");
    }
    manifest = JSON.parse(global.fs.readText(path));
    if (!manifest || manifest.format !== "eqf1-map" ||
        typeof manifest.chars !== "string" ||
        !manifest.sizes || typeof manifest.sizes !== "object") {
      throw new Error("display.loadFontSet(path) expects an eqf1-map manifest");
    }
    chars = uniqueChars(manifest.chars);
    slotFirst = own(manifest, "slotFirst") ? manifest.slotFirst | 0 : 32;
    slotLast = slotFirst + chars.length - 1;
    if (slotFirst < 0 || slotLast > 255) {
      throw new Error("display font map slot range is outside EQF1");
    }
    map = makeCharMap(chars, slotFirst);
    fallback = typeof manifest.fallback === "string" && own(map, manifest.fallback)
      ? map[manifest.fallback]
      : (own(map, "?") ? map["?"] : slotFirst);
    dir = dirname(path);
    fontSet = {
      name: manifest.name || basename(path),
      path: path,
      dir: dir,
      chars: chars.join(""),
      slotFirst: slotFirst,
      slotLast: slotLast,
      map: map,
      fallbackCode: fallback,
      sizes: manifest.sizes,
      loaded: {},
      load: function (size, name) {
        var key;
        var entry;
        var fontName;
        var font;

        if (size === undefined || size === null) {
          size = manifest.defaultSize || Object.keys(manifest.sizes)[0];
        }
        key = String(size);
        entry = manifest.sizes[key];
        if (!entry || typeof entry.path !== "string") {
          throw new Error("display font set has no size: " + key);
        }
        if (name === undefined && this.loaded[key]) {
          return this.loaded[key];
        }
        fontName = name || (this.name + "-" + key);
        font = display.loadFont(joinPath(this.dir, entry.path), fontName);
        font.map = this.map;
        font.fallbackCode = this.fallbackCode;
        font.slotFirst = this.slotFirst;
        font.slotLast = this.slotLast;
        font.fontSet = this;
        font.encodeText = function (text) {
          return encodeTextForFont(text, font);
        };
        if (name === undefined) {
          this.loaded[key] = font;
        }
        return font;
      }
    };
    fontSetCache[path] = fontSet;
    return fontSet;
  };
  display.loadMappedFont = function (path, size, name) {
    return display.loadFontSet(path).load(size, name);
  };
  defaultFont = display.loadFont(DEFAULT_FONT_PATH, DEFAULT_FONT_NAME);
  display.defaultFont = defaultFont;
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
