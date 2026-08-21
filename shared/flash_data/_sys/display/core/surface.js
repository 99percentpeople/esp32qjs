(function (global) {
  var system = global.__displaySystemV2;
  var display = global.display;

  if (!system || system.surfaceLoaded) {
    return;
  }

  function own(value, key) {
    return system.own(value, key);
  }

  function requireBitmap() {
    if (!global.bitmap || typeof global.bitmap.create !== "function") {
      throw new Error("display.Surface requires the bitmap module");
    }
  }

  function requireOpen(surface, apiName) {
    if (surface.closed || !surface.bitmap) {
      throw new Error("cannot use a closed Surface in " + apiName);
    }
  }

  function normalizeDrawColor(surface, value, fallback, apiName) {
    return system.normalizeColor(surface.pixelFormat, value, fallback, apiName);
  }

  function textFont(style) {
    return system.fontFromStyle(style);
  }

  function nativeText(surface, text, style) {
    return system.encodeText(text, textFont(style));
  }

  function nativeTextOptions(surface, style) {
    var options = {};
    var font = textFont(style);

    if (style && typeof style === "object") {
      if (own(style, "spacing")) {
        options.spacing = style.spacing;
      }
      if (own(style, "background")) {
        options.background = style.background === null
          ? null
          : normalizeDrawColor(surface, style.background, surface.background,
              "Surface text background");
      }
    } else if (style !== undefined) {
      options.spacing = style;
    }
    options.font = font.native || font;
    if (!own(options, "spacing")) {
      options.spacing = surface.spacing;
    }
    return options;
  }

  function FrameSource(surface) {
    this.surface = surface;
    this.spanSource = null;
    this.spanKey = "";
    this.width = surface.width;
    this.height = surface.height;
    this.pixelFormat = surface.pixelFormat;
    this.layout = surface.layout;
    this.chunkBytes = surface.chunkBytes;
  }

  FrameSource.prototype.requireOpen = function (apiName) {
    requireOpen(this.surface, apiName);
    return this.surface.bitmap;
  };

  FrameSource.prototype.readRect = function (x, y, width, height, options) {
    return this.requireOpen("FrameSource.readRect()")
      .readRect(x, y, width, height, options);
  };

  FrameSource.prototype.readRectChunks = function (x, y, width, height, options) {
    return this.requireOpen("FrameSource.readRectChunks()")
      .readRectChunks(x, y, width, height, options);
  };

  FrameSource.prototype.getSpanSource = function (options) {
    var buffer = this.requireOpen("FrameSource.getSpanSource()");
    var settings = options || {};
    var key = String(settings.byteOrder || "") + ":" + String(settings.chunkBytes || "");

    if (typeof buffer.createSpanSource !== "function") {
      return null;
    }
    if (!this.spanSource || this.spanKey !== key) {
      this.spanSource = buffer.createSpanSource(settings);
      this.spanKey = key;
    }
    return this.spanSource;
  };

  FrameSource.prototype.close = function () {
    this.spanSource = null;
    this.spanKey = "";
    this.surface = null;
    return true;
  };

  function createBitmap(metadata, options, foreground, background) {
    var storage = own(options, "storage") ? options.storage : "auto";
    var config = {
      width: metadata.width,
      height: metadata.height,
      format: metadata.pixelFormat,
      storage: storage,
      foreground: foreground,
      background: background
    };

    if (metadata.layout) {
      config.layout = metadata.layout;
    }
    if (own(options, "chunkBytes")) {
      config.chunkBytes = options.chunkBytes;
    }
    try {
      return global.bitmap.create(config);
    } catch (error) {
      if (!own(options, "fallbackStorage") || options.fallbackStorage === storage) {
        throw error;
      }
      config.storage = options.fallbackStorage;
      return global.bitmap.create(config);
    }
  }

  function Surface(metadata, options) {
    var commandOptions;
    var foregroundDefault;
    var backgroundDefault;

    requireBitmap();
    system.assertObject(metadata, "new display.Surface(metadata, options)");
    options = system.assertKnownOptions(options || {}, [
      "storage",
      "fallbackStorage",
      "chunkBytes",
      "foreground",
      "background",
      "spacing",
      "commandBuffer"
    ], "new display.Surface(metadata, options)");

    this.width = metadata.width | 0;
    this.height = metadata.height | 0;
    this.pixelFormat = String(metadata.pixelFormat || "");
    this.layout = String(metadata.layout || (this.pixelFormat === "mono1" ? "page-y8" : "linear"));
    if (this.width <= 0 || this.height <= 0) {
      throw new RangeError("display.Surface dimensions must be positive");
    }
    system.normalizeColor(this.pixelFormat, 0, 0, "display.Surface pixelFormat");

    foregroundDefault = this.pixelFormat === "mono1" ? 1 : system.normalizeColor(
      this.pixelFormat,
      this.pixelFormat === "rgb565"
        ? 0xffff
        : (this.pixelFormat === "rgb888" ? 0xffffff : 0xff),
      1,
      "display.Surface foreground"
    );
    backgroundDefault = 0;
    this.foreground = system.normalizeColor(
      this.pixelFormat,
      options.foreground,
      foregroundDefault,
      "display.Surface foreground"
    );
    this.background = system.normalizeColor(
      this.pixelFormat,
      options.background,
      backgroundDefault,
      "display.Surface background"
    );
    this.spacing = own(options, "spacing") ? options.spacing | 0 : 0;
    this.chunkBytes = own(options, "chunkBytes") ? options.chunkBytes | 0 : 0;
    this.bitmap = createBitmap(metadata, options, this.foreground, this.background);
    this.frame = new FrameSource(this);
    this.commandBufferEnabled = options.commandBuffer !== false;
    commandOptions = options.commandBuffer && typeof options.commandBuffer === "object"
      ? options.commandBuffer
      : {};
    system.assertKnownOptions(commandOptions, ["commandCapacity", "textBytes"],
      "display.Surface commandBuffer options");
    this.commandBufferOptions = {
      commandCapacity: own(commandOptions, "commandCapacity")
        ? commandOptions.commandCapacity
        : 192,
      textBytes: own(commandOptions, "textBytes") ? commandOptions.textBytes : 2048
    };
    this.commandBuffer = null;
    this.commandBatch = null;
    this.closed = false;
    this.ready = true;
    this.clear();
  }

  Surface.prototype.clear = function (color) {
    requireOpen(this, "Surface.clear()");
    color = normalizeDrawColor(this, color, this.background, "Surface.clear(color)");
    this.bitmap.clear(color);
    return this;
  };

  Surface.prototype.fill = function (color) {
    return this.clear(color);
  };

  Surface.prototype.setPixel = function (x, y, color) {
    requireOpen(this, "Surface.setPixel()");
    color = normalizeDrawColor(this, color, this.foreground, "Surface.setPixel(x, y, color)");
    this.bitmap.setPixel(x, y, color);
    return this;
  };

  Surface.prototype.getPixel = function (x, y) {
    requireOpen(this, "Surface.getPixel()");
    return this.bitmap.getPixel(x, y);
  };

  Surface.prototype.fillRect = function (x, y, width, height, color) {
    requireOpen(this, "Surface.fillRect()");
    color = normalizeDrawColor(this, color, this.foreground,
      "Surface.fillRect(x, y, width, height, color)");
    this.bitmap.fillRect(x, y, width, height, color);
    return this;
  };

  Surface.prototype.drawCircle = function (cx, cy, radius, color) {
    requireOpen(this, "Surface.drawCircle()");
    color = normalizeDrawColor(this, color, this.foreground,
      "Surface.drawCircle(cx, cy, radius, color)");
    this.bitmap.drawCircle(cx, cy, radius, color);
    return this;
  };

  Surface.prototype.fillCircle = function (cx, cy, radius, color) {
    requireOpen(this, "Surface.fillCircle()");
    color = normalizeDrawColor(this, color, this.foreground,
      "Surface.fillCircle(cx, cy, radius, color)");
    this.bitmap.fillCircle(cx, cy, radius, color);
    return this;
  };

  Surface.prototype.drawEllipse = function (cx, cy, rx, ry, color, options) {
    requireOpen(this, "Surface.drawEllipse()");
    color = normalizeDrawColor(this, color, this.foreground,
      "Surface.drawEllipse(cx, cy, rx, ry, color)");
    this.bitmap.drawEllipse(cx, cy, rx, ry, color, options);
    return this;
  };

  Surface.prototype.fillEllipse = function (cx, cy, rx, ry, color) {
    requireOpen(this, "Surface.fillEllipse()");
    color = normalizeDrawColor(this, color, this.foreground,
      "Surface.fillEllipse(cx, cy, rx, ry, color)");
    this.bitmap.fillEllipse(cx, cy, rx, ry, color);
    return this;
  };

  Surface.prototype.drawLine = function (x0, y0, x1, y1, color) {
    requireOpen(this, "Surface.drawLine()");
    color = normalizeDrawColor(this, color, this.foreground,
      "Surface.drawLine(x0, y0, x1, y1, color)");
    this.bitmap.drawLine(x0, y0, x1, y1, color);
    return this;
  };

  Surface.prototype.drawRect = function (x, y, width, height, color) {
    requireOpen(this, "Surface.drawRect()");
    color = normalizeDrawColor(this, color, this.foreground,
      "Surface.drawRect(x, y, width, height, color)");
    this.bitmap.drawRect(x, y, width, height, color);
    return this;
  };

  Surface.prototype.drawRoundRect = function (x, y, width, height, radius, color) {
    requireOpen(this, "Surface.drawRoundRect()");
    color = normalizeDrawColor(this, color, this.foreground,
      "Surface.drawRoundRect(x, y, width, height, radius, color)");
    this.bitmap.drawRoundRect(x, y, width, height, radius, color);
    return this;
  };

  Surface.prototype.fillRoundRect = function (x, y, width, height, radius, color) {
    requireOpen(this, "Surface.fillRoundRect()");
    color = normalizeDrawColor(this, color, this.foreground,
      "Surface.fillRoundRect(x, y, width, height, radius, color)");
    this.bitmap.fillRoundRect(x, y, width, height, radius, color);
    return this;
  };

  Surface.prototype.drawPolyline = function (points, color) {
    requireOpen(this, "Surface.drawPolyline()");
    color = normalizeDrawColor(this, color, this.foreground,
      "Surface.drawPolyline(points, color)");
    this.bitmap.drawPolyline(points, color);
    return this;
  };

  Surface.prototype.drawPolygon = function (points, color) {
    requireOpen(this, "Surface.drawPolygon()");
    color = normalizeDrawColor(this, color, this.foreground,
      "Surface.drawPolygon(points, color)");
    this.bitmap.drawPolygon(points, color);
    return this;
  };

  Surface.prototype.fillPolygon = function (points, color) {
    requireOpen(this, "Surface.fillPolygon()");
    color = normalizeDrawColor(this, color, this.foreground,
      "Surface.fillPolygon(points, color)");
    this.bitmap.fillPolygon(points, color);
    return this;
  };

  Surface.prototype.drawTriangle = function (x0, y0, x1, y1, x2, y2, color) {
    requireOpen(this, "Surface.drawTriangle()");
    color = normalizeDrawColor(this, color, this.foreground,
      "Surface.drawTriangle(x0, y0, x1, y1, x2, y2, color)");
    this.bitmap.drawTriangle(x0, y0, x1, y1, x2, y2, color);
    return this;
  };

  Surface.prototype.fillTriangle = function (x0, y0, x1, y1, x2, y2, color) {
    requireOpen(this, "Surface.fillTriangle()");
    color = normalizeDrawColor(this, color, this.foreground,
      "Surface.fillTriangle(x0, y0, x1, y1, x2, y2, color)");
    this.bitmap.fillTriangle(x0, y0, x1, y1, x2, y2, color);
    return this;
  };

  Surface.prototype.drawQuadraticBezier = function (x0, y0, cx, cy, x1, y1, color, options) {
    requireOpen(this, "Surface.drawQuadraticBezier()");
    color = normalizeDrawColor(this, color, this.foreground,
      "Surface.drawQuadraticBezier(x0, y0, cx, cy, x1, y1, color)");
    this.bitmap.drawQuadraticBezier(x0, y0, cx, cy, x1, y1, color, options);
    return this;
  };

  Surface.prototype.drawCubicBezier = function (x0, y0, c1x, c1y, c2x, c2y, x1, y1, color, options) {
    requireOpen(this, "Surface.drawCubicBezier()");
    color = normalizeDrawColor(this, color, this.foreground,
      "Surface.drawCubicBezier(x0, y0, c1x, c1y, c2x, c2y, x1, y1, color)");
    this.bitmap.drawCubicBezier(x0, y0, c1x, c1y, c2x, c2y, x1, y1, color, options);
    return this;
  };

  Surface.prototype.drawMask = function (x, y, mask, options) {
    var style = system.styleOptions(options, "Surface.drawMask(x, y, mask, options)");
    var nativeOptions = {
      color: normalizeDrawColor(this, style.color, this.foreground,
        "Surface.drawMask(x, y, mask, options).color")
    };

    requireOpen(this, "Surface.drawMask()");
    if (own(style, "background")) {
      nativeOptions.background = style.background === null
        ? null
        : normalizeDrawColor(this, style.background, this.background,
            "Surface mask background");
    }
    this.bitmap.drawMask(x, y, mask, nativeOptions);
    return this;
  };

  Surface.prototype.blit = function (source, options) {
    requireOpen(this, "Surface.blit()");
    this.bitmap.blit(source, options);
    return this;
  };

  Surface.prototype.drawChar = function (x, y, ch, options) {
    return this.drawText(x, y, String(ch).charAt(0), options);
  };

  Surface.prototype.drawText = function (x, y, text, options) {
    var style = system.styleOptions(options, "Surface.drawText(x, y, text, options)");
    var nativeOptions;

    requireOpen(this, "Surface.drawText()");
    nativeOptions = nativeTextOptions(this, style);
    nativeOptions.color = normalizeDrawColor(this, style.color, this.foreground,
      "Surface.drawText(x, y, text, options).color");
    this.bitmap.drawText(x, y, nativeText(this, text, style), nativeOptions);
    return this;
  };

  Surface.prototype.measureText = function (text, style) {
    requireOpen(this, "Surface.measureText()");
    style = system.styleOptions(style, "Surface.measureText(text, style)");
    return this.bitmap.measureText(
      nativeText(this, text, style),
      nativeTextOptions(this, style)
    );
  };

  Surface.prototype.getDirty = function () {
    requireOpen(this, "Surface.getDirty()");
    return this.bitmap.getDirty();
  };

  Surface.prototype.clearDirty = function () {
    requireOpen(this, "Surface.clearDirty()");
    this.bitmap.clearDirty();
    return this;
  };

  Surface.prototype.markDirty = function (x, y, width, height) {
    requireOpen(this, "Surface.markDirty()");
    this.bitmap.markDirty(x, y, width, height);
    return this;
  };

  var CMD_CLEAR = 1;
  var CMD_FILL_RECT = 2;
  var CMD_DRAW_RECT = 3;
  var CMD_DRAW_LINE = 4;
  var CMD_DRAW_ROUND_RECT = 5;
  var CMD_FILL_ROUND_RECT = 6;
  var CMD_DRAW_TEXT = 7;
  var CMD_TEXT_HAS_BACKGROUND = 1;

  function pushU16(out, value) {
    value = value | 0;
    out.push(value & 0xff, (value >> 8) & 0xff);
  }

  function pushI16(out, value) {
    pushU16(out, value);
  }

  function pushU32(out, value) {
    value = value >>> 0;
    out.push(
      value & 0xff,
      (value >>> 8) & 0xff,
      (value >>> 16) & 0xff,
      (value >>> 24) & 0xff
    );
  }

  function pushRectCommand(out, op, x, y, width, height, color) {
    out.push(op);
    pushI16(out, x);
    pushI16(out, y);
    pushI16(out, width);
    pushI16(out, height);
    pushU32(out, color);
  }

  function pushRoundRectCommand(out, op, x, y, width, height, radius, color) {
    out.push(op);
    pushI16(out, x);
    pushI16(out, y);
    pushI16(out, width);
    pushI16(out, height);
    pushI16(out, radius);
    pushU32(out, color);
  }

  function clampTextSpacing(value) {
    return system.clampInt(value, 0, 32);
  }

  function SurfaceCommandBatch(surface, nativeBatch) {
    this.surface = surface;
    this.nativeBatch = nativeBatch;
    this.pixelFormat = surface.pixelFormat;
    this.foreground = surface.foreground;
    this.background = surface.background;
    this.spacing = surface.spacing;
    this.bytes = [];
    this.text = "";
    this.textFont = null;
  }

  SurfaceCommandBatch.prototype.reset = function (nativeBatch) {
    this.nativeBatch = nativeBatch || this.nativeBatch;
    this.bytes.length = 0;
    this.text = "";
    this.textFont = null;
    return this;
  };

  SurfaceCommandBatch.prototype.flush = function () {
    var options;

    if (this.bytes.length <= 0) {
      return this;
    }
    if (this.text.length > 0) {
      options = { text: this.text, font: this.textFont };
      this.nativeBatch.appendPacked(this.bytes, options);
    } else {
      this.nativeBatch.appendPacked(this.bytes);
    }
    this.bytes.length = 0;
    this.text = "";
    this.textFont = null;
    return this;
  };

  SurfaceCommandBatch.prototype.clear = function (color) {
    color = normalizeDrawColor(this.surface, color, this.background,
      "Surface batch clear(color)");
    this.bytes.push(CMD_CLEAR);
    pushU32(this.bytes, color);
    return this;
  };

  SurfaceCommandBatch.prototype.fill = function (color) {
    return this.clear(color);
  };

  SurfaceCommandBatch.prototype.fillRect = function (x, y, width, height, color) {
    color = normalizeDrawColor(this.surface, color, this.foreground,
      "Surface batch fillRect(x, y, width, height, color)");
    if ((width | 0) > 0 && (height | 0) > 0) {
      pushRectCommand(this.bytes, CMD_FILL_RECT, x, y, width, height, color);
    }
    return this;
  };

  SurfaceCommandBatch.prototype.drawLine = function (x0, y0, x1, y1, color) {
    color = normalizeDrawColor(this.surface, color, this.foreground,
      "Surface batch drawLine(x0, y0, x1, y1, color)");
    pushRectCommand(this.bytes, CMD_DRAW_LINE, x0, y0, x1, y1, color);
    return this;
  };

  SurfaceCommandBatch.prototype.drawRect = function (x, y, width, height, color) {
    color = normalizeDrawColor(this.surface, color, this.foreground,
      "Surface batch drawRect(x, y, width, height, color)");
    if ((width | 0) > 0 && (height | 0) > 0) {
      pushRectCommand(this.bytes, CMD_DRAW_RECT, x, y, width, height, color);
    }
    return this;
  };

  SurfaceCommandBatch.prototype.drawRoundRect = function (x, y, width, height, radius, color) {
    color = normalizeDrawColor(this.surface, color, this.foreground,
      "Surface batch drawRoundRect(x, y, width, height, radius, color)");
    if ((width | 0) > 0 && (height | 0) > 0) {
      pushRoundRectCommand(this.bytes, CMD_DRAW_ROUND_RECT, x, y, width, height, radius, color);
    }
    return this;
  };

  SurfaceCommandBatch.prototype.fillRoundRect = function (x, y, width, height, radius, color) {
    color = normalizeDrawColor(this.surface, color, this.foreground,
      "Surface batch fillRoundRect(x, y, width, height, radius, color)");
    if ((width | 0) > 0 && (height | 0) > 0) {
      pushRoundRectCommand(this.bytes, CMD_FILL_ROUND_RECT, x, y, width, height, radius, color);
    }
    return this;
  };

  SurfaceCommandBatch.prototype.drawChar = function (x, y, ch, options) {
    return this.drawText(x, y, String(ch).charAt(0), options);
  };

  SurfaceCommandBatch.prototype.drawText = function (x, y, text, options) {
    var style = system.styleOptions(options, "Surface batch drawText(x, y, text, options)");
    var color = normalizeDrawColor(this.surface, style.color, this.foreground,
      "Surface batch drawText(x, y, text, options).color");
    var textOptions = nativeTextOptions(this.surface, style);
    var encoded = nativeText(this.surface, text, style);
    var hasBackground = textOptions.background !== undefined && textOptions.background !== null;
    var background = hasBackground ? textOptions.background : this.background;
    var font = textOptions.font;
    var textOffset;
    var textLength = encoded.length | 0;

    if (!font) {
      throw new Error("Surface batch drawText() requires a native font");
    }
    if (textLength <= 0) {
      return this;
    }
    if (textLength > 0xffff) {
      throw new RangeError("Surface batch drawText() text is too long");
    }
    if (this.textFont && this.textFont !== font) {
      this.flush();
    }
    if (!this.textFont) {
      this.textFont = font;
    }
    if (this.text.length + textLength > 0xffff) {
      this.flush();
      this.textFont = font;
    }
    textOffset = this.text.length;
    this.text += encoded;
    this.bytes.push(CMD_DRAW_TEXT);
    pushI16(this.bytes, x);
    pushI16(this.bytes, y);
    pushU32(this.bytes, color);
    pushU32(this.bytes, background);
    pushU16(this.bytes, hasBackground ? CMD_TEXT_HAS_BACKGROUND : 0);
    pushI16(this.bytes, clampTextSpacing(textOptions.spacing));
    pushU16(this.bytes, textOffset);
    pushU16(this.bytes, textLength);
    return this;
  };

  SurfaceCommandBatch.prototype.measureText = function (text, style) {
    return this.surface.measureText(text, style);
  };

  SurfaceCommandBatch.prototype.stats = function () {
    this.flush();
    return this.nativeBatch.stats();
  };

  Surface.prototype.beginBatch = function () {
    requireOpen(this, "Surface.beginBatch()");
    if (!this.commandBufferEnabled ||
        typeof this.bitmap.createCommandBuffer !== "function") {
      return null;
    }
    if (!this.commandBuffer) {
      this.commandBuffer = this.bitmap.createCommandBuffer(this.commandBufferOptions);
    } else {
      this.commandBuffer.reset();
    }
    if (!this.commandBatch) {
      this.commandBatch = new SurfaceCommandBatch(this, this.commandBuffer);
    }
    return this.commandBatch.reset(this.commandBuffer);
  };

  Surface.prototype.endBatch = function (batch) {
    var nativeBatch = batch && batch.nativeBatch ? batch.nativeBatch : batch;

    requireOpen(this, "Surface.endBatch()");
    if (batch && typeof batch.flush === "function") {
      batch.flush();
    }
    if (nativeBatch && typeof nativeBatch.replay === "function") {
      nativeBatch.replay(this.bitmap);
    }
    return this;
  };

  Surface.prototype.close = function () {
    var firstError = null;

    if (this.closed) {
      return true;
    }
    this.closed = true;
    this.ready = false;
    if (this.commandBuffer) {
      try {
        this.commandBuffer.close();
      } catch (error) {
        firstError = error;
      }
    }
    this.commandBuffer = null;
    this.commandBatch = null;
    if (this.frame) {
      this.frame.close();
      this.frame = null;
    }
    if (this.bitmap) {
      try {
        this.bitmap.close();
      } catch (error2) {
        if (!firstError) {
          firstError = error2;
        }
      }
    }
    this.bitmap = null;
    if (firstError) {
      throw firstError;
    }
    return true;
  };

  display.Surface = /** @type {typeof ESP32QJS.Surface} */ (
    /** @type {unknown} */ (Surface)
  );
  system.FrameSource = FrameSource;
  system.SurfaceCommandBatch = SurfaceCommandBatch;
  system.surfaceLoaded = true;
})(globalThis);
