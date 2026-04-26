(function (global) {
  var system = global.__displaySystem;
  var display = global.display;
  var DEFAULT_WIDTH = 240;
  var DEFAULT_HEIGHT = 240;
  var DEFAULT_FREQ_HZ = 20000000;
  var DEFAULT_NATIVE_MAX_TRANSFER_SIZE = 16384;

  if (!system || system.st7789Loaded) {
    return;
  }

  function own(obj, key) {
    return system.own(obj, key);
  }

  function toBool(value, fallback) {
    return system.toBool(value, fallback);
  }

  var rgb565 = display.rgb565;

  function normalizeColor(value, fallback, apiName) {
    if (value === undefined || value === null) {
      return fallback & 0xffff;
    }
    if (typeof value === "boolean") {
      throw new Error(apiName + " expects a display.rgb565(...) color");
    }
    if (typeof value === "number") {
      return value & 0xffff;
    }
    if (value && typeof value === "object") {
      return rgb565(value.r || 0, value.g || 0, value.b || 0);
    }
    throw new Error(apiName + " expects a display.rgb565(...) color");
  }

  function nativeDrawColor(surface, value, fallback, apiName) {
    if (value === undefined) {
      return fallback;
    }
    return normalizeColor(value, fallback, apiName);
  }

  function styleOptions(value, apiName) {
    return system.styleOptions(value, apiName);
  }

  function textFontFromStyle(style) {
    var font = display.defaultFont;

    if (style && typeof style === "object") {
      if (own(style, "font")) {
        font = style.font;
      }
    }
    return font;
  }

  function nativeTextOptions(surface, style) {
    var options = {};
    var font = textFontFromStyle(style);

    if (style && typeof style === "object") {
      if (own(style, "spacing")) {
        options.spacing = style.spacing;
      }
      if (own(style, "background")) {
        options.background = style.background === null
          ? null
          : nativeDrawColor(surface, style.background, surface.background, "ST7789 text background");
      }
    } else if (style !== undefined) {
      options.spacing = style;
    }
    if (font) {
      options.font = font.native || font;
    }
    if (!own(options, "spacing")) {
      options.spacing = surface.spacing;
    }
    return options;
  }

  function nativeText(text, style) {
    if (display && typeof display.encodeText === "function") {
      return display.encodeText(text, textFontFromStyle(style));
    }
    return String(text);
  }

  function requireDisplayBuffer() {
    if (typeof displayBuffer !== "object" ||
        !displayBuffer ||
        typeof displayBuffer.create !== "function") {
      throw new Error("st7789 display driver requires the displayBuffer module");
    }
  }

  function createNativeBuffer(surface, options, chunkBytes) {
    var storage = own(options, "storage") ? options.storage : "dma";
    var config = {
      width: surface.width,
      height: surface.height,
      format: "rgb565",
      storage: storage,
      chunkBytes: chunkBytes,
      foreground: surface.foreground,
      background: surface.background
    };

    try {
      return displayBuffer.create(config);
    } catch (error) {
      if (own(options, "storage") || storage === "auto") {
        throw error;
      }
      config.storage = "auto";
      return displayBuffer.create(config);
    }
  }

  function writeGpio(pin, value) {
    if (typeof pin === "number" && pin >= 0) {
      gpio.digitalWrite(pin, !!value);
    }
  }

  function configureOutput(pin, initialValue, requiredName) {
    if (typeof pin !== "number" || pin < 0) {
      if (requiredName) {
        throw new Error(requiredName + " GPIO is required");
      }
      return;
    }
    gpio.pinMode(pin, gpio.OUTPUT);
    gpio.digitalWrite(pin, !!initialValue);
  }

  function delayMs(ms) {
    if (typeof sleep === "function") {
      sleep(ms);
    }
  }

  function toBytes(value) {
    var bytes;
    var i;

    if (value === undefined || value === null) {
      return [];
    }
    if (typeof value === "number") {
      return [value & 0xff];
    }
    if (value && typeof value === "object" &&
        value.buffer && value.BYTES_PER_ELEMENT === 1 &&
        typeof value.length === "number") {
      return value;
    }
    bytes = new Array(value.length);
    for (i = 0; i < value.length; i += 1) {
      bytes[i] = value[i] & 0xff;
    }
    return bytes;
  }

  function makeByteBuffer(length) {
    if (typeof Uint8Array === "function") {
      return new Uint8Array(length);
    }
    return new Array(length);
  }

  function nowUs() {
    if (typeof esp32 !== "undefined" && esp32 && typeof esp32.micros === "function") {
      return esp32.micros();
    }
    return 0;
  }

  function newPerfStats() {
    return {
      flushCalls: 0,
      chunks: 0,
      pixels: 0,
      bytes: 0,
      totalFlushUs: 0,
      windowUs: 0,
      pixelUs: 0,
      dataUs: 0,
      directFlushes: 0
    };
  }

  function copyOptions(source) {
    var target = {};
    var key;

    source = source || {};
    for (key in source) {
      if (own(source, key)) {
        target[key] = source[key];
      }
    }
    return target;
  }

  function flushRectFromValue(surface, value) {
    var x;
    var y;
    var width;
    var height;

    if (!value || typeof value !== "object") {
      return null;
    }
    if (value.length >= 4) {
      x = value[0];
      y = value[1];
      width = value[2];
      height = value[3];
    } else {
      x = value.x;
      y = value.y;
      width = own(value, "width") ? value.width : value.w;
      height = own(value, "height") ? value.height : value.h;
    }
    x = system.clampInt(x, 0, surface.width);
    y = system.clampInt(y, 0, surface.height);
    width = system.clampInt(width, 0, surface.width - x);
    height = system.clampInt(height, 0, surface.height - y);
    if (width <= 0 || height <= 0) {
      return null;
    }
    return {
      x: x,
      y: y,
      width: width,
      height: height,
      area: width * height
    };
  }

  function rotationMadctl(rotation, bgr) {
    var value;

    rotation = ((rotation | 0) % 4 + 4) % 4;
    if (rotation === 1) {
      value = 0x60;
    } else if (rotation === 2) {
      value = 0xc0;
    } else if (rotation === 3) {
      value = 0xa0;
    } else {
      value = 0x00;
    }
    return bgr ? (value | 0x08) : value;
  }

  function ST7789Display(options) {
    var maxTransferSize;
    var chunkBytes;

    if (typeof spi === "undefined" || typeof gpio === "undefined") {
      throw new Error("st7789 display driver requires spi and gpio modules");
    }
    requireDisplayBuffer();

    options = options || {};
    display.Surface.call(this, {
      driver: own(options, "driverName") ? options.driverName : "st7789",
      width: own(options, "width") ? options.width : DEFAULT_WIDTH,
      height: own(options, "height") ? options.height : DEFAULT_HEIGHT,
      pixelFormat: "rgb565"
    });
    this.spacing = own(options, "spacing") ? options.spacing : 0;

    maxTransferSize = own(options, "maxTransferSize")
      ? options.maxTransferSize
      : DEFAULT_NATIVE_MAX_TRANSFER_SIZE;
    if (maxTransferSize < this.width * 2) {
      maxTransferSize = this.width * 2;
    }
    chunkBytes = own(options, "chunkBytes") ? options.chunkBytes : maxTransferSize;
    if (chunkBytes > maxTransferSize) {
      chunkBytes = maxTransferSize;
    }
    if (chunkBytes < this.width * 2) {
      chunkBytes = this.width * 2;
    }

    this.pixelFormat = "rgb565";
    this.columnOffset = own(options, "columnOffset") ? options.columnOffset | 0 : 0;
    this.rowOffset = own(options, "rowOffset") ? options.rowOffset | 0 : 0;
    this.rotation = own(options, "rotation") ? options.rotation | 0 : 0;
    this.bgr = toBool(options.bgr, false);
    this.inverted = toBool(options.inverted, true);
    this.foreground = normalizeColor(options.foreground, 0xffff, "ST7789 foreground");
    this.background = normalizeColor(options.background, 0x0000, "ST7789 background");
    this.dc = own(options, "dc") ? options.dc : -1;
    this.resetPin = own(options, "reset") ? options.reset : (own(options, "rst") ? options.rst : -1);
    this.backlightPin = own(options, "backlight") ? options.backlight : (own(options, "blk") ? options.blk : -1);
    this.backlightActive = toBool(options.backlightActive, true);
    this.rowsPerChunk = Math.max(1, (chunkBytes / (this.width * 2)) | 0);
    this.chunkBytes = chunkBytes;
    this.payload = null;
    this.nativeBuffer = createNativeBuffer(this, options, chunkBytes);
    this.perfEnabled = toBool(options.perf, false);
    this.perfStats = newPerfStats();
    this.commandByte = makeByteBuffer(1);
    this.windowBytes = makeByteBuffer(4);
    this.busOptions = {
      host: own(options, "host") ? options.host : spi.DEFAULT_HOST,
      sclk: own(options, "sclk") ? options.sclk : spi.DEFAULT_SCLK,
      mosi: own(options, "mosi") ? options.mosi : spi.DEFAULT_MOSI,
      miso: own(options, "miso") ? options.miso : -1,
      maxTransferSize: maxTransferSize
    };
    this.deviceOptions = {
      cs: own(options, "cs") ? options.cs : spi.DEFAULT_CS,
      mode: own(options, "mode") ? options.mode : 0,
      freqHz: own(options, "freqHz") ? options.freqHz : DEFAULT_FREQ_HZ,
      queueSize: own(options, "queueSize") ? options.queueSize : 2
    };
  }

  system.inherit(ST7789Display, display.Surface);

  ST7789Display.rgb565 = rgb565;

  ST7789Display.prototype.clear = function (color) {
    color = nativeDrawColor(this, color, this.background, "ST7789.clear(color)");
    this.nativeBuffer.clear(color);
    return this;
  };

  ST7789Display.prototype.fill = function (color) {
    return this.clear(color);
  };

  ST7789Display.prototype.setPixel = function (x, y, color) {
    color = nativeDrawColor(this, color, this.foreground, "ST7789.setPixel(x, y, color)");
    this.nativeBuffer.setPixel(x, y, color);
    return this;
  };

  ST7789Display.prototype.getPixel = function (x, y) {
    return this.nativeBuffer.getPixel(x, y);
  };

  ST7789Display.prototype.fillRect = function (x, y, width, height, color) {
    color = nativeDrawColor(this, color, this.foreground, "ST7789.fillRect(x, y, width, height, color)");
    this.nativeBuffer.fillRect(x, y, width, height, color);
    return this;
  };

  ST7789Display.prototype.drawCircle = function (cx, cy, radius, color) {
    color = nativeDrawColor(this, color, this.foreground, "ST7789.drawCircle(cx, cy, radius, color)");
    this.nativeBuffer.drawCircle(cx, cy, radius, color);
    return this;
  };

  ST7789Display.prototype.fillCircle = function (cx, cy, radius, color) {
    color = nativeDrawColor(this, color, this.foreground, "ST7789.fillCircle(cx, cy, radius, color)");
    this.nativeBuffer.fillCircle(cx, cy, radius, color);
    return this;
  };

  ST7789Display.prototype.drawEllipse = function (cx, cy, rx, ry, color, options) {
    color = nativeDrawColor(this, color, this.foreground, "ST7789.drawEllipse(cx, cy, rx, ry, color)");
    this.nativeBuffer.drawEllipse(cx, cy, rx, ry, color, options);
    return this;
  };

  ST7789Display.prototype.fillEllipse = function (cx, cy, rx, ry, color) {
    color = nativeDrawColor(this, color, this.foreground, "ST7789.fillEllipse(cx, cy, rx, ry, color)");
    this.nativeBuffer.fillEllipse(cx, cy, rx, ry, color);
    return this;
  };

  ST7789Display.prototype.drawLine = function (x0, y0, x1, y1, color) {
    color = nativeDrawColor(this, color, this.foreground, "ST7789.drawLine(x0, y0, x1, y1, color)");
    this.nativeBuffer.drawLine(x0, y0, x1, y1, color);
    return this;
  };

  ST7789Display.prototype.drawRect = function (x, y, width, height, color) {
    color = nativeDrawColor(this, color, this.foreground, "ST7789.drawRect(x, y, width, height, color)");
    this.nativeBuffer.drawRect(x, y, width, height, color);
    return this;
  };

  ST7789Display.prototype.drawRoundRect = function (x, y, width, height, radius, color) {
    color = nativeDrawColor(this, color, this.foreground, "ST7789.drawRoundRect(x, y, width, height, radius, color)");
    this.nativeBuffer.drawRoundRect(x, y, width, height, radius, color);
    return this;
  };

  ST7789Display.prototype.fillRoundRect = function (x, y, width, height, radius, color) {
    color = nativeDrawColor(this, color, this.foreground, "ST7789.fillRoundRect(x, y, width, height, radius, color)");
    this.nativeBuffer.fillRoundRect(x, y, width, height, radius, color);
    return this;
  };

  ST7789Display.prototype.fillPolygon = function (points, color) {
    color = nativeDrawColor(this, color, this.foreground, "ST7789.fillPolygon(points, color)");
    this.nativeBuffer.fillPolygon(points, color);
    return this;
  };

  ST7789Display.prototype.fillTriangle = function (x0, y0, x1, y1, x2, y2, color) {
    color = nativeDrawColor(this, color, this.foreground, "ST7789.fillTriangle(x0, y0, x1, y1, x2, y2, color)");
    this.nativeBuffer.fillTriangle(x0, y0, x1, y1, x2, y2, color);
    return this;
  };

  ST7789Display.prototype.drawPolyline = function (points, color) {
    color = nativeDrawColor(this, color, this.foreground, "ST7789.drawPolyline(points, color)");
    this.nativeBuffer.drawPolyline(points, color);
    return this;
  };

  ST7789Display.prototype.drawPolygon = function (points, color) {
    color = nativeDrawColor(this, color, this.foreground, "ST7789.drawPolygon(points, color)");
    this.nativeBuffer.drawPolygon(points, color);
    return this;
  };

  ST7789Display.prototype.drawTriangle = function (x0, y0, x1, y1, x2, y2, color) {
    color = nativeDrawColor(this, color, this.foreground, "ST7789.drawTriangle(x0, y0, x1, y1, x2, y2, color)");
    this.nativeBuffer.drawTriangle(x0, y0, x1, y1, x2, y2, color);
    return this;
  };

  ST7789Display.prototype.drawQuadraticBezier = function (x0, y0, cx, cy, x1, y1, color, options) {
    color = nativeDrawColor(this, color, this.foreground, "ST7789.drawQuadraticBezier(x0, y0, cx, cy, x1, y1, color)");
    this.nativeBuffer.drawQuadraticBezier(x0, y0, cx, cy, x1, y1, color, options);
    return this;
  };

  ST7789Display.prototype.drawCubicBezier = function (x0, y0, c1x, c1y, c2x, c2y, x1, y1, color, options) {
    color = nativeDrawColor(this, color, this.foreground, "ST7789.drawCubicBezier(x0, y0, c1x, c1y, c2x, c2y, x1, y1, color)");
    this.nativeBuffer.drawCubicBezier(x0, y0, c1x, c1y, c2x, c2y, x1, y1, color, options);
    return this;
  };

  ST7789Display.prototype.drawBitmap = function (x, y, bitmap, options) {
    var style = styleOptions(options, "ST7789.drawBitmap(x, y, bitmap, options)");
    var color = nativeDrawColor(this, style.color, this.foreground, "ST7789.drawBitmap(x, y, bitmap, options).color");
    var nativeOptions = {
      color: color
    };

    if (style.background !== undefined && style.background !== null) {
      nativeOptions.background = nativeDrawColor(this, style.background, this.background, "ST7789 bitmap background");
    } else if (style.background === null) {
      nativeOptions.background = null;
    }
    this.nativeBuffer.drawBitmap(x, y, bitmap, nativeOptions);
    return this;
  };

  ST7789Display.prototype.drawChar = function (x, y, ch, options) {
    var style = styleOptions(options, "ST7789.drawChar(x, y, ch, options)");
    var color = nativeDrawColor(this, style.color, this.foreground, "ST7789.drawChar(x, y, ch, options).color");
    var nativeOptions;

    nativeOptions = nativeTextOptions(this, style);
    nativeOptions.color = color;
    this.nativeBuffer.drawText(x, y, String(ch).charAt(0), nativeOptions);
    return this;
  };

  ST7789Display.prototype.drawText = function (x, y, text, options) {
    var style = styleOptions(options, "ST7789.drawText(x, y, text, options)");
    var color = nativeDrawColor(this, style.color, this.foreground, "ST7789.drawText(x, y, text, options).color");
    var nativeOptions;

    nativeOptions = nativeTextOptions(this, style);
    nativeOptions.color = color;
    this.nativeBuffer.drawText(x, y, nativeText(text, style), nativeOptions);
    return this;
  };

  ST7789Display.prototype.measureText = function (text, style) {
    return this.nativeBuffer.measureText(nativeText(text, style), nativeTextOptions(this, style));
  };

  ST7789Display.prototype._writeCommand = function (command) {
    this.commandByte[0] = command & 0xff;
    writeGpio(this.dc, false);
    this.device.write(this.commandByte);
    return this;
  };

  ST7789Display.prototype._command = function (command, data) {
    this._writeCommand(command);
    data = toBytes(data);
    if (data.length > 0) {
      writeGpio(this.dc, true);
      this.device.write(data);
    }
    return this;
  };

  ST7789Display.prototype._data = function (data) {
    writeGpio(this.dc, true);
    this.device.write(data);
    return this;
  };

  ST7789Display.prototype._reset = function () {
    if (typeof this.resetPin !== "number" || this.resetPin < 0) {
      return this;
    }
    writeGpio(this.resetPin, true);
    delayMs(10);
    writeGpio(this.resetPin, false);
    delayMs(20);
    writeGpio(this.resetPin, true);
    delayMs(120);
    return this;
  };

  ST7789Display.prototype._setWindow = function (x, y, width, height) {
    var x0 = this.columnOffset + (x | 0);
    var y0 = this.rowOffset + (y | 0);
    var x1 = x0 + (width | 0) - 1;
    var y1 = y0 + (height | 0) - 1;
    var windowBytes = this.windowBytes;

    windowBytes[0] = (x0 >> 8) & 0xff;
    windowBytes[1] = x0 & 0xff;
    windowBytes[2] = (x1 >> 8) & 0xff;
    windowBytes[3] = x1 & 0xff;
    this._command(0x2a, windowBytes);

    windowBytes[0] = (y0 >> 8) & 0xff;
    windowBytes[1] = y0 & 0xff;
    windowBytes[2] = (y1 >> 8) & 0xff;
    windowBytes[3] = y1 & 0xff;
    this._command(0x2b, windowBytes);

    this._writeCommand(0x2c);
    return this;
  };

  ST7789Display.prototype._makePixelPayload = function (x, y, width, rows) {
    return this.nativeBuffer.readRect(x, y, width, rows, { byteOrder: "be" });
  };

  ST7789Display.prototype.init = function () {
    configureOutput(this.dc, false, "dc");
    configureOutput(this.resetPin, true, null);
    configureOutput(this.backlightPin, !this.backlightActive, null);

    this.bus = spi.openBus(this.busOptions);
    this.device = this.bus.openDevice(this.deviceOptions);
    this._reset();

    this._command(0x01);
    delayMs(150);
    this._command(0x11);
    delayMs(120);
    this._command(0x3a, 0x55);
    this._command(0x36, rotationMadctl(this.rotation, this.bgr));
    this._command(this.inverted ? 0x21 : 0x20);
    this._command(0x13);
    delayMs(10);
    this._command(0x29);
    delayMs(100);
    writeGpio(this.backlightPin, this.backlightActive);

    this.ready = true;
    return this.clear().flush();
  };

  ST7789Display.prototype.flush = function () {
    return this.flushRect(0, 0, this.width, this.height);
  };

  ST7789Display.prototype.flushRects = function (rects, options) {
    var list = [];
    var rect;
    var i;
    var x0 = this.width;
    var y0 = this.height;
    var x1 = 0;
    var y1 = 0;
    var totalArea = 0;
    var boundsArea;
    var screenArea = this.width * this.height;
    var merge = true;
    var mergeCoverage = 0.72;
    var mergeAreaRatio = 1.22;
    var mergePixelBudget = this.width * 24;

    if (!rects || typeof rects.length !== "number") {
      return this.flush();
    }
    options = options || {};
    if (own(options, "merge")) {
      merge = options.merge !== false;
    }
    if (own(options, "mergeCoverage")) {
      mergeCoverage = +options.mergeCoverage;
    }
    if (own(options, "mergeAreaRatio")) {
      mergeAreaRatio = +options.mergeAreaRatio;
    }
    if (own(options, "mergePixelBudget")) {
      mergePixelBudget = options.mergePixelBudget | 0;
    }

    for (i = 0; i < rects.length; i += 1) {
      rect = flushRectFromValue(this, rects[i]);
      if (!rect) {
        continue;
      }
      list.push(rect);
      totalArea += rect.area;
      if (rect.x < x0) {
        x0 = rect.x;
      }
      if (rect.y < y0) {
        y0 = rect.y;
      }
      if (rect.x + rect.width > x1) {
        x1 = rect.x + rect.width;
      }
      if (rect.y + rect.height > y1) {
        y1 = rect.y + rect.height;
      }
    }

    if (list.length === 0) {
      return this;
    }
    if (list.length === 1) {
      rect = list[0];
      return this.flushRect(rect.x, rect.y, rect.width, rect.height);
    }

    boundsArea = (x1 - x0) * (y1 - y0);
    if (merge &&
        (totalArea >= screenArea * mergeCoverage ||
         boundsArea <= totalArea * mergeAreaRatio ||
         boundsArea - totalArea <= mergePixelBudget * (list.length - 1))) {
      return this.flushRect(x0, y0, x1 - x0, y1 - y0);
    }

    for (i = 0; i < list.length; i += 1) {
      rect = list[i];
      this.flushRect(rect.x, rect.y, rect.width, rect.height);
    }
    return this;
  };

  ST7789Display.prototype.flushRect = function (x, y, width, height) {
    var perf = this.perfEnabled ? this.perfStats : null;
    var flushStartUs = 0;
    var stepStartUs = 0;
    var payload;
    var chunks;
    var rows;
    var rowsPerChunk;
    var endY;
    var chunkStats;

    if (!this.ready) {
      return this;
    }

    x = system.clampInt(x, 0, this.width);
    y = system.clampInt(y, 0, this.height);
    width = system.clampInt(width, 0, this.width - x);
    height = system.clampInt(height, 0, this.height - y);
    if (width <= 0 || height <= 0) {
      return this;
    }

    endY = y + height;
    rowsPerChunk = Math.max(1, (this.chunkBytes / (width * 2)) | 0);
    if (perf) {
      flushStartUs = nowUs();
      perf.flushCalls += 1;
    }
    if (this.device && typeof this.device.writeChunks === "function") {
      if (perf) {
        stepStartUs = nowUs();
      }
      this._setWindow(x, y, width, height);
      if (perf) {
        perf.windowUs += nowUs() - stepStartUs;
        stepStartUs = nowUs();
      }
      chunks = this.nativeBuffer.readRectChunks(x, y, width, height, {
        byteOrder: "be",
        chunkBytes: this.chunkBytes,
        reuse: true
      });
      if (perf) {
        perf.pixelUs += nowUs() - stepStartUs;
        stepStartUs = nowUs();
      }
      writeGpio(this.dc, true);
      chunkStats = this.device.writeChunks(chunks, {
        queueDepth: this.deviceOptions.queueSize
      });
      if (perf) {
        perf.dataUs += nowUs() - stepStartUs;
        perf.chunks += chunkStats.chunks || chunks.length || 0;
        perf.pixels += width * height;
        perf.bytes += chunkStats.bytes || width * height * 2;
        if (chunkStats.direct) {
          perf.directFlushes += 1;
        }
        perf.totalFlushUs += nowUs() - flushStartUs;
      }
      return this;
    }
    for (; y < endY; y += rows) {
      rows = Math.min(rowsPerChunk, endY - y);
      if (perf) {
        stepStartUs = nowUs();
      }
      this._setWindow(x, y, width, rows);
      if (perf) {
        perf.windowUs += nowUs() - stepStartUs;
        stepStartUs = nowUs();
      }
      payload = this._makePixelPayload(x, y, width, rows);
      if (perf) {
        perf.pixelUs += nowUs() - stepStartUs;
        stepStartUs = nowUs();
      }
      this._data(payload);
      payload = null;
      if (perf) {
        perf.dataUs += nowUs() - stepStartUs;
        perf.chunks += 1;
        perf.pixels += width * rows;
        perf.bytes += width * rows * 2;
      }
    }
    if (perf) {
      perf.totalFlushUs += nowUs() - flushStartUs;
    }
    return this;
  };

  ST7789Display.prototype.resetPerf = function () {
    this.perfStats = newPerfStats();
    return this;
  };

  ST7789Display.prototype.getPerf = function () {
    var stats = this.perfStats || newPerfStats();

    return {
      flushCalls: stats.flushCalls,
      chunks: stats.chunks,
      pixels: stats.pixels,
      bytes: stats.bytes,
      totalFlushUs: stats.totalFlushUs,
      windowUs: stats.windowUs,
      pixelUs: stats.pixelUs,
      dataUs: stats.dataUs,
      directFlushes: stats.directFlushes
    };
  };

  ST7789Display.prototype.on = function () {
    return this._command(0x29);
  };

  ST7789Display.prototype.off = function () {
    return this._command(0x28);
  };

  ST7789Display.prototype.invert = function (enabled) {
    this.inverted = enabled !== false;
    return this._command(this.inverted ? 0x21 : 0x20);
  };

  ST7789Display.prototype.backlight = function (enabled) {
    writeGpio(this.backlightPin, enabled !== false ? this.backlightActive : !this.backlightActive);
    return this;
  };

  ST7789Display.prototype.close = function () {
    this.backlight(false);
    if (this.device) {
      this.device.close();
      this.device = null;
    }
    if (this.bus) {
      this.bus.close();
      this.bus = null;
    }
    if (this.nativeBuffer) {
      this.nativeBuffer.close();
      this.nativeBuffer = null;
    }
    this.ready = false;
    return true;
  };

  display.rgb565 = display.rgb565 || rgb565;
  display.ST7789 = ST7789Display;
  display.registerDriver("st7789", function (options) {
    options = copyOptions(options);
    options.driverName = "st7789";
    return new ST7789Display(options);
  });
  display.registerDriver("wlk1501spi8p", function (options) {
    options = copyOptions(options);
    if (!own(options, "width")) {
      options.width = 240;
    }
    if (!own(options, "height")) {
      options.height = 240;
    }
    options.driverName = "wlk1501spi8p";
    return new ST7789Display(options);
  });

  system.st7789Loaded = true;
})(globalThis);
