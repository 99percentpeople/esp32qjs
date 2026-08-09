(function (global) {
  var system = global.__displaySystemV2;
  var display = global.display;
  var DEFAULT_WIDTH = 128;
  var DEFAULT_HEIGHT = 64;

  if (!system || system.ssd1306DriverLoaded) {
    return;
  }

  function own(value, key) {
    return system.own(value, key);
  }

  function nowUs() {
    return global.esp32 && typeof global.esp32.micros === "function"
      ? global.esp32.micros()
      : 0;
  }

  function newStats() {
    return {
      presents: 0,
      regions: 0,
      pixels: 0,
      bytes: 0,
      totalUs: 0
    };
  }

  function copyStats(stats) {
    return {
      presents: stats.presents,
      regions: stats.regions,
      pixels: stats.pixels,
      bytes: stats.bytes,
      totalUs: stats.totalUs
    };
  }

  function requireTransport(transport) {
    if (!transport || typeof transport.open !== "function" ||
        typeof transport.command !== "function" ||
        typeof transport.write !== "function" ||
        typeof transport.close !== "function") {
      throw new TypeError("SSD1306 driver requires an I2C display transport");
    }
    if (transport.kind !== "i2c") {
      throw new TypeError("SSD1306 driver transport must be kind 'i2c'");
    }
    return transport;
  }

  function SSD1306Driver(options) {
    options = system.assertKnownOptions(options || {}, [
      "transport",
      "width",
      "height"
    ], "display.drivers.create(\"ssd1306\", options)");

    this.name = "ssd1306";
    this.transport = requireTransport(options.transport);
    this.width = own(options, "width") ? options.width | 0 : DEFAULT_WIDTH;
    this.height = own(options, "height") ? options.height | 0 : DEFAULT_HEIGHT;
    if (this.width <= 0 || this.width > 256 ||
        this.height <= 0 || this.height > 64) {
      throw new RangeError("SSD1306 dimensions must fit within 256x64");
    }
    this.pixelFormat = "mono1";
    this.layout = "page-y8";
    this.byteOrder = "native";
    this.capabilities = {
      partialPresent: true,
      multiRegion: true,
      power: true,
      inversion: true,
      contrast: true,
      backlight: false
    };
    this.state = "created";
    this._stats = newStats();
  }

  SSD1306Driver.prototype.requireOpen = function (apiName) {
    if (this.state !== "open") {
      throw new Error(apiName + " requires an open SSD1306 driver");
    }
    return this;
  };

  SSD1306Driver.prototype.open = function () {
    var contrast = this.height <= 32 ? 0x8f : 0xcf;
    var comPins = this.height <= 32 ? 0x02 : 0x12;

    if (this.state === "open") {
      return this;
    }
    if (this.state === "closed") {
      throw new Error("cannot reopen a closed SSD1306 driver");
    }
    try {
      this.transport.open();
      this.transport.command([
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
      this.state = "open";
      return this;
    } catch (error) {
      try {
        this.transport.close();
      } catch (cleanupError) {
      }
      this.state = "closed";
      throw error;
    }
  };

  SSD1306Driver.prototype.present = function (frame, regions) {
    var started;
    var rect;
    var y0;
    var y1;
    var page0;
    var page1;
    var payload;
    var pixels = 0;
    var bytes = 0;
    var i;
    var totalUs = 0;

    this.requireOpen("SSD1306Driver.present()");
    if (!frame || frame.pixelFormat !== "mono1" || frame.layout !== "page-y8") {
      throw new TypeError("SSD1306Driver.present() requires a mono1 page-y8 frame");
    }
    started = nowUs();
    for (i = 0; i < regions.length; i += 1) {
      rect = regions[i];
      y0 = (rect.y >> 3) << 3;
      y1 = Math.min(this.height, ((rect.y + rect.height + 7) >> 3) << 3);
      page0 = y0 >> 3;
      page1 = (y1 >> 3) - 1;
      this.transport.command([
        0x21, rect.x, rect.x + rect.width - 1,
        0x22, page0, page1
      ]);
      payload = frame.readRect(rect.x, y0, rect.width, y1 - y0).toArray();
      this.transport.write(payload);
      pixels += rect.width * (y1 - y0);
      bytes += payload.length;
    }
    if (started !== 0) {
      totalUs = nowUs() - started;
    }
    this._stats.presents += 1;
    this._stats.regions += regions.length;
    this._stats.pixels += pixels;
    this._stats.bytes += bytes;
    this._stats.totalUs += totalUs;
    return {
      regions: regions.length,
      pixels: pixels,
      bytes: bytes,
      chunks: regions.length,
      directTransfers: 0,
      totalUs: totalUs,
      transferUs: totalUs
    };
  };

  SSD1306Driver.prototype.setPower = function (enabled) {
    this.requireOpen("SSD1306Driver.setPower()");
    this.transport.command(enabled !== false ? 0xaf : 0xae);
    return this;
  };

  SSD1306Driver.prototype.setInverted = function (enabled) {
    this.requireOpen("SSD1306Driver.setInverted()");
    this.transport.command(enabled !== false ? 0xa7 : 0xa6);
    return this;
  };

  SSD1306Driver.prototype.setContrast = function (value) {
    this.requireOpen("SSD1306Driver.setContrast()");
    this.transport.command([0x81, system.clampInt(value, 0, 255)]);
    return this;
  };

  SSD1306Driver.prototype.stats = function () {
    return copyStats(this._stats);
  };

  SSD1306Driver.prototype.resetStats = function () {
    this._stats = newStats();
    return this;
  };

  SSD1306Driver.prototype.close = function () {
    var firstError = null;

    if (this.state === "closed") {
      return true;
    }
    if (this.state === "open") {
      try {
        this.transport.command(0xae);
      } catch (error) {
        firstError = error;
      }
    }
    try {
      this.transport.close();
    } catch (error2) {
      if (!firstError) {
        firstError = error2;
      }
    }
    this.state = "closed";
    if (firstError) {
      throw firstError;
    }
    return true;
  };

  display.drivers.register("ssd1306", function (options) {
    return new SSD1306Driver(options);
  });
  system.SSD1306Driver = SSD1306Driver;
  system.ssd1306DriverLoaded = true;
})(globalThis);
