(function (global) {
  var system = global.__displaySystemV2;
  var display = global.display;

  if (!system || system.i2cTransportLoaded) {
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

  function bytesWithPrefix(prefix, first, second) {
    var result = [prefix & 0xff];
    var values;
    var i;

    if (first !== undefined && first !== null) {
      values = typeof first === "number" ? [first] : first;
      for (i = 0; i < values.length; i += 1) {
        result.push(values[i] & 0xff);
      }
    }
    if (second !== undefined && second !== null) {
      values = typeof second === "number" ? [second] : second;
      for (i = 0; i < values.length; i += 1) {
        result.push(values[i] & 0xff);
      }
    }
    return result;
  }

  function newStats() {
    return {
      commands: 0,
      writes: 0,
      chunks: 0,
      bytes: 0,
      totalUs: 0
    };
  }

  function copyStats(stats) {
    return {
      commands: stats.commands,
      writes: stats.writes,
      chunks: stats.chunks,
      bytes: stats.bytes,
      totalUs: stats.totalUs
    };
  }

  function I2CTransport(options) {
    options = system.assertKnownOptions(options || {}, [
      "bus",
      "busOptions",
      "address",
      "commandPrefix",
      "dataPrefix"
    ], "display.transports.create(\"i2c\", options)");
    this.kind = "i2c";
    this.state = "created";
    this.suppliedBus = options.bus || null;
    this.bus = null;
    this.device = null;
    this.ownedBus = false;
    this.busOptions = system.copyObject(options.busOptions || {});
    this.address = own(options, "address") ? options.address | 0 : 0x3c;
    this.commandPrefix = own(options, "commandPrefix") ? options.commandPrefix | 0 : 0x00;
    this.dataPrefix = own(options, "dataPrefix") ? options.dataPrefix | 0 : 0x40;
    if (this.address < 0x03 || this.address > 0x77) {
      throw new RangeError("I2C display address must be in 0x03..0x77");
    }
    this.capabilities = {
      chunks: true,
      source: false,
      reset: false,
      backlight: false
    };
    this._stats = newStats();
  }

  I2CTransport.prototype.requireOpen = function (apiName) {
    if (this.state !== "open" || !this.device) {
      throw new Error(apiName + " requires an open I2C display transport");
    }
    return this.device;
  };

  I2CTransport.prototype.open = function () {
    var status;

    if (this.state === "open") {
      return this;
    }
    if (this.state === "closed") {
      throw new Error("cannot reopen a closed I2C display transport");
    }
    if (this.suppliedBus) {
      if (typeof this.suppliedBus.status !== "function") {
        throw new TypeError("I2C transport bus must be an I2CBus");
      }
      status = this.suppliedBus.status();
      if (!status.opened) {
        throw new Error("I2C transport received a closed bus");
      }
      this.bus = this.suppliedBus;
      this.ownedBus = false;
    } else {
      if (!global.i2c || typeof global.i2c.openBus !== "function") {
        throw new Error("I2C display transport requires the i2c module");
      }
      this.bus = global.i2c.openBus(this.busOptions);
      this.ownedBus = true;
    }
    try {
      this.device = this.bus.openDevice({ address: this.address });
    } catch (error) {
      if (this.ownedBus) {
        try {
          this.bus.close();
        } catch (closeError) {
        }
      }
      this.bus = null;
      this.ownedBus = false;
      throw error;
    }
    this.state = "open";
    return this;
  };

  I2CTransport.prototype.command = function (command, data) {
    var device = this.requireOpen("I2CTransport.command()");
    var payload = bytesWithPrefix(this.commandPrefix, command, data);
    var started = nowUs();
    var result = device.write(payload);

    this._stats.commands += 1;
    this._stats.writes += 1;
    this._stats.chunks += 1;
    this._stats.bytes += payload.length;
    if (started !== 0) {
      this._stats.totalUs += nowUs() - started;
    }
    return this;
  };

  I2CTransport.prototype.write = function (data) {
    var device = this.requireOpen("I2CTransport.write()");
    var body = typeof data === "number" ? [data] : data;
    var byteLength = body && typeof body.byteLength === "number"
      ? body.byteLength : body.length;
    var started = nowUs();
    var result = device.writeSegments([[this.dataPrefix & 0xff], body]);

    this._stats.writes += 1;
    this._stats.chunks += 1;
    this._stats.bytes += byteLength + 1;
    if (started !== 0) {
      this._stats.totalUs += nowUs() - started;
    }
    return result;
  };

  I2CTransport.prototype.writeChunks = function (chunks) {
    var device = this.requireOpen("I2CTransport.writeChunks()");
    var payloads = [];
    var bytes = 0;
    var started = nowUs();
    var result;
    var i;

    for (i = 0; i < chunks.length; i += 1) {
      payloads.push(bytesWithPrefix(this.dataPrefix, chunks[i]));
      bytes += payloads[payloads.length - 1].length;
    }
    result = device.writeBatch(payloads);
    this._stats.writes += 1;
    this._stats.chunks += result.chunks || payloads.length;
    this._stats.bytes += result.bytes || bytes;
    if (started !== 0) {
      this._stats.totalUs += nowUs() - started;
    }
    return result;
  };

  I2CTransport.prototype.writeSource = function () {
    throw new Error("I2C display transport does not support span sources");
  };

  I2CTransport.prototype.reset = function () {
    throw new Error("I2C display transport does not manage a reset pin");
  };

  I2CTransport.prototype.setBacklight = function () {
    throw new Error("I2C display transport does not manage a backlight pin");
  };

  I2CTransport.prototype.stats = function () {
    return copyStats(this._stats);
  };

  I2CTransport.prototype.resetStats = function () {
    this._stats = newStats();
    return this;
  };

  I2CTransport.prototype.close = function () {
    var firstError = null;

    if (this.state === "closed") {
      return true;
    }
    if (this.device) {
      try {
        this.device.close();
      } catch (error) {
        firstError = error;
      }
    }
    if (this.bus && this.ownedBus) {
      try {
        this.bus.close();
      } catch (busCloseError) {
        if (!firstError) {
          firstError = busCloseError;
        }
      }
    }
    this.device = null;
    this.bus = null;
    this.ownedBus = false;
    this.state = "closed";
    if (firstError) {
      throw firstError;
    }
    return true;
  };

  display.transports.register("i2c", function (options) {
    return new I2CTransport(options);
  });
  system.I2CTransport = I2CTransport;
  system.i2cTransportLoaded = true;
})(globalThis);
