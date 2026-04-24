(function (global) {
  var system = global.__displaySystem;
  var display = global.display;
  var DEFAULT_ADDRESS = 0x3c;
  var DEFAULT_WIDTH = 128;
  var DEFAULT_HEIGHT = 64;
  var DEFAULT_SPACING = 0;

  if (!system || system.ssd1306Loaded) {
    return;
  }

  function own(obj, key) {
    return system.own(obj, key);
  }

  function toBool(value, fallback) {
    return system.toBool(value, fallback);
  }

  function toEnabled(value) {
    return !(value === false || value === 0 || value === null);
  }

  function textFontFromStyle(style) {
    var font = display.defaultFont;

    if (style && typeof style === "object" && own(style, "font")) {
      font = style.font;
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

  function busMatches(bus, desired) {
    var status;

    if (!bus) {
      return false;
    }

    try {
      status = bus.status();
    } catch (error) {
      return false;
    }

    return status.opened &&
      status.sda === desired.sda &&
      status.scl === desired.scl &&
      status.freqHz === desired.freqHz &&
      status.timeoutMs === desired.timeoutMs &&
      status.internalPullup === desired.internalPullup;
  }

  function ensureI2CBus(options, currentBus) {
    var desired = {
      sda: own(options, "sda") ? options.sda : i2c.DEFAULT_SDA,
      scl: own(options, "scl") ? options.scl : i2c.DEFAULT_SCL,
      freqHz: own(options, "freqHz") ? options.freqHz : i2c.DEFAULT_FREQ_HZ,
      timeoutMs: own(options, "timeoutMs") ? options.timeoutMs : i2c.DEFAULT_TIMEOUT_MS,
      internalPullup: toBool(options.internalPullup, true)
    };
    var bus = currentBus;

    if (!busMatches(bus, desired)) {
      if (bus) {
        try {
          bus.close();
        } catch (error) {
        }
      }
      bus = i2c.open(desired);
    }

    return bus;
  }

  function makeDataPayload(buffer) {
    var payload = new Array(buffer.length + 1);
    var i;

    payload[0] = 0x40;
    for (i = 0; i < buffer.length; i += 1) {
      payload[i + 1] = buffer[i];
    }
    return payload;
  }

  function writeCommand(bus, address, payload) {
    var command = [0x00];
    var i;

    if (typeof payload === "number") {
      command.push(payload & 0xff);
    } else {
      for (i = 0; i < payload.length; i += 1) {
        command.push(payload[i] & 0xff);
      }
    }

    return bus.write(address, command);
  }

  function SSD1306Display(options) {
    var useNativeBuffer;

    options = options || {};

    display.MonoSurface.call(this, {
      driver: "ssd1306",
      width: own(options, "width") ? options.width : DEFAULT_WIDTH,
      height: own(options, "height") ? options.height : DEFAULT_HEIGHT,
      spacing: own(options, "spacing") ? options.spacing : DEFAULT_SPACING
    });
    useNativeBuffer = hasNativeBuffer();
    this.nativeBuffer = useNativeBuffer
      ? displayBuffer.create({
          width: this.width,
          height: this.height,
          format: displayBuffer.MONO1,
          layout: "page-y8",
          storage: own(options, "storage") ? options.storage : "auto",
          foreground: true,
          background: false
        })
      : null;
    this.address = own(options, "address") ? options.address : DEFAULT_ADDRESS;
    this.busOptions = {
      sda: own(options, "sda") ? options.sda : i2c.DEFAULT_SDA,
      scl: own(options, "scl") ? options.scl : i2c.DEFAULT_SCL,
      freqHz: own(options, "freqHz") ? options.freqHz : i2c.DEFAULT_FREQ_HZ,
      timeoutMs: own(options, "timeoutMs") ? options.timeoutMs : i2c.DEFAULT_TIMEOUT_MS,
      internalPullup: toBool(options.internalPullup, true)
    };
  }

  system.inherit(SSD1306Display, display.MonoSurface);

  SSD1306Display.prototype.clear = function (enabled) {
    if (this.nativeBuffer) {
      this.nativeBuffer.clear(toEnabled(enabled));
      return this;
    }
    return display.MonoSurface.prototype.clear.call(this, enabled);
  };

  SSD1306Display.prototype.fill = function (enabled) {
    return this.clear(enabled);
  };

  SSD1306Display.prototype.setPixel = function (x, y, enabled) {
    if (this.nativeBuffer) {
      this.nativeBuffer.setPixel(x, y, toEnabled(enabled));
      return this;
    }
    return display.MonoSurface.prototype.setPixel.call(this, x, y, enabled);
  };

  SSD1306Display.prototype.getPixel = function (x, y) {
    if (this.nativeBuffer) {
      return !!this.nativeBuffer.getPixel(x, y);
    }
    return display.MonoSurface.prototype.getPixel.call(this, x, y);
  };

  SSD1306Display.prototype.fillRect = function (x, y, width, height, enabled) {
    if (this.nativeBuffer) {
      this.nativeBuffer.fillRect(x, y, width, height, toEnabled(enabled));
      return this;
    }
    return display.MonoSurface.prototype.fillRect.call(this, x, y, width, height, enabled);
  };

  SSD1306Display.prototype.drawLine = function (x0, y0, x1, y1, enabled) {
    if (this.nativeBuffer) {
      this.nativeBuffer.drawLine(x0, y0, x1, y1, toEnabled(enabled));
      return this;
    }
    return display.MonoSurface.prototype.drawLine.call(this, x0, y0, x1, y1, enabled);
  };

  SSD1306Display.prototype.drawRect = function (x, y, width, height, enabled) {
    if (this.nativeBuffer) {
      this.nativeBuffer.drawRect(x, y, width, height, toEnabled(enabled));
      return this;
    }
    return display.MonoSurface.prototype.drawRect.call(this, x, y, width, height, enabled);
  };

  SSD1306Display.prototype.drawBitmap = function (x, y, bitmap, enabled) {
    if (this.nativeBuffer) {
      this.nativeBuffer.drawBitmap(x, y, bitmap, toEnabled(enabled));
      return this;
    }
    return display.MonoSurface.prototype.drawBitmap.call(this, x, y, bitmap, enabled);
  };

  SSD1306Display.prototype.drawChar = function (x, y, ch, enabled) {
    if (this.nativeBuffer) {
      this.nativeBuffer.drawText(x, y, String(ch).charAt(0), toEnabled(enabled), nativeTextOptions(this));
      return this;
    }
    return display.MonoSurface.prototype.drawChar.call(this, x, y, ch, enabled);
  };

  SSD1306Display.prototype.drawText = function (x, y, text, enabled, style) {
    if (this.nativeBuffer) {
      this.nativeBuffer.drawText(x, y, nativeText(text, style), toEnabled(enabled), nativeTextOptions(this, style));
      return this;
    }
    return display.MonoSurface.prototype.drawText.call(this, x, y, text, enabled, style);
  };

  SSD1306Display.prototype.measureText = function (text, style) {
    if (this.nativeBuffer) {
      return this.nativeBuffer.measureText(nativeText(text, style), nativeTextOptions(this, style));
    }
    return display.MonoSurface.prototype.measureText.call(this, text, style);
  };

  SSD1306Display.prototype.command = function (payload) {
    this.bus = ensureI2CBus(this.busOptions, this.bus);
    writeCommand(this.bus, this.address, payload);
    return this;
  };

  SSD1306Display.prototype.init = function () {
    var contrast = this.height <= 32 ? 0x8f : 0xcf;
    var comPins = this.height <= 32 ? 0x02 : 0x12;

    this.bus = ensureI2CBus(this.busOptions, this.bus);
    this.command([
      0xae,
      0xd5, 0x80,
      0xa8, this.height - 1,
      0xd3, 0x00,
      0x40,
      0x8d, 0x14,
      0x20, 0x00,
      0xa1,
      0xc8,
      0xda, comPins,
      0x81, contrast,
      0xd9, 0xf1,
      0xdb, 0x40,
      0xa4,
      0xa6,
      0x2e,
      0xaf
    ]);

    this.ready = true;
    return this.clear(false).flush();
  };

  SSD1306Display.prototype.on = function () {
    return this.command(0xaf);
  };

  SSD1306Display.prototype.off = function () {
    return this.command(0xae);
  };

  SSD1306Display.prototype.invert = function (enabled) {
    return this.command(enabled ? 0xa7 : 0xa6);
  };

  SSD1306Display.prototype.contrast = function (value) {
    var level = system.clampInt(value, 0, 255);

    return this.command([0x81, level]);
  };

  SSD1306Display.prototype.flush = function () {
    var buffer = this.nativeBuffer
      ? this.nativeBuffer.readRect(0, 0, this.width, this.height).toArray()
      : this.buffer;

    this.bus = ensureI2CBus(this.busOptions, this.bus);
    this.command([
      0x21, 0x00, this.width - 1,
      0x22, 0x00, this.pages - 1
    ]);
    this.bus.write(this.address, makeDataPayload(buffer));
    return this;
  };

  SSD1306Display.prototype.close = function () {
    if (this.nativeBuffer) {
      this.nativeBuffer.close();
      this.nativeBuffer = null;
    }
    if (this.bus) {
      this.bus.close();
      this.bus = null;
    }
    this.ready = false;
    return true;
  };

  display.registerDriver("ssd1306", function (options) {
    return new SSD1306Display(options || {});
  });

  system.ssd1306Loaded = true;
})(globalThis);
