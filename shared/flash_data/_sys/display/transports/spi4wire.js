(function (global) {
  var system = global.__displaySystemV2;
  var display = global.display;

  if (!system || system.spi4WireTransportLoaded) {
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

  function validPin(pin) {
    return typeof pin === "number" && pin >= 0;
  }

  function configureOutput(pin, initialValue, requiredName) {
    if (!validPin(pin)) {
      if (requiredName) {
        throw new Error(requiredName + " GPIO is required");
      }
      return false;
    }
    global.gpio.pinMode(pin, global.gpio.OUTPUT);
    global.gpio.digitalWrite(pin, !!initialValue);
    return true;
  }

  function writePin(pin, value) {
    if (validPin(pin)) {
      global.gpio.digitalWrite(pin, !!value);
    }
  }

  function releasePin(pin) {
    if (validPin(pin) && global.gpio && global.gpio.DISABLED !== undefined) {
      global.gpio.pinMode(pin, global.gpio.DISABLED);
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
    return typeof global.Uint8Array === "function"
      ? new global.Uint8Array(length)
      : new Array(length);
  }

  function newStats() {
    return {
      commands: 0,
      writes: 0,
      chunks: 0,
      bytes: 0,
      directTransfers: 0,
      transferUs: 0,
      totalUs: 0
    };
  }

  function copyStats(stats) {
    return {
      commands: stats.commands,
      writes: stats.writes,
      chunks: stats.chunks,
      bytes: stats.bytes,
      directTransfers: stats.directTransfers,
      transferUs: stats.transferUs,
      totalUs: stats.totalUs
    };
  }

  function SPI4WireTransport(options) {
    var pins;

    options = system.assertKnownOptions(options || {}, [
      "bus",
      "device",
      "busOptions",
      "deviceOptions",
      "pins",
      "backlightActive"
    ], "display.transports.create(\"spi4wire\", options)");
    pins = options.pins || {};
    system.assertKnownOptions(pins, ["dc", "reset", "backlight"],
      "SPI4Wire transport pins");

    this.kind = "spi4wire";
    this.state = "created";
    this.suppliedBus = options.bus || null;
    this.suppliedDevice = options.device || null;
    this.bus = null;
    this.device = null;
    this.ownedBus = false;
    this.ownedDevice = false;
    this.busOptions = system.copyObject(options.busOptions || {});
    this.deviceOptions = system.copyObject(options.deviceOptions || {});
    this.queueDepth = own(this.deviceOptions, "queueSize")
      ? this.deviceOptions.queueSize | 0
      : 2;
    this.dc = own(pins, "dc") ? pins.dc : -1;
    this.resetPin = own(pins, "reset") ? pins.reset : -1;
    this.backlightPin = own(pins, "backlight") ? pins.backlight : -1;
    this.backlightActive = system.toBool(options.backlightActive, true);
    this.commandByte = makeByteBuffer(1);
    this.configuredPins = [];
    this.capabilities = {
      chunks: true,
      source: true,
      reset: validPin(this.resetPin),
      backlight: validPin(this.backlightPin)
    };
    this._stats = newStats();
  }

  SPI4WireTransport.prototype.requireOpen = function (apiName) {
    if (this.state !== "open" || !this.device) {
      throw new Error(apiName + " requires an open SPI4Wire transport");
    }
    return this.device;
  };

  SPI4WireTransport.prototype.open = function () {
    var status;

    if (this.state === "open") {
      return this;
    }
    if (this.state === "closed") {
      throw new Error("cannot reopen a closed SPI4Wire transport");
    }
    if (!global.gpio || typeof global.gpio.pinMode !== "function") {
      throw new Error("SPI4Wire transport requires the gpio module");
    }
    try {
      if (configureOutput(this.dc, false, "dc")) {
        this.configuredPins.push(this.dc);
      }
      if (configureOutput(this.resetPin, true, null)) {
        this.configuredPins.push(this.resetPin);
      }
      if (configureOutput(this.backlightPin, !this.backlightActive, null)) {
        this.configuredPins.push(this.backlightPin);
      }

      if (this.suppliedDevice) {
        if (typeof this.suppliedDevice.status !== "function") {
          throw new TypeError("SPI4Wire transport device must be an SPIDevice");
        }
        status = this.suppliedDevice.status();
        if (!status.opened) {
          throw new Error("SPI4Wire transport received a closed device");
        }
        this.device = this.suppliedDevice;
        this.ownedDevice = false;
        this.bus = this.suppliedBus || null;
        this.ownedBus = false;
      } else {
        if (this.suppliedBus) {
          if (typeof this.suppliedBus.status !== "function") {
            throw new TypeError("SPI4Wire transport bus must be an SPIBus");
          }
          status = this.suppliedBus.status();
          if (!status.opened) {
            throw new Error("SPI4Wire transport received a closed bus");
          }
          this.bus = this.suppliedBus;
          this.ownedBus = false;
        } else {
          if (!global.spi || typeof global.spi.openBus !== "function") {
            throw new Error("SPI4Wire transport requires the spi module");
          }
          this.bus = global.spi.openBus(this.busOptions);
          this.ownedBus = true;
        }
        this.device = this.bus.openDevice(this.deviceOptions);
        this.ownedDevice = true;
      }
      this.state = "open";
      return this;
    } catch (error) {
      try {
        this.close();
      } catch (cleanupError) {
      }
      throw error;
    }
  };

  SPI4WireTransport.prototype.command = function (command, data) {
    var device = this.requireOpen("SPI4WireTransport.command()");
    var payload = toBytes(data);
    var started = nowUs();
    var count;

    this.commandByte[0] = command & 0xff;
    writePin(this.dc, false);
    count = device.write(this.commandByte);
    this._stats.commands += 1;
    this._stats.writes += 1;
    this._stats.chunks += 1;
    this._stats.bytes += count || 1;
    if (payload.length > 0) {
      writePin(this.dc, true);
      count = device.write(payload);
      this._stats.writes += 1;
      this._stats.chunks += 1;
      this._stats.bytes += count || payload.length;
    }
    if (started !== 0) {
      started = nowUs() - started;
      this._stats.transferUs += started;
      this._stats.totalUs += started;
    }
    return this;
  };

  SPI4WireTransport.prototype.write = function (data) {
    var device = this.requireOpen("SPI4WireTransport.write()");
    var started = nowUs();
    var count;

    writePin(this.dc, true);
    count = device.write(data);
    this._stats.writes += 1;
    this._stats.chunks += 1;
    this._stats.bytes += count || data.length || 0;
    if (started !== 0) {
      started = nowUs() - started;
      this._stats.transferUs += started;
      this._stats.totalUs += started;
    }
    return count;
  };

  SPI4WireTransport.prototype.writeChunks = function (chunks, options) {
    var device = this.requireOpen("SPI4WireTransport.writeChunks()");
    var started = nowUs();
    var result;

    writePin(this.dc, true);
    result = device.writeChunks(chunks, options || {});
    this._stats.writes += 1;
    this._stats.chunks += result.chunks || chunks.length || 0;
    this._stats.bytes += result.bytes || 0;
    this._stats.directTransfers += result.direct ? 1 : 0;
    this._stats.transferUs += result.transferUs || result.totalUs || 0;
    if (started !== 0) {
      this._stats.totalUs += nowUs() - started;
    }
    return result;
  };

  SPI4WireTransport.prototype.writeSource = function (source, options) {
    var device = this.requireOpen("SPI4WireTransport.writeSource()");
    var started = nowUs();
    var result;

    if (typeof device.writeSource !== "function") {
      throw new Error("SPIDevice.writeSource() is unavailable");
    }
    writePin(this.dc, true);
    result = device.writeSource(source, options || {});
    this._stats.writes += 1;
    this._stats.chunks += result.chunks || 0;
    this._stats.bytes += result.bytes || 0;
    this._stats.directTransfers += result.direct ? 1 : 0;
    this._stats.transferUs += result.transferUs || result.totalUs || 0;
    if (started !== 0) {
      this._stats.totalUs += nowUs() - started;
    }
    return result;
  };

  SPI4WireTransport.prototype.reset = function () {
    this.requireOpen("SPI4WireTransport.reset()");
    if (!this.capabilities.reset) {
      throw new Error("SPI4Wire transport does not manage a reset pin");
    }
    writePin(this.resetPin, true);
    delayMs(10);
    writePin(this.resetPin, false);
    delayMs(20);
    writePin(this.resetPin, true);
    delayMs(120);
    return this;
  };

  SPI4WireTransport.prototype.setBacklight = function (enabled) {
    this.requireOpen("SPI4WireTransport.setBacklight()");
    if (!this.capabilities.backlight) {
      throw new Error("SPI4Wire transport does not manage a backlight pin");
    }
    writePin(this.backlightPin,
      enabled !== false ? this.backlightActive : !this.backlightActive);
    return this;
  };

  SPI4WireTransport.prototype.stats = function () {
    return copyStats(this._stats);
  };

  SPI4WireTransport.prototype.resetStats = function () {
    this._stats = newStats();
    return this;
  };

  SPI4WireTransport.prototype.close = function () {
    var firstError = null;
    var i;

    if (this.state === "closed") {
      return true;
    }
    if (validPin(this.backlightPin) &&
        this.configuredPins.indexOf(this.backlightPin) >= 0) {
      try {
        writePin(this.backlightPin, !this.backlightActive);
      } catch (error) {
        firstError = error;
      }
    }
    if (this.device && this.ownedDevice) {
      try {
        this.device.close();
      } catch (error2) {
        if (!firstError) {
          firstError = error2;
        }
      }
    }
    if (this.bus && this.ownedBus) {
      try {
        this.bus.close();
      } catch (error3) {
        if (!firstError) {
          firstError = error3;
        }
      }
    }
    for (i = this.configuredPins.length - 1; i >= 0; i -= 1) {
      try {
        releasePin(this.configuredPins[i]);
      } catch (error4) {
        if (!firstError) {
          firstError = error4;
        }
      }
    }
    this.device = null;
    this.bus = null;
    this.ownedDevice = false;
    this.ownedBus = false;
    this.configuredPins.length = 0;
    this.state = "closed";
    if (firstError) {
      throw firstError;
    }
    return true;
  };

  display.transports.register("spi4wire", function (options) {
    return new SPI4WireTransport(options);
  });
  system.SPI4WireTransport = SPI4WireTransport;
  system.spi4WireTransportLoaded = true;
})(globalThis);
