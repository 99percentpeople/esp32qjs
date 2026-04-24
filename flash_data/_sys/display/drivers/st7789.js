(function (global) {
  var system = global.__displaySystem;
  var display = global.display;
  var DEFAULT_WIDTH = 240;
  var DEFAULT_HEIGHT = 240;
  var DEFAULT_FREQ_HZ = 20000000;
  var DEFAULT_MAX_TRANSFER_SIZE = 4092;
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

  function rgb565(red, green, blue) {
    red = system.clampInt(red, 0, 255);
    green = system.clampInt(green, 0, 255);
    blue = system.clampInt(blue, 0, 255);
    return ((red & 0xf8) << 8) | ((green & 0xfc) << 3) | (blue >> 3);
  }

  function normalizeColor(value, fallback) {
    if (value === undefined || value === null) {
      return fallback & 0xffff;
    }
    if (typeof value === "boolean") {
      return value ? fallback & 0xffff : 0;
    }
    if (typeof value === "number") {
      return value & 0xffff;
    }
    if (value && typeof value === "object") {
      return rgb565(value.r || 0, value.g || 0, value.b || 0);
    }
    return fallback & 0xffff;
  }

  function nativeDrawColor(surface, value) {
    if (value === undefined) {
      return surface.foreground;
    }
    if (value === null || value === false || value === 0) {
      return surface.background;
    }
    if (value === true) {
      return surface.foreground;
    }
    return normalizeColor(value, surface.foreground);
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

  function hasNativeBuffer() {
    return typeof displayBuffer === "object" &&
      displayBuffer &&
      typeof displayBuffer.create === "function";
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

  function sliceByteBuffer(buffer, length) {
    var out;
    var i;

    if (buffer.length === length) {
      return buffer;
    }
    if (buffer.subarray) {
      return buffer.subarray(0, length);
    }
    out = new Array(length);
    for (i = 0; i < length; i += 1) {
      out[i] = buffer[i];
    }
    return out;
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
      dataUs: 0
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
    var useNativeBuffer;

    if (typeof spi === "undefined" || typeof gpio === "undefined") {
      throw new Error("st7789 display driver requires spi and gpio modules");
    }

    options = options || {};
    display.MonoSurface.call(this, {
      driver: own(options, "driverName") ? options.driverName : "st7789",
      width: own(options, "width") ? options.width : DEFAULT_WIDTH,
      height: own(options, "height") ? options.height : DEFAULT_HEIGHT,
      spacing: own(options, "spacing") ? options.spacing : 0
    });
    useNativeBuffer = hasNativeBuffer();

    maxTransferSize = own(options, "maxTransferSize")
      ? options.maxTransferSize
      : (useNativeBuffer
          ? DEFAULT_NATIVE_MAX_TRANSFER_SIZE
          : (typeof spi.DEFAULT_MAX_TRANSFER_SIZE === "number" ? spi.DEFAULT_MAX_TRANSFER_SIZE : DEFAULT_MAX_TRANSFER_SIZE));
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
    this.foreground = normalizeColor(options.foreground, 0xffff);
    this.background = normalizeColor(options.background, 0x0000);
    this.dc = own(options, "dc") ? options.dc : -1;
    this.resetPin = own(options, "reset") ? options.reset : (own(options, "rst") ? options.rst : -1);
    this.backlightPin = own(options, "backlight") ? options.backlight : (own(options, "blk") ? options.blk : -1);
    this.backlightActive = toBool(options.backlightActive, true);
    this.rowsPerChunk = Math.max(1, (chunkBytes / (this.width * 2)) | 0);
    this.chunkBytes = chunkBytes;
    this.payload = null;
    this.nativeBuffer = useNativeBuffer
      ? displayBuffer.create({
          width: this.width,
          height: this.height,
          format: "rgb565",
          storage: own(options, "storage") ? options.storage : "auto",
          chunkBytes: chunkBytes,
          foreground: this.foreground,
          background: this.background
        })
      : null;
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
      queueSize: own(options, "queueSize") ? options.queueSize : 1
    };
  }

  system.inherit(ST7789Display, display.MonoSurface);

  ST7789Display.rgb565 = rgb565;

  ST7789Display.prototype.clear = function (color) {
    if (this.nativeBuffer) {
      this.nativeBuffer.clear(nativeDrawColor(this, color === undefined ? this.background : color));
      return this;
    }
    return display.MonoSurface.prototype.clear.call(this, color);
  };

  ST7789Display.prototype.fill = function (color) {
    return this.clear(color);
  };

  ST7789Display.prototype.setPixel = function (x, y, color) {
    if (this.nativeBuffer) {
      this.nativeBuffer.setPixel(x, y, nativeDrawColor(this, color));
      return this;
    }
    return display.MonoSurface.prototype.setPixel.call(this, x, y, color);
  };

  ST7789Display.prototype.getPixel = function (x, y) {
    if (this.nativeBuffer) {
      return this.nativeBuffer.getPixel(x, y);
    }
    return display.MonoSurface.prototype.getPixel.call(this, x, y);
  };

  ST7789Display.prototype.fillRect = function (x, y, width, height, color) {
    if (this.nativeBuffer) {
      this.nativeBuffer.fillRect(x, y, width, height, nativeDrawColor(this, color));
      return this;
    }
    return display.MonoSurface.prototype.fillRect.call(this, x, y, width, height, color);
  };

  ST7789Display.prototype.drawLine = function (x0, y0, x1, y1, color) {
    if (this.nativeBuffer) {
      this.nativeBuffer.drawLine(x0, y0, x1, y1, nativeDrawColor(this, color));
      return this;
    }
    return display.MonoSurface.prototype.drawLine.call(this, x0, y0, x1, y1, color);
  };

  ST7789Display.prototype.drawRect = function (x, y, width, height, color) {
    if (this.nativeBuffer) {
      this.nativeBuffer.drawRect(x, y, width, height, nativeDrawColor(this, color));
      return this;
    }
    return display.MonoSurface.prototype.drawRect.call(this, x, y, width, height, color);
  };

  ST7789Display.prototype.drawBitmap = function (x, y, bitmap, color) {
    if (this.nativeBuffer) {
      this.nativeBuffer.drawBitmap(x, y, bitmap, nativeDrawColor(this, color));
      return this;
    }
    return display.MonoSurface.prototype.drawBitmap.call(this, x, y, bitmap, color);
  };

  ST7789Display.prototype.drawChar = function (x, y, ch, color) {
    if (this.nativeBuffer) {
      this.nativeBuffer.drawText(x, y, String(ch).charAt(0), nativeDrawColor(this, color));
      return this;
    }
    return display.MonoSurface.prototype.drawChar.call(this, x, y, ch, color);
  };

  ST7789Display.prototype.drawText = function (x, y, text, color, style) {
    if (this.nativeBuffer) {
      this.nativeBuffer.drawText(x, y, nativeText(text, style), nativeDrawColor(this, color), nativeTextOptions(this, style));
      return this;
    }
    return display.MonoSurface.prototype.drawText.call(this, x, y, text, color, style);
  };

  ST7789Display.prototype.measureText = function (text, style) {
    if (this.nativeBuffer) {
      return this.nativeBuffer.measureText(nativeText(text, style), nativeTextOptions(this, style));
    }
    return display.MonoSurface.prototype.measureText.call(this, text, style);
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

  ST7789Display.prototype._payloadBuffer = function (length) {
    if (!this.payload || this.payload.length < length) {
      this.payload = makeByteBuffer(length);
    }
    return this.payload;
  };

  ST7789Display.prototype._makePixelPayload = function (x, y, width, rows) {
    var length = width * rows * 2;
    var payload;
    var out = 0;
    var yy;
    var xx;
    var page;
    var bit;
    var color;

    if (this.nativeBuffer) {
      return this.nativeBuffer.readRect(x, y, width, rows, { byteOrder: "be" });
    }

    payload = this._payloadBuffer(length);
    for (yy = y; yy < y + rows; yy += 1) {
      page = yy >> 3;
      bit = 1 << (yy & 7);
      for (xx = x; xx < x + width; xx += 1) {
        color = (this.buffer[xx + (this.width * page)] & bit) !== 0 ? this.foreground : this.background;
        payload[out] = (color >> 8) & 0xff;
        payload[out + 1] = color & 0xff;
        out += 2;
      }
    }
    return sliceByteBuffer(payload, length);
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
    return this.clear(false).flush();
  };

  ST7789Display.prototype.flush = function () {
    return this.flushRect(0, 0, this.width, this.height);
  };

  ST7789Display.prototype.flushRect = function (x, y, width, height) {
    var perf = this.perfEnabled ? this.perfStats : null;
    var flushStartUs = 0;
    var stepStartUs = 0;
    var payload;
    var rows;
    var rowsPerChunk;
    var endY;

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
      dataUs: stats.dataUs
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
