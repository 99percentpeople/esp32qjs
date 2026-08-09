(function (global) {
  var VERSION = "0.5.0";
  var owns = Object.prototype.hasOwnProperty;
  var system = global.__displaySystemV2;
  var display;

  if (system && system.namespaceLoaded) {
    return;
  }

  function own(value, key) {
    return value !== null && value !== undefined && owns.call(value, key);
  }

  function assertObject(value, apiName) {
    if (!value || typeof value !== "object") {
      throw new TypeError(apiName + " expects an object");
    }
    return value;
  }

  function assertKnownOptions(options, names, apiName) {
    var allowed = {};
    var key;
    var i;

    options = options || {};
    for (i = 0; i < names.length; i += 1) {
      allowed[names[i]] = true;
    }
    for (key in options) {
      if (own(options, key) && !allowed[key]) {
        throw new TypeError(apiName + " received unknown option: " + key);
      }
    }
    return options;
  }

  function copyObject(source) {
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

  function mergeObject(base, overrides) {
    var result = copyObject(base);
    var key;

    overrides = overrides || {};
    for (key in overrides) {
      if (own(overrides, key)) {
        result[key] = overrides[key];
      }
    }
    return result;
  }

  function toBool(value, fallback) {
    if (value === undefined) {
      return fallback;
    }
    return !!value;
  }

  function assertNumber(value, apiName) {
    if (typeof value !== "number" || value !== value) {
      throw new TypeError(apiName + " expects a number");
    }
    return value;
  }

  function clampInt(value, minValue, maxValue) {
    var number = value | 0;

    if (number < minValue) {
      return minValue;
    }
    if (number > maxValue) {
      return maxValue;
    }
    return number;
  }

  function mono1(value) {
    return clampInt(assertNumber(value, "display.mono1(value)") | 0, 0, 1);
  }

  function gray4(value) {
    return clampInt(assertNumber(value, "display.gray4(value)") | 0, 0, 15);
  }

  function gray8(value) {
    return clampInt(assertNumber(value, "display.gray8(value)") | 0, 0, 255);
  }

  function rgb565(red, green, blue) {
    red = clampInt(assertNumber(red, "display.rgb565(red, green, blue)") | 0, 0, 255);
    green = clampInt(assertNumber(green, "display.rgb565(red, green, blue)") | 0, 0, 255);
    blue = clampInt(assertNumber(blue, "display.rgb565(red, green, blue)") | 0, 0, 255);
    return ((red & 0xf8) << 8) | ((green & 0xfc) << 3) | (blue >> 3);
  }

  function colorLimit(pixelFormat) {
    if (pixelFormat === "mono1") {
      return 1;
    }
    if (pixelFormat === "gray4") {
      return 15;
    }
    if (pixelFormat === "gray8") {
      return 255;
    }
    if (pixelFormat === "rgb565") {
      return 0xffff;
    }
    throw new TypeError("unsupported display pixel format: " + pixelFormat);
  }

  function normalizeColor(pixelFormat, value, fallback, apiName) {
    var limit = colorLimit(pixelFormat);
    var color;

    if (value === undefined || value === null) {
      return fallback & limit;
    }
    if (pixelFormat === "rgb565" && value && typeof value === "object") {
      return rgb565(value.r || 0, value.g || 0, value.b || 0);
    }
    if (typeof value !== "number" || value !== value) {
      throw new TypeError(apiName + " expects a packed " + pixelFormat + " color");
    }
    color = value | 0;
    if (color < 0 || color > limit) {
      throw new RangeError(apiName + " color is outside " + pixelFormat);
    }
    return color;
  }

  function styleOptions(value, apiName) {
    if (value === undefined || value === null) {
      return {};
    }
    if (typeof value !== "object") {
      throw new TypeError(apiName + " expects an options object");
    }
    return value;
  }

  function Registry(kind) {
    this.kind = kind;
    this.factories = {};
  }

  Registry.prototype.register = function (name, factory) {
    if (typeof name !== "string" || name.length === 0) {
      throw new TypeError("display." + this.kind + ".register(name, factory) expects a name");
    }
    if (typeof factory !== "function") {
      throw new TypeError("display." + this.kind + ".register(name, factory) expects a factory");
    }
    if (own(this.factories, name)) {
      throw new Error("display " + this.kind + " already registered: " + name);
    }
    this.factories[name] = factory;
    return this;
  };

  Registry.prototype.has = function (name) {
    return own(this.factories, String(name));
  };

  Registry.prototype.create = function (name, options) {
    var key = String(name);
    var factory = this.factories[key];
    var result;

    if (!factory) {
      throw new Error("display " + this.kind + " not registered: " + key);
    }
    result = factory(options || {});
    if (!result || typeof result !== "object") {
      throw new Error("display " + this.kind + " factory returned an invalid object: " + key);
    }
    return result;
  };

  Registry.prototype.list = function () {
    return Object.keys(this.factories);
  };

  display = global.display || {};
  system = {
    version: VERSION,
    namespaceLoaded: true,
    coreLoaded: false,
    own: own,
    assertObject: assertObject,
    assertKnownOptions: assertKnownOptions,
    copyObject: copyObject,
    mergeObject: mergeObject,
    toBool: toBool,
    assertNumber: assertNumber,
    clampInt: clampInt,
    mono1: mono1,
    gray4: gray4,
    gray8: gray8,
    rgb565: rgb565,
    normalizeColor: normalizeColor,
    styleOptions: styleOptions,
    Registry: Registry
  };

  display.VERSION = VERSION;
  display.mono1 = mono1;
  display.gray4 = gray4;
  display.gray8 = gray8;
  display.rgb565 = rgb565;
  display.transports = new Registry("transports");
  display.drivers = new Registry("drivers");
  display.profiles = new Registry("profiles");
  display.__loaded = false;

  global.display = display;
  global.__displaySystemV2 = system;
})(globalThis);
