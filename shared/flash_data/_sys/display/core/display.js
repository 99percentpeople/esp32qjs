(function (global) {
  var system = global.__displaySystemV2;
  var display = global.display;
  var DRAW_METHODS = [
    "clear",
    "fill",
    "setPixel",
    "fillRect",
    "drawCircle",
    "fillCircle",
    "drawEllipse",
    "fillEllipse",
    "drawLine",
    "drawRect",
    "drawRoundRect",
    "fillRoundRect",
    "drawPolyline",
    "drawPolygon",
    "fillPolygon",
    "drawTriangle",
    "fillTriangle",
    "drawQuadraticBezier",
    "drawCubicBezier",
    "drawBitmap",
    "drawChar",
    "drawText"
  ];

  if (!system || system.displayLoaded) {
    return;
  }

  function own(value, key) {
    return system.own(value, key);
  }

  function nowUs() {
    if (global.esp32 && typeof global.esp32.micros === "function") {
      return global.esp32.micros();
    }
    return 0;
  }

  function validateDriver(driver) {
    var requiredMethods = ["open", "present", "close"];
    var i;

    if (!driver || typeof driver !== "object") {
      throw new TypeError(
        "display.open() now expects a PanelDriver; create one with display.drivers.create()"
      );
    }
    if (typeof driver.driver === "string" && typeof driver.present !== "function") {
      throw new TypeError(
        "display.open() no longer accepts flat driver options; " +
        "create a transport and PanelDriver first"
      );
    }
    if (typeof driver.name !== "string" || driver.name.length === 0) {
      throw new TypeError("PanelDriver.name must be a non-empty string");
    }
    if ((driver.width | 0) <= 0 || (driver.height | 0) <= 0) {
      throw new RangeError("PanelDriver dimensions must be positive");
    }
    if (typeof driver.pixelFormat !== "string" || driver.pixelFormat.length === 0) {
      throw new TypeError("PanelDriver.pixelFormat is required");
    }
    for (i = 0; i < requiredMethods.length; i += 1) {
      if (typeof driver[requiredMethods[i]] !== "function") {
        throw new TypeError("PanelDriver must implement " + requiredMethods[i] + "()");
      }
    }
    return driver;
  }

  function combineCapabilities(driver, surface) {
    var combined = {};
    var source = driver.capabilities || {};
    var key;

    for (key in source) {
      if (own(source, key)) {
        combined[key] = !!source[key];
      }
    }
    combined.batch = surface.commandBufferEnabled &&
      !!surface.nativeBuffer &&
      typeof surface.nativeBuffer.createCommandBuffer === "function";
    combined.directSource = !!(
      driver.transport &&
      driver.transport.capabilities &&
      driver.transport.capabilities.source
    );
    return combined;
  }

  function Display(driver, options) {
    var metadata;

    driver = validateDriver(driver);
    options = system.assertKnownOptions(options || {}, [
      "surface",
      "present",
      "metrics",
      "profileName"
    ], "new display.Display(driver, options)");
    metadata = {
      width: driver.width,
      height: driver.height,
      pixelFormat: driver.pixelFormat,
      layout: driver.layout || (driver.pixelFormat === "mono1" ? "page-y8" : "linear")
    };

    this.driver = driver;
    this.driverName = driver.name;
    this.profileName = own(options, "profileName") ? String(options.profileName) : null;
    this.surface = new display.Surface(metadata, options.surface || {});
    this.width = this.surface.width;
    this.height = this.surface.height;
    this.pixelFormat = this.surface.pixelFormat;
    this.foreground = this.surface.foreground;
    this.background = this.surface.background;
    this.presentOptions = system.copyObject(system.assertKnownOptions(
      options.present || {},
      ["merge", "mergeCoverage", "mergeAreaRatio", "mergePixelBudget", "queueDepth"],
      "new display.Display(driver, options).present"
    ));
    this.metricsEnabled = options.metrics === true;
    this.capabilities = combineCapabilities(driver, this.surface);
    this.state = "created";
    this.ready = false;
    this._stats = system.newDisplayStats();
  }

  Display.prototype.requireUsable = function (apiName) {
    if (this.state === "closed" || this.state === "closing") {
      throw new Error("cannot use a closed Display in " + apiName);
    }
    return this;
  };

  Display.prototype.requireOpen = function (apiName) {
    this.requireUsable(apiName);
    if (this.state !== "open") {
      throw new Error(apiName + " requires an open Display");
    }
    return this;
  };

  Display.prototype.open = function () {
    var openError;

    this.requireUsable("Display.open()");
    if (this.state === "open") {
      return this;
    }
    if (this.state !== "created") {
      throw new Error("Display.open() cannot run while state is " + this.state);
    }
    this.state = "opening";
    try {
      this.driver.open();
      this.state = "open";
      this.ready = true;
      this.flush();
      return this;
    } catch (error) {
      openError = error;
      try {
        this.close();
      } catch (cleanupError) {
      }
      throw openError;
    }
  };

  Display.prototype.present = function (regions, options) {
    var settings;
    var normalized;
    var started = 0;
    var result;

    this.requireOpen("Display.present()");
    options = system.assertKnownOptions(options || {}, [
      "merge",
      "mergeCoverage",
      "mergeAreaRatio",
      "mergePixelBudget",
      "queueDepth"
    ], "Display.present(regions, options)");
    settings = system.mergeObject(this.presentOptions, options);
    settings.metrics = this.metricsEnabled;
    normalized = system.normalizeRegions(this.surface, this.driver, regions, settings);
    if (normalized.length === 0) {
      return this;
    }
    if (this.metricsEnabled) {
      started = nowUs();
    }
    result = this.driver.present(this.surface.frame, normalized, settings) || {};
    if (this.metricsEnabled && !own(result, "totalUs") && started !== 0) {
      result.totalUs = nowUs() - started;
    }
    system.addDisplayStats(this._stats, result, normalized);
    this.surface.clearDirty();
    return this;
  };

  Display.prototype.flush = function () {
    return this.present(system.fullRegion(this.surface), { merge: false });
  };

  Display.prototype.flushRect = function (x, y, width, height) {
    return this.present({ x: x, y: y, width: width, height: height }, { merge: false });
  };

  Display.prototype.flushRects = function (regions, options) {
    return this.present(regions, options);
  };

  Display.prototype.beginBatch = function (options) {
    this.requireUsable("Display.beginBatch()");
    return this.surface.beginBatch(options);
  };

  Display.prototype.endBatch = function (batch) {
    this.requireUsable("Display.endBatch()");
    this.surface.endBatch(batch);
    return this;
  };

  Display.prototype.getPixel = function (x, y) {
    this.requireUsable("Display.getPixel()");
    return this.surface.getPixel(x, y);
  };

  Display.prototype.measureText = function (text, style) {
    this.requireUsable("Display.measureText()");
    return this.surface.measureText(text, style);
  };

  Display.prototype.supports = function (name) {
    return !!this.capabilities[String(name)];
  };

  Display.prototype.requireCapability = function (name, apiName) {
    this.requireOpen(apiName);
    if (!this.supports(name)) {
      throw new Error(apiName + " is not supported by driver " + this.driverName);
    }
    return this;
  };

  Display.prototype.setPower = function (enabled) {
    this.requireCapability("power", "Display.setPower()");
    this.driver.setPower(enabled !== false);
    return this;
  };

  Display.prototype.setInverted = function (enabled) {
    this.requireCapability("inversion", "Display.setInverted()");
    this.driver.setInverted(enabled !== false);
    return this;
  };

  Display.prototype.setContrast = function (value) {
    this.requireCapability("contrast", "Display.setContrast()");
    this.driver.setContrast(value);
    return this;
  };

  Display.prototype.setBacklight = function (enabled) {
    this.requireCapability("backlight", "Display.setBacklight()");
    this.driver.setBacklight(enabled !== false);
    return this;
  };

  Display.prototype.stats = function () {
    var result = system.copyDisplayStats(this._stats);

    result.enabled = this.metricsEnabled;
    result.driver = typeof this.driver.stats === "function" ? this.driver.stats() : {};
    result.transport = this.driver.transport && typeof this.driver.transport.stats === "function"
      ? this.driver.transport.stats()
      : {};
    return result;
  };

  Display.prototype.resetStats = function () {
    this._stats = system.newDisplayStats();
    if (typeof this.driver.resetStats === "function") {
      this.driver.resetStats();
    }
    if (this.driver.transport && typeof this.driver.transport.resetStats === "function") {
      this.driver.transport.resetStats();
    }
    return this;
  };

  Display.prototype.close = function () {
    var firstError = null;
    var wasOpen;

    if (this.state === "closed") {
      return true;
    }
    wasOpen = this.state === "open";
    this.state = "closing";
    this.ready = false;
    if (wasOpen && this.supports("backlight") && typeof this.driver.setBacklight === "function") {
      try {
        this.driver.setBacklight(false);
      } catch (error) {
        firstError = error;
      }
    }
    try {
      this.driver.close();
    } catch (error2) {
      if (!firstError) {
        firstError = error2;
      }
    }
    if (this.driver.transport && typeof this.driver.transport.close === "function") {
      try {
        this.driver.transport.close();
      } catch (error3) {
        if (!firstError) {
          firstError = error3;
        }
      }
    }
    try {
      this.surface.close();
    } catch (error4) {
      if (!firstError) {
        firstError = error4;
      }
    }
    this.state = "closed";
    if (firstError) {
      throw firstError;
    }
    return true;
  };

  function installDrawMethod(name) {
    Display.prototype[name] = function () {
      this.requireUsable("Display." + name + "()");
      this.surface[name].apply(this.surface, arguments);
      return this;
    };
  }

  var i;
  for (i = 0; i < DRAW_METHODS.length; i += 1) {
    installDrawMethod(DRAW_METHODS[i]);
  }

  display.Display = /** @type {typeof ESP32QJS.Display} */ (
    /** @type {unknown} */ (Display)
  );
  display.create = function (driver, options) {
    return new Display(driver, options || {});
  };
  display.open = function (driver, options) {
    return display.create(driver, options).open();
  };

  (function () {
    var registryCreate = display.profiles.create;

    display.profiles.create = /** @type {ESP32QJS.DisplayProfileRegistry["create"]} */ (function (name, options) {
      var spec = registryCreate.call(this, name, options || {});

      if (spec instanceof Display) {
        return spec;
      }
      if (!spec.driver) {
        throw new Error("display profile must return { driver, options }");
      }
      return display.create(spec.driver, spec.options || {});
    });
    display.profiles.open = function (name, options) {
      return this.create(name, options).open();
    };
  })();

  system.Display = Display;
  system.displayLoaded = true;
})(globalThis);
