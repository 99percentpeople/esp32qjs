test("display/lifecycle", function () {
  var events = [];
  var presentCount = 0;
  var closeCount = 0;
  var powerState = null;
  var driver;
  var screen;
  var batch;
  var stats;
  var errorText = "";
  var retryDriver;
  var retryScreen;
  var failPresent = false;
  var borrowedCloseCount = 0;
  var borrowedWrites = 0;
  var borrowedBus;
  var borrowedTransport;
  var failedOpenCloseCount = 0;
  var failedOpenScreen;

  load("_sys/display.js");

  test.equal(display.VERSION, "0.5.0", "display version should expose the layered API");
  test.ok(typeof display.Display === "function", "Display facade should be exposed");
  test.ok(typeof display.Surface === "function", "Surface renderer should be exposed");
  test.ok(typeof display.drivers.create === "function", "driver registry should be exposed");
  test.ok(typeof display.transports.create === "function", "transport registry should be exposed");

  if (!display.drivers.has("test-panel")) {
    display.drivers.register("test-panel", function (options) {
      return {
        name: "test-panel",
        width: options.width || 24,
        height: options.height || 16,
        pixelFormat: "mono1",
        layout: "linear",
        state: "created",
        capabilities: {
          partialPresent: true,
          multiRegion: true,
          power: true,
          inversion: false,
          contrast: false,
          backlight: false
        },
        open: function () {
          events.push("open");
          this.state = "open";
          return this;
        },
        present: function (frame, regions) {
          test.equal(frame.pixelFormat, "mono1", "driver should receive a frame source");
          presentCount += 1;
          events.push("present:" + regions.length);
          return {
            regions: regions.length,
            pixels: regions[0].width * regions[0].height,
            bytes: 8,
            chunks: 1,
            directTransfers: 0,
            totalUs: 1
          };
        },
        setPower: function (enabled) {
          powerState = enabled;
          return this;
        },
        close: function () {
          closeCount += 1;
          this.state = "closed";
          return true;
        }
      };
    });
  }

  driver = display.drivers.create("test-panel", { width: 24, height: 16 });
  test.equal(typeof driver.drawText, "undefined", "panel driver should not own drawing methods");
  screen = display.create(driver, {
    surface: {
      storage: "auto",
      commandBuffer: { commandCapacity: 8, textBytes: 32 }
    },
    metrics: true
  });
  test.equal(screen.state, "created", "display.create should not open hardware");
  test.equal(events.length, 0, "driver factory and Display constructor should perform no panel IO");

  screen.drawText(0, 0, "PREOPEN");
  screen.open();
  test.equal(screen.state, "open", "open should transition to open state");
  test.equal(events[0], "open", "driver open should run before first presentation");
  test.equal(presentCount, 1, "open should present the pre-rendered first frame");
  test.equal(screen.surface.getDirty(), null, "successful presentation should clear dirty state");

  screen.fillRect(1, 1, 4, 3, 1);
  screen.present([
    { x: 1, y: 1, width: 4, height: 3 },
    { x: 10, y: 2, width: 2, height: 2 }
  ], { merge: false });
  test.equal(presentCount, 2, "explicit presentation should reach the driver");
  test.equal(events[events.length - 1], "present:2", "multi-region presentation should stay separate");

  if (screen.supports("batch")) {
    batch = screen.beginBatch();
    test.ok(batch !== null, "batch capability should return a drawing target");
    batch.clear(0).drawText(0, 0, "B");
    screen.endBatch(batch);
    test.ok(batch.stats().count > 0, "surface batch should retain native command statistics");
    screen.present();
  }

  test.ok(screen.supports("power"), "driver capability should reach the Display facade");
  test.ok(!screen.supports("inversion"), "unsupported capability should remain false");
  screen.setPower(false);
  test.equal(powerState, false, "Display control should delegate to the driver");
  stats = screen.stats();
  test.ok(stats.presents >= 2, "Display stats should count presentations");
  test.ok(stats.bytes >= 16, "Display stats should aggregate driver bytes");

  try {
    display.open({ driver: "test-panel" });
  } catch (legacyError) {
    errorText = String(legacyError);
  }
  test.ok(errorText.indexOf("flat driver options") >= 0,
    "legacy flat display options should fail with a migration error");

  load("_sys/display/transports/i2c.js");
  borrowedBus = {
    status: function () { return { opened: true }; },
    write: function (address, data) {
      borrowedWrites += 1;
      return data.length;
    },
    writeChunks: function (address, chunks) {
      borrowedWrites += 1;
      return { chunks: chunks.length, bytes: chunks.length };
    },
    close: function () {
      borrowedCloseCount += 1;
      return true;
    }
  };
  borrowedTransport = display.transports.create("i2c", {
    bus: borrowedBus,
    address: 0x3c
  });
  borrowedTransport.open().command(0xae);
  borrowedTransport.write([0]);
  borrowedTransport.close();
  test.equal(borrowedWrites, 2, "borrowed I2C transport should perform framed writes");
  test.equal(borrowedCloseCount, 0, "transport close should preserve a borrowed bus");

  test.equal(screen.close(), true, "close should release the display");
  test.equal(screen.close(), true, "close should be idempotent");
  test.equal(closeCount, 1, "driver close should run exactly once");
  errorText = "";
  try {
    screen.clear();
  } catch (closedError) {
    errorText = String(closedError);
  }
  test.ok(errorText.indexOf("closed Display") >= 0,
    "drawing through a closed Display should fail clearly");

  failedOpenScreen = display.create({
    name: "failed-open-panel",
    width: 8,
    height: 8,
    pixelFormat: "mono1",
    layout: "page-y8",
    capabilities: {},
    open: function () { throw new Error("injected open failure"); },
    present: function () { throw new Error("present should not run"); },
    close: function () {
      failedOpenCloseCount += 1;
      return true;
    }
  });
  errorText = "";
  try {
    failedOpenScreen.open();
  } catch (openError) {
    errorText = String(openError);
  }
  test.ok(errorText.indexOf("injected open failure") >= 0,
    "driver open errors should propagate");
  test.equal(failedOpenScreen.state, "closed",
    "open failure should leave the Display closed");
  test.equal(failedOpenCloseCount, 1,
    "open failure should close the driver exactly once");
  test.equal(failedOpenScreen.surface.closed, true,
    "open failure should close the Surface");

  retryDriver = {
    name: "retry-panel",
    width: 8,
    height: 8,
    pixelFormat: "mono1",
    layout: "page-y8",
    capabilities: { partialPresent: true, multiRegion: true },
    open: function () { return this; },
    present: function () {
      if (failPresent) {
        throw new Error("injected present failure");
      }
      return { regions: 1, pixels: 64, bytes: 8, chunks: 1 };
    },
    close: function () { return true; }
  };
  retryScreen = display.open(retryDriver);
  retryScreen.setPixel(0, 0, 1);
  failPresent = true;
  errorText = "";
  try {
    retryScreen.present();
  } catch (presentError) {
    errorText = String(presentError);
  }
  test.ok(errorText.indexOf("injected present failure") >= 0,
    "presentation errors should propagate");
  test.ok(retryScreen.surface.getDirty() !== null,
    "failed presentation should preserve dirty state");
  failPresent = false;
  retryScreen.present();
  test.equal(retryScreen.surface.getDirty(), null,
    "successful retry should clear dirty state");
  retryScreen.close();

  return {
    events: events,
    presents: stats.presents,
    capabilities: screen.capabilities
  };
});
