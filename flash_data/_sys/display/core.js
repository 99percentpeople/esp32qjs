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

  function assertNumber(value, apiName) {
    if (typeof value !== "number" || value !== value) {
      throw new Error(apiName + " expects a number");
    }
    return value;
  }

  function mono1(value) {
    return clampInt(assertNumber(value, "display.mono1(value)") | 0, 0, 1);
  }

  function gray4(value) {
    return clampInt(assertNumber(value, "display.gray4(value)") | 0, 0, 15);
  }

  function gray8(value) {
    return clampInt(assertNumber(value, "display.gray8(value)") | 0, 0, 255);
  }

  function rgb565(red, green, blue) {
    red = clampInt(assertNumber(red, "display.rgb565(red, green, blue)") | 0, 0, 255);
    green = clampInt(assertNumber(green, "display.rgb565(red, green, blue)") | 0, 0, 255);
    blue = clampInt(assertNumber(blue, "display.rgb565(red, green, blue)") | 0, 0, 255);
    return ((red & 0xf8) << 8) | ((green & 0xfc) << 3) | (blue >> 3);
  }

  function normalizeMonoColor(value, fallback, apiName) {
    var color;

    if (value === undefined || value === null) {
      return fallback;
    }
    if (typeof value !== "number" || value !== value) {
      throw new Error(apiName + " expects a display.mono1(...) color");
    }
    color = value | 0;
    if (color < 0 || color > 1) {
      throw new Error(apiName + " expects a display.mono1(...) color");
    }
    return color;
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

  function backgroundFromStyle(style) {
    if (style && typeof style === "object" && own(style, "background") && style.background !== null) {
      return {
        hasColor: true,
        color: normalizeMonoColor(style.background, 0, "text background")
      };
    }
    return {
      hasColor: false,
      color: 0
    };
  }

  function styleOptions(value, apiName) {
    if (value === undefined || value === null) {
      return {};
    }
    if (typeof value !== "object") {
      throw new Error(apiName + " expects an options object");
    }
    return value;
  }

  function flatPointList(points, apiName) {
    var out = [];
    var length;
    var item;
    var x;
    var y;
    var i;

    if (!points || typeof points.length !== "number") {
      throw new Error(apiName + " expects a point array");
    }
    length = points.length | 0;
    if (length <= 0) {
      return out;
    }
    if (typeof points[0] === "number") {
      if ((length & 1) !== 0) {
        throw new Error(apiName + " expects an even-length flat point array");
      }
      for (i = 0; i < length; i += 2) {
        out.push(points[i] | 0, points[i + 1] | 0);
      }
      return out;
    }
    for (i = 0; i < length; i += 1) {
      item = points[i];
      if (!item || typeof item !== "object") {
        throw new Error(apiName + " expects points shaped as [x, y] or { x, y }");
      }
      if (typeof item.length === "number") {
        x = item[0];
        y = item[1];
      } else {
        x = item.x;
        y = item.y;
      }
      out.push(x | 0, y | 0);
    }
    return out;
  }

  function sortFirst(values, count) {
    var i;
    var j;
    var value;

    for (i = 1; i < count; i += 1) {
      value = values[i];
      j = i - 1;
      while (j >= 0 && values[j] > value) {
        values[j + 1] = values[j];
        j -= 1;
      }
      values[j + 1] = value;
    }
  }

  function curveSegments(options, fallback) {
    var style = styleOptions(options, "curve options");
    var segments = own(style, "segments") ? style.segments | 0 : fallback;

    if (segments < 2) {
      segments = 2;
    }
    if (segments > 128) {
      segments = 128;
    }
    return segments;
  }

  function ellipseSegments(rx, ry, options) {
    var longest = Math.max(Math.abs(rx | 0), Math.abs(ry | 0));
    var fallback = longest * 2;

    if (fallback < 16) {
      fallback = 16;
    }
    if (fallback > 96) {
      fallback = 96;
    }
    return curveSegments(options, fallback);
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

  Surface.prototype.drawCircle = function (cx, cy, radius, color) {
    var r = Math.abs(radius | 0);
    var x = r;
    var y = 0;
    var err = 1 - x;

    cx = cx | 0;
    cy = cy | 0;
    if (r === 0) {
      return this.setPixel(cx, cy, color);
    }
    while (x >= y) {
      this.setPixel(cx + x, cy + y, color);
      this.setPixel(cx + y, cy + x, color);
      this.setPixel(cx - y, cy + x, color);
      this.setPixel(cx - x, cy + y, color);
      this.setPixel(cx - x, cy - y, color);
      this.setPixel(cx - y, cy - x, color);
      this.setPixel(cx + y, cy - x, color);
      this.setPixel(cx + x, cy - y, color);
      y += 1;
      if (err < 0) {
        err += (y << 1) + 1;
      } else {
        x -= 1;
        err += ((y - x) << 1) + 1;
      }
    }
    return this;
  };

  Surface.prototype.fillCircle = function (cx, cy, radius, color) {
    var r = Math.abs(radius | 0);
    var rr = r * r;
    var y;
    var x;

    cx = cx | 0;
    cy = cy | 0;
    for (y = -r; y <= r; y += 1) {
      x = Math.sqrt(rr - y * y) | 0;
      this.fillRect(cx - x, cy + y, x * 2 + 1, 1, color);
    }
    return this;
  };

  Surface.prototype.drawEllipse = function (cx, cy, rx, ry, color, options) {
    var segments = ellipseSegments(rx, ry, options);
    var prevX;
    var prevY;
    var x;
    var y;
    var angle;
    var i;

    cx = cx | 0;
    cy = cy | 0;
    rx = Math.abs(rx | 0);
    ry = Math.abs(ry | 0);
    if (rx === 0 && ry === 0) {
      return this.setPixel(cx, cy, color);
    }
    if (rx === 0) {
      return this.drawLine(cx, cy - ry, cx, cy + ry, color);
    }
    if (ry === 0) {
      return this.drawLine(cx - rx, cy, cx + rx, cy, color);
    }
    prevX = cx + rx;
    prevY = cy;
    for (i = 1; i <= segments; i += 1) {
      angle = Math.PI * 2 * i / segments;
      x = cx + Math.round(Math.cos(angle) * rx);
      y = cy + Math.round(Math.sin(angle) * ry);
      this.drawLine(prevX, prevY, x, y, color);
      prevX = x;
      prevY = y;
    }
    return this;
  };

  Surface.prototype.fillEllipse = function (cx, cy, rx, ry, color) {
    var y;
    var span;
    var ratio;

    cx = cx | 0;
    cy = cy | 0;
    rx = Math.abs(rx | 0);
    ry = Math.abs(ry | 0);
    if (rx === 0 && ry === 0) {
      return this.setPixel(cx, cy, color);
    }
    if (rx === 0) {
      return this.drawLine(cx, cy - ry, cx, cy + ry, color);
    }
    if (ry === 0) {
      return this.drawLine(cx - rx, cy, cx + rx, cy, color);
    }
    for (y = -ry; y <= ry; y += 1) {
      ratio = y / ry;
      span = Math.sqrt(1 - ratio * ratio) * rx | 0;
      this.fillRect(cx - span, cy + y, span * 2 + 1, 1, color);
    }
    return this;
  };

  Surface.prototype.drawRoundRect = function (x, y, width, height, radius, color) {
    var r;
    var cx0;
    var cx1;
    var cy0;
    var cy1;
    var px;
    var py;
    var err;

    x = x | 0;
    y = y | 0;
    width = width | 0;
    height = height | 0;
    if (width <= 0 || height <= 0) {
      return this;
    }
    r = Math.abs(radius | 0);
    r = Math.min(r, width >> 1, height >> 1);
    if (r <= 0) {
      return this.drawRect(x, y, width, height, color);
    }
    cx0 = x + r;
    cx1 = x + width - r - 1;
    cy0 = y + r;
    cy1 = y + height - r - 1;
    this.drawLine(cx0, y, cx1, y, color);
    this.drawLine(cx0, y + height - 1, cx1, y + height - 1, color);
    this.drawLine(x, cy0, x, cy1, color);
    this.drawLine(x + width - 1, cy0, x + width - 1, cy1, color);
    px = r;
    py = 0;
    err = 1 - px;
    while (px >= py) {
      this.setPixel(cx0 - px, cy0 - py, color);
      this.setPixel(cx0 - py, cy0 - px, color);
      this.setPixel(cx1 + px, cy0 - py, color);
      this.setPixel(cx1 + py, cy0 - px, color);
      this.setPixel(cx0 - px, cy1 + py, color);
      this.setPixel(cx0 - py, cy1 + px, color);
      this.setPixel(cx1 + px, cy1 + py, color);
      this.setPixel(cx1 + py, cy1 + px, color);
      py += 1;
      if (err < 0) {
        err += (py << 1) + 1;
      } else {
        px -= 1;
        err += ((py - px) << 1) + 1;
      }
    }
    return this;
  };

  Surface.prototype.fillRoundRect = function (x, y, width, height, radius, color) {
    var r;
    var dy;
    var span;
    var rowWidth;

    x = x | 0;
    y = y | 0;
    width = width | 0;
    height = height | 0;
    if (width <= 0 || height <= 0) {
      return this;
    }
    r = Math.abs(radius | 0);
    r = Math.min(r, width >> 1, height >> 1);
    if (r <= 0) {
      return this.fillRect(x, y, width, height, color);
    }
    this.fillRect(x, y + r, width, height - r * 2, color);
    for (dy = 0; dy < r; dy += 1) {
      span = Math.sqrt(r * r - (r - dy) * (r - dy)) | 0;
      rowWidth = width - r * 2 + span * 2;
      this.fillRect(x + r - span, y + dy, rowWidth, 1, color);
      this.fillRect(x + r - span, y + height - 1 - dy, rowWidth, 1, color);
    }
    return this;
  };

  Surface.prototype.drawPolyline = function (points, color) {
    var flat = flatPointList(points, "drawPolyline(points, color)");
    var i;

    for (i = 2; i < flat.length; i += 2) {
      this.drawLine(flat[i - 2], flat[i - 1], flat[i], flat[i + 1], color);
    }
    return this;
  };

  Surface.prototype.drawPolygon = function (points, color) {
    var flat = flatPointList(points, "drawPolygon(points, color)");
    var last = flat.length - 2;

    if (flat.length < 4) {
      return this;
    }
    this.drawPolyline(flat, color);
    return this.drawLine(flat[last], flat[last + 1], flat[0], flat[1], color);
  };

  Surface.prototype.fillPolygon = function (points, color) {
    var flat = flatPointList(points, "fillPolygon(points, color)");
    var count = flat.length >> 1;
    var intersections;
    var minY;
    var maxY;
    var scanY;
    var edge;
    var next;
    var x1;
    var y1;
    var x2;
    var y2;
    var n;
    var i;
    var y;
    var xStart;
    var xEnd;

    if (count < 3) {
      return this;
    }
    minY = flat[1];
    maxY = flat[1];
    for (i = 3; i < flat.length; i += 2) {
      if (flat[i] < minY) {
        minY = flat[i];
      }
      if (flat[i] > maxY) {
        maxY = flat[i];
      }
    }
    intersections = new Array(count);
    for (y = minY; y <= maxY; y += 1) {
      scanY = y + 0.5;
      n = 0;
      for (edge = 0; edge < count; edge += 1) {
        next = edge === count - 1 ? 0 : edge + 1;
        x1 = flat[edge * 2];
        y1 = flat[edge * 2 + 1];
        x2 = flat[next * 2];
        y2 = flat[next * 2 + 1];
        if ((y1 <= scanY && y2 > scanY) || (y2 <= scanY && y1 > scanY)) {
          intersections[n] = x1 + ((scanY - y1) * (x2 - x1)) / (y2 - y1);
          n += 1;
        }
      }
      sortFirst(intersections, n);
      for (i = 0; i + 1 < n; i += 2) {
        xStart = Math.ceil(intersections[i]);
        xEnd = Math.floor(intersections[i + 1]);
        if (xEnd >= xStart) {
          this.fillRect(xStart, y, xEnd - xStart + 1, 1, color);
        }
      }
    }
    return this;
  };

  Surface.prototype.drawTriangle = function (x0, y0, x1, y1, x2, y2, color) {
    return this.drawPolygon([x0, y0, x1, y1, x2, y2], color);
  };

  Surface.prototype.fillTriangle = function (x0, y0, x1, y1, x2, y2, color) {
    return this.fillPolygon([x0, y0, x1, y1, x2, y2], color);
  };

  Surface.prototype.drawQuadraticBezier = function (x0, y0, cx, cy, x1, y1, color, options) {
    var segments = curveSegments(options, 24);
    var prevX = x0 | 0;
    var prevY = y0 | 0;
    var t;
    var mt;
    var x;
    var y;
    var i;

    for (i = 1; i <= segments; i += 1) {
      t = i / segments;
      mt = 1 - t;
      x = Math.round(mt * mt * x0 + 2 * mt * t * cx + t * t * x1);
      y = Math.round(mt * mt * y0 + 2 * mt * t * cy + t * t * y1);
      this.drawLine(prevX, prevY, x, y, color);
      prevX = x;
      prevY = y;
    }
    return this;
  };

  Surface.prototype.drawCubicBezier = function (x0, y0, c1x, c1y, c2x, c2y, x1, y1, color, options) {
    var segments = curveSegments(options, 32);
    var prevX = x0 | 0;
    var prevY = y0 | 0;
    var t;
    var mt;
    var x;
    var y;
    var i;

    for (i = 1; i <= segments; i += 1) {
      t = i / segments;
      mt = 1 - t;
      x = Math.round(
        mt * mt * mt * x0 +
        3 * mt * mt * t * c1x +
        3 * mt * t * t * c2x +
        t * t * t * x1
      );
      y = Math.round(
        mt * mt * mt * y0 +
        3 * mt * mt * t * c1y +
        3 * mt * t * t * c2y +
        t * t * t * y1
      );
      this.drawLine(prevX, prevY, x, y, color);
      prevX = x;
      prevY = y;
    }
    return this;
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
    this.foreground = normalizeMonoColor(options.foreground, mono1(1), "MonoSurface foreground");
    this.background = normalizeMonoColor(options.background, mono1(0), "MonoSurface background");
    this.buffer = new Array(this.width * this.pages);
    this.clear();
  }

  inherit(MonoSurface, Surface);

  MonoSurface.prototype.clear = function (color) {
    var fillByte = normalizeMonoColor(color, this.background, "MonoSurface.clear(color)") ? 0xff : 0x00;
    var i;

    for (i = 0; i < this.buffer.length; i += 1) {
      this.buffer[i] = fillByte;
    }
    return this;
  };

  MonoSurface.prototype.fill = function (color) {
    return this.clear(color);
  };

  MonoSurface.prototype._indexFor = function (x, y) {
    return x + (this.width * (y >> 3));
  };

  MonoSurface.prototype._setPixelRaw = function (x, y, color) {
    var index;
    var mask;

    x = x | 0;
    y = y | 0;
    if (x < 0 || y < 0 || x >= this.width || y >= this.height) {
      return this;
    }

    index = this._indexFor(x, y);
    mask = 1 << (y & 7);
    if (color !== 0) {
      this.buffer[index] |= mask;
    } else {
      this.buffer[index] &= (0xff ^ mask);
    }
    return this;
  };

  MonoSurface.prototype.setPixel = function (x, y, color) {
    return this._setPixelRaw(x, y, normalizeMonoColor(color, this.foreground, "MonoSurface.setPixel(x, y, color)"));
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
    return (this.buffer[index] & mask) !== 0 ? mono1(1) : mono1(0);
  };

  MonoSurface.prototype.fillRect = function (x, y, width, height, color) {
    var pixel = normalizeMonoColor(color, this.foreground, "MonoSurface.fillRect(x, y, width, height, color)");
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
        this._setPixelRaw(xx, yy, pixel);
      }
    }
    return this;
  };

  MonoSurface.prototype.drawLine = function (x0, y0, x1, y1, color) {
    var pixel = normalizeMonoColor(color, this.foreground, "MonoSurface.drawLine(x0, y0, x1, y1, color)");
    var dx = Math.abs(x1 - x0);
    var sx = x0 < x1 ? 1 : -1;
    var dy = -Math.abs(y1 - y0);
    var sy = y0 < y1 ? 1 : -1;
    var err = dx + dy;
    var e2;

    while (true) {
      this._setPixelRaw(x0, y0, pixel);
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

  MonoSurface.prototype.drawRect = function (x, y, width, height, color) {
    var pixel = normalizeMonoColor(color, this.foreground, "MonoSurface.drawRect(x, y, width, height, color)");

    if ((width | 0) <= 0 || (height | 0) <= 0) {
      return this;
    }

    this.drawLine(x, y, x + width - 1, y, pixel);
    this.drawLine(x, y + height - 1, x + width - 1, y + height - 1, pixel);
    this.drawLine(x, y, x, y + height - 1, pixel);
    this.drawLine(x + width - 1, y, x + width - 1, y + height - 1, pixel);
    return this;
  };

  MonoSurface.prototype.drawBitmap = function (x, y, bitmap, options) {
    var style = styleOptions(options, "MonoSurface.drawBitmap(x, y, bitmap, options)");
    var pixels;
    var width;
    var height;
    var color = own(style, "color") ? style.color : undefined;
    var foreground = normalizeMonoColor(color, this.foreground, "MonoSurface.drawBitmap(x, y, bitmap, options).color");
    var background = backgroundFromStyle(style);
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
        if (pixels[index]) {
          this._setPixelRaw(x + col, y + row, foreground);
        } else if (background.hasColor) {
          this._setPixelRaw(x + col, y + row, background.color);
        }
        index += 1;
      }
    }
    return this;
  };

  MonoSurface.prototype.drawChar = function (x, y, ch, options) {
    var style = styleOptions(options, "MonoSurface.drawChar(x, y, ch, options)");
    var font = fontFromStyle(style);
    var encoded = encodeTextForFont(String(ch).charAt(0), font);
    var color = normalizeMonoColor(style.color, this.foreground, "MonoSurface.drawChar(x, y, ch, options).color");
    var background = backgroundFromStyle(style);
    var col;
    var row;
    var bits;

    if (background.hasColor) {
      this.fillRect(x, y, font.advance, font.lineHeight, background.color);
    }
    for (col = 0; col < font.width; col += 1) {
      for (row = 0; row < font.height; row += 1) {
        bits = glyphByte(font, encoded.charAt(0), col, row >> 3);
        if ((bits & (1 << (row & 7))) !== 0) {
          this._setPixelRaw(x + col, y + row, color);
        }
      }
    }
    return this;
  };

  MonoSurface.prototype.drawText = function (x, y, text, options) {
    var style = styleOptions(options, "MonoSurface.drawText(x, y, text, options)");
    var font = fontFromStyle(style);
    var str = encodeTextForFont(text, font);
    var cursorX = x | 0;
    var cursorY = y | 0;
    var gap = spacingFromStyle(style, this.spacing);
    var color = normalizeMonoColor(style.color, this.foreground, "MonoSurface.drawText(x, y, text, options).color");
    var background = backgroundFromStyle(style);
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
      if (background.hasColor) {
        this.fillRect(cursorX, cursorY, font.advance + gap, font.lineHeight, background.color);
      }
      for (col = 0; col < font.width; col += 1) {
        for (row = 0; row < font.height; row += 1) {
          bits = glyphByte(font, ch, col, row >> 3);
          if ((bits & (1 << (row & 7))) !== 0) {
            this._setPixelRaw(cursorX + col, cursorY + row, color);
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
    mono1: mono1,
    gray4: gray4,
    gray8: gray8,
    rgb565: rgb565,
    normalizeMonoColor: normalizeMonoColor,
    styleOptions: styleOptions,
    clampInt: clampInt
  };

  display.VERSION = "0.4.0";
  display.mono1 = display.mono1 || mono1;
  display.gray4 = display.gray4 || gray4;
  display.gray8 = display.gray8 || gray8;
  display.rgb565 = display.rgb565 || rgb565;
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
