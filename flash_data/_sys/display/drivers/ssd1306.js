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

  function ensureI2CBus(options) {
    var desired = {
      sda: own(options, "sda") ? options.sda : i2c.DEFAULT_SDA,
      scl: own(options, "scl") ? options.scl : i2c.DEFAULT_SCL,
      freqHz: own(options, "freqHz") ? options.freqHz : i2c.DEFAULT_FREQ_HZ,
      timeoutMs: own(options, "timeoutMs") ? options.timeoutMs : i2c.DEFAULT_TIMEOUT_MS,
      internalPullup: toBool(options.internalPullup, true)
    };
    var status = i2c.status();

    if (!status.opened ||
        status.sda !== desired.sda ||
        status.scl !== desired.scl ||
        status.freqHz !== desired.freqHz ||
        status.timeoutMs !== desired.timeoutMs ||
        status.internalPullup !== desired.internalPullup) {
      status = i2c.open(desired);
    }

    return status;
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

  function writeCommand(address, payload) {
    var command = [0x00];
    var i;

    if (typeof payload === "number") {
      command.push(payload & 0xff);
    } else {
      for (i = 0; i < payload.length; i += 1) {
        command.push(payload[i] & 0xff);
      }
    }

    return i2c.write(address, command);
  }

  function SSD1306Display(options) {
    options = options || {};

    display.MonoSurface.call(this, {
      driver: "ssd1306",
      width: own(options, "width") ? options.width : DEFAULT_WIDTH,
      height: own(options, "height") ? options.height : DEFAULT_HEIGHT,
      spacing: own(options, "spacing") ? options.spacing : DEFAULT_SPACING
    });
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

  SSD1306Display.prototype.command = function (payload) {
    writeCommand(this.address, payload);
    return this;
  };

  SSD1306Display.prototype.init = function () {
    var contrast = this.height <= 32 ? 0x8f : 0xcf;
    var comPins = this.height <= 32 ? 0x02 : 0x12;

    ensureI2CBus(this.busOptions);
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
    this.command([
      0x21, 0x00, this.width - 1,
      0x22, 0x00, this.pages - 1
    ]);
    i2c.write(this.address, makeDataPayload(this.buffer));
    return this;
  };

  display.registerDriver("ssd1306", function (options) {
    return new SSD1306Display(options || {});
  });

  system.ssd1306Loaded = true;
})(globalThis);
