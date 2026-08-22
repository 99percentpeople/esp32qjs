(function (global) {
  var system = global.__displaySystemV2;
  var display = global.display;

  if (!system || system.wlk1501spi8pProfileLoaded) {
    return;
  }

  function own(value, key) {
    return system.own(value, key);
  }

  function profileFactory(options) {
    var transportOverrides;
    var driverOverrides;
    var surfaceOverrides;
    var displayOverrides;
    var busOptions;
    var deviceOptions;
    var pins;
    var transportOptions;
    var driverOptions;
    var displayOptions;
    var transport;
    var driver;
    var maxTransferSize;

    options = system.assertKnownOptions(options || {}, [
      "transport",
      "driver",
      "surface",
      "display"
    ], "display.profiles.create(\"wlk1501spi8p\", options)");
    transportOverrides = options.transport || {};
    driverOverrides = options.driver || {};
    surfaceOverrides = options.surface || {};
    displayOverrides = options.display || {};

    system.assertKnownOptions(displayOverrides, ["present", "metrics"],
      "wlk1501spi8p display options");
    system.assertKnownOptions(transportOverrides, [
      "bus",
      "device",
      "busOptions",
      "deviceOptions",
      "pins",
      "backlightActive"
    ], "wlk1501spi8p transport options");
    busOptions = system.mergeObject({
      host: global.spi.DEFAULT_HOST,
      sclk: global.spi.DEFAULT_SCLK,
      mosi: global.spi.DEFAULT_MOSI,
      miso: -1,
      maxTransferSize: 16384
    }, transportOverrides.busOptions || {});
    deviceOptions = system.mergeObject({
      cs: global.spi.DEFAULT_CS >= 0 ? global.spi.DEFAULT_CS : 2,
      mode: 0,
      freqHz: 20000000,
      queueSize: 2
    }, transportOverrides.deviceOptions || {});
    pins = system.mergeObject({
      dc: 4,
      reset: 5,
      backlight: 6
    }, transportOverrides.pins || {});
    transportOptions = {
      busOptions: busOptions,
      deviceOptions: deviceOptions,
      pins: pins,
      backlightActive: own(transportOverrides, "backlightActive")
        ? transportOverrides.backlightActive
        : true
    };
    if (own(transportOverrides, "bus")) {
      transportOptions.bus = transportOverrides.bus;
    }
    if (own(transportOverrides, "device")) {
      transportOptions.device = transportOverrides.device;
    }
    transport = display.transports.create("spi4wire", transportOptions);

    driverOptions = system.mergeObject({
      width: 240,
      height: 240,
      rotation: 0,
      columnOffset: 0,
      rowOffset: 0,
      bgr: false,
      inverted: true
    }, driverOverrides);
    driverOptions.transport = transport;
    driver = display.drivers.create("st7789", driverOptions);

    maxTransferSize = own(busOptions, "maxTransferSize")
      ? busOptions.maxTransferSize
      : 16384;
    displayOptions = system.mergeObject(displayOverrides, {
      surface: system.mergeObject({
        storage: "dma",
        fallbackStorage: "auto",
        chunkBytes: maxTransferSize,
        commandBuffer: {
          commandCapacity: 192,
          textBytes: 2048
        }
      }, surfaceOverrides),
      profileName: "wlk1501spi8p"
    });
    return {
      driver: driver,
      options: displayOptions
    };
  }

  display.profiles.register("wlk1501spi8p", profileFactory);
  system.wlk1501spi8pProfileLoaded = true;
})(globalThis);
