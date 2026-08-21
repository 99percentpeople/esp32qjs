(function (global) {
  var system = global.__displaySystemV2;
  var display = global.display;
  var DEFAULT_FONT_PATH = "_sys/display/fonts/mono5x7.eqf";
  var DEFAULT_FONT_NAME = "mono5x7";
  var fontSetCache = {};
  var defaultFont;

  if (!system || system.fontsLoaded) {
    return;
  }

  function own(value, key) {
    return system.own(value, key);
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

  function encodeTextForFont(text, font) {
    var str = String(text);
    var out = "";
    var item;
    var code;
    var i = 0;

    font = resolveFont(font);
    if (!font.map) {
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
      } else {
        lineWidth += font.advance + spacing;
      }
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

  display.fonts = {};
  display.registerFont = function (name, font) {
    if (typeof name !== "string" || name.length === 0) {
      throw new TypeError("display.registerFont(name, font) expects a font name");
    }
    if (!font) {
      throw new TypeError("display.registerFont(name, font) expects a font");
    }
    display.fonts[name] = font;
    return font;
  };

  display.loadFont = function (path, name) {
    var nativeFont = null;
    var font;
    var fontName = name === undefined || name === null ? basename(path) : String(name);

    if (global.bitmap && typeof global.bitmap.loadFont === "function") {
      nativeFont = global.bitmap.loadFont(path);
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
      load: function (size, fontName) {
        var key;
        var entry;
        var resolvedName;
        var font;

        if (size === undefined || size === null) {
          size = manifest.defaultSize || Object.keys(manifest.sizes)[0];
        }
        key = String(size);
        entry = manifest.sizes[key];
        if (!entry || typeof entry.path !== "string") {
          throw new Error("display font set has no size: " + key);
        }
        if (fontName === undefined && this.loaded[key]) {
          return this.loaded[key];
        }
        resolvedName = fontName || (this.name + "-" + key);
        font = display.loadFont(joinPath(this.dir, entry.path), resolvedName);
        font.map = this.map;
        font.fallbackCode = this.fallbackCode;
        font.slotFirst = this.slotFirst;
        font.slotLast = this.slotLast;
        font.fontSet = this;
        font.encodeText = function (text) {
          return encodeTextForFont(text, font);
        };
        if (fontName === undefined) {
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
  display.measureText = measureText;
  display.encodeText = encodeTextForFont;
  display.fontNeedsTextMapping = function (font) {
    return !!(font && font.map);
  };

  system.resolveFont = resolveFont;
  system.fontFromStyle = fontFromStyle;
  system.encodeText = encodeTextForFont;
  system.measureText = measureText;
  system.fontsLoaded = true;
})(globalThis);
