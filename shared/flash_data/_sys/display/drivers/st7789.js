(function (global) {
  var system = global.__displaySystemV2;
  var display = global.display;
  var DEFAULT_WIDTH = 240;
  var DEFAULT_HEIGHT = 240;

  if (!system || system.st7789DriverLoaded) {
    return;
  }

  function own(value, key) {
    return system.own(value, key);
  }

  function nowUs() {
    return global.sys && typeof global.sys.micros === "function"
      ? global.sys.micros()
      : 0;
  }

  function delayMs(ms) {
    if (typeof global.sleep === "function") {
      global.sleep(ms);
    }
  }

  function makeByteBuffer(length) {
    return typeof global.Uint8Array === "function"
      ? new global.Uint8Array(length)
      : new Array(length);
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

  function newStats() {
    return {
      presents: 0,
      regions: 0,
      pixels: 0,
      bytes: 0,
      chunks: 0,
      directTransfers: 0,
      totalUs: 0,
      prepareUs: 0,
      panelUs: 0,
      transferUs: 0
    };
  }

  function copyStats(stats) {
    return {
      presents: stats.presents,
      regions: stats.regions,
      pixels: stats.pixels,
      bytes: stats.bytes,
      chunks: stats.chunks,
      directTransfers: stats.directTransfers,
      totalUs: stats.totalUs,
      prepareUs: stats.prepareUs,
      panelUs: stats.panelUs,
      transferUs: stats.transferUs
    };
  }

  function addResult(target, result, pixels, timings) {
    result = result || {};
    target.regions += 1;
    target.pixels += pixels;
    target.bytes += result.bytes || pixels * 2;
    target.chunks += result.chunks || 1;
    target.directTransfers += result.direct ? 1 : 0;
    target.prepareUs += timings.prepareUs;
    target.panelUs += timings.panelUs;
    target.transferUs += result.transferUs || result.totalUs || timings.transferUs;
  }

  function requireTransport(transport) {
    if (!transport || typeof transport.open !== "function" ||
        typeof transport.command !== "function" ||
        typeof transport.write !== "function" ||
        typeof transport.close !== "function") {
      throw new TypeError("ST7789 driver requires an SPI4Wire display transport");
    }
    if (transport.kind !== "spi4wire") {
      throw new TypeError("ST7789 driver transport must be kind 'spi4wire'");
    }
    return transport;
  }

  function ST7789Driver(options) {
    options = system.assertKnownOptions(options || {}, [
      "transport",
      "width",
      "height",
      "columnOffset",
      "rowOffset",
      "rotation",
      "bgr",
      "inverted"
    ], "display.drivers.create(\"st7789\", options)");

    this.name = "st7789";
    this.transport = requireTransport(options.transport);
    this.width = own(options, "width") ? options.width | 0 : DEFAULT_WIDTH;
    this.height = own(options, "height") ? options.height | 0 : DEFAULT_HEIGHT;
    if (this.width <= 0 || this.width > 320 ||
        this.height <= 0 || this.height > 320) {
      throw new RangeError("ST7789 dimensions must fit within 320x320");
    }
    this.columnOffset = own(options, "columnOffset") ? options.columnOffset | 0 : 0;
    this.rowOffset = own(options, "rowOffset") ? options.rowOffset | 0 : 0;
    this.rotation = own(options, "rotation") ? options.rotation | 0 : 0;
    this.bgr = system.toBool(options.bgr, false);
    this.inverted = system.toBool(options.inverted, true);
    this.pixelFormat = "rgb565";
    this.layout = "linear";
    this.byteOrder = "be";
    this.capabilities = {
      partialPresent: true,
      multiRegion: true,
      power: true,
      inversion: true,
      contrast: false,
      backlight: !!(
        this.transport.capabilities && this.transport.capabilities.backlight
      )
    };
    this.windowBytes = makeByteBuffer(4);
    this.state = "created";
    this._stats = newStats();
  }

  ST7789Driver.prototype.requireOpen = function (apiName) {
    if (this.state !== "open") {
      throw new Error(apiName + " requires an open ST7789 driver");
    }
    return this;
  };

  ST7789Driver.prototype.open = function () {
    if (this.state === "open") {
      return this;
    }
    if (this.state === "closed") {
      throw new Error("cannot reopen a closed ST7789 driver");
    }
    try {
      this.transport.open();
      if (this.transport.capabilities && this.transport.capabilities.reset) {
        this.transport.reset();
      }
      this.transport.command(0x01);
      delayMs(150);
      this.transport.command(0x11);
      delayMs(120);
      this.transport.command(0x3a, 0x55);
      this.transport.command(0x36, rotationMadctl(this.rotation, this.bgr));
      this.transport.command(this.inverted ? 0x21 : 0x20);
      this.transport.command(0x13);
      delayMs(10);
      this.transport.command(0x29);
      delayMs(100);
      if (this.capabilities.backlight) {
        this.transport.setBacklight(true);
      }
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

  ST7789Driver.prototype.setWindow = function (x, y, width, height) {
    var x0 = this.columnOffset + (x | 0);
    var y0 = this.rowOffset + (y | 0);
    var x1 = x0 + (width | 0) - 1;
    var y1 = y0 + (height | 0) - 1;
    var bytes = this.windowBytes;

    bytes[0] = (x0 >> 8) & 0xff;
    bytes[1] = x0 & 0xff;
    bytes[2] = (x1 >> 8) & 0xff;
    bytes[3] = x1 & 0xff;
    this.transport.command(0x2a, bytes);

    bytes[0] = (y0 >> 8) & 0xff;
    bytes[1] = y0 & 0xff;
    bytes[2] = (y1 >> 8) & 0xff;
    bytes[3] = y1 & 0xff;
    this.transport.command(0x2b, bytes);
    this.transport.command(0x2c);
    return this;
  };

  ST7789Driver.prototype.presentRegion = function (frame, rect, options) {
    var metrics = options.metrics === true;
    var timings = { prepareUs: 0, panelUs: 0, transferUs: 0 };
    var started = metrics ? nowUs() : 0;
    var step = started;
    var spanSource;
    var chunks;
    var payload;
    var result;
    var transferStart;
    var sourceOptions = { byteOrder: "be" };
    var writeOptions = {
      queueDepth: own(options, "queueDepth")
        ? options.queueDepth
        : this.transport.queueDepth
    };

    if (frame.chunkBytes > 0) {
      sourceOptions.chunkBytes = frame.chunkBytes;
    }
    this.setWindow(rect.x, rect.y, rect.width, rect.height);
    if (metrics && step !== 0) {
      timings.panelUs = nowUs() - step;
      step = nowUs();
    }

    spanSource = this.transport.capabilities && this.transport.capabilities.source
      ? frame.getSpanSource(sourceOptions)
      : null;
    if (spanSource && typeof this.transport.writeSource === "function") {
      spanSource.setRect(rect.x, rect.y, rect.width, rect.height);
      if (metrics && step !== 0) {
        timings.prepareUs = nowUs() - step;
        transferStart = nowUs();
      }
      result = this.transport.writeSource(spanSource, writeOptions);
    } else if (this.transport.capabilities && this.transport.capabilities.chunks &&
               typeof this.transport.writeChunks === "function") {
      sourceOptions.reuse = true;
      chunks = frame.readRectChunks(
        rect.x,
        rect.y,
        rect.width,
        rect.height,
        sourceOptions
      );
      if (metrics && step !== 0) {
        timings.prepareUs = nowUs() - step;
        transferStart = nowUs();
      }
      result = this.transport.writeChunks(chunks, writeOptions);
    } else {
      payload = frame.readRect(
        rect.x,
        rect.y,
        rect.width,
        rect.height,
        sourceOptions
      );
      if (metrics && step !== 0) {
        timings.prepareUs = nowUs() - step;
        transferStart = nowUs();
      }
      try {
        this.transport.write(payload);
      } finally {
        payload.close();
      }
      result = {
        chunks: 1,
        bytes: rect.width * rect.height * 2,
        direct: false
      };
    }
    if (metrics && transferStart) {
      timings.transferUs = nowUs() - transferStart;
    }
    return {
      result: result,
      timings: timings,
      totalUs: metrics && started !== 0 ? nowUs() - started : 0
    };
  };

  ST7789Driver.prototype.present = function (frame, regions, options) {
    var started;
    var regionResult;
    var result = {
      regions: 0,
      pixels: 0,
      bytes: 0,
      chunks: 0,
      directTransfers: 0,
      totalUs: 0,
      prepareUs: 0,
      panelUs: 0,
      transferUs: 0
    };
    var rect;
    var i;

    this.requireOpen("ST7789Driver.present()");
    if (!frame || frame.pixelFormat !== "rgb565" || frame.layout !== "linear") {
      throw new TypeError("ST7789Driver.present() requires an rgb565 linear frame");
    }
    options = options || {};
    started = options.metrics === true ? nowUs() : 0;
    for (i = 0; i < regions.length; i += 1) {
      rect = regions[i];
      regionResult = this.presentRegion(frame, rect, options);
      addResult(result, regionResult.result, rect.width * rect.height,
        regionResult.timings);
    }
    result.totalUs = started !== 0 ? nowUs() - started : 0;
    this._stats.presents += 1;
    this._stats.regions += result.regions;
    this._stats.pixels += result.pixels;
    this._stats.bytes += result.bytes;
    this._stats.chunks += result.chunks;
    this._stats.directTransfers += result.directTransfers;
    this._stats.totalUs += result.totalUs;
    this._stats.prepareUs += result.prepareUs;
    this._stats.panelUs += result.panelUs;
    this._stats.transferUs += result.transferUs;
    return result;
  };

  ST7789Driver.prototype.setPower = function (enabled) {
    this.requireOpen("ST7789Driver.setPower()");
    this.transport.command(enabled !== false ? 0x29 : 0x28);
    return this;
  };

  ST7789Driver.prototype.setInverted = function (enabled) {
    this.requireOpen("ST7789Driver.setInverted()");
    this.inverted = enabled !== false;
    this.transport.command(this.inverted ? 0x21 : 0x20);
    return this;
  };

  ST7789Driver.prototype.setBacklight = function (enabled) {
    this.requireOpen("ST7789Driver.setBacklight()");
    if (!this.capabilities.backlight) {
      throw new Error("ST7789 driver transport does not manage a backlight");
    }
    this.transport.setBacklight(enabled !== false);
    return this;
  };

  ST7789Driver.prototype.stats = function () {
    return copyStats(this._stats);
  };

  ST7789Driver.prototype.resetStats = function () {
    this._stats = newStats();
    return this;
  };

  ST7789Driver.prototype.close = function () {
    var firstError = null;

    if (this.state === "closed") {
      return true;
    }
    if (this.state === "open") {
      if (this.capabilities.backlight) {
        try {
          this.transport.setBacklight(false);
        } catch (error) {
          firstError = error;
        }
      }
      try {
        this.transport.command(0x28);
      } catch (error2) {
        if (!firstError) {
          firstError = error2;
        }
      }
    }
    try {
      this.transport.close();
    } catch (error3) {
      if (!firstError) {
        firstError = error3;
      }
    }
    this.state = "closed";
    if (firstError) {
      throw firstError;
    }
    return true;
  };

  display.drivers.register("st7789", function (options) {
    return new ST7789Driver(options);
  });
  system.ST7789Driver = ST7789Driver;
  system.st7789DriverLoaded = true;
})(globalThis);
