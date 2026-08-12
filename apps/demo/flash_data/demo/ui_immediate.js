load("_sys/display/wlk1501spi8p.js");
load("_sys/ui.js");
load("_sys/ui/control.js");
load("_sys/ui/fps.js");

var CONFIG = globalThis.uiDemoConfig || {};
var owns = Object.prototype.hasOwnProperty;
var TRANSFER_BYTES = CONFIG.transferBytes || 16384;

function own(obj, key) {
  return obj !== null && obj !== undefined && owns.call(obj, key);
}

function mergeOptions(defaults, overrides) {
  var result = {};
  var key;

  defaults = defaults || {};
  overrides = overrides || {};
  for (key in defaults) {
    if (own(defaults, key)) {
      result[key] = defaults[key];
    }
  }
  for (key in overrides) {
    if (own(overrides, key)) {
      result[key] = overrides[key];
    }
  }
  return result;
}

function makeDisplayOptions() {
  var base = CONFIG.display || globalThis.displayConfig || {};
  var transport = base.transport || {};
  var deviceDefaults = {
    freqHz: CONFIG.freqHz || 80000000,
    queueSize: CONFIG.queueSize === undefined ? 2 : CONFIG.queueSize
  };
  var options = {
    transport: {
      busOptions: mergeOptions({ maxTransferSize: TRANSFER_BYTES }, transport.busOptions),
      deviceOptions: mergeOptions(deviceDefaults, transport.deviceOptions),
      pins: mergeOptions({}, transport.pins)
    },
    driver: mergeOptions({}, base.driver),
    surface: mergeOptions({ chunkBytes: TRANSFER_BYTES }, base.surface),
    display: mergeOptions({ metrics: true }, base.display)
  };

  if (own(transport, "bus")) {
    options.transport.bus = transport.bus;
  }
  if (own(transport, "device")) {
    options.transport.device = transport.device;
  }
  if (own(transport, "backlightActive")) {
    options.transport.backlightActive = transport.backlightActive;
  }
  return options;
}

var screen = display.profiles.open("wlk1501spi8p", makeDisplayOptions());
var wifiEnabled = true;
var showFps = true;
var brightness = 45;
var mode = 0;
var selected = 0;
var frame = 0;
var timer = null;
var items = ["Status", "Network", "Sensors", "Power"];
var tabs = ["Main", "Net", "Info"];
var rootOptions = { padding: 6, gap: 4 };
var statusOptions = {
  left: "ESP32",
  title: "Immediate UI",
  right: "RUN"
};
var panelOptions = { height: 116, padding: 5, gap: 4 };
var panelRowOptions = { height: 14, gap: 4 };
var indicatorOptions = { id: "input", label: "IN", width: 74, height: 14 };
var fpsOptions = { enabled: true, sampleMs: 1000, precision: 1, width: 72 };
var sliderOptions = { min: 0, max: 100, step: 5 };
var progressOptions = { min: 0, max: 100, height: 12 };
var menuOptions = { visibleCount: 2, height: 46 };
var frameDelayMs = CONFIG.frameDelayMs === undefined ? 0 : CONFIG.frameDelayMs;
var lastFrameUs = 0;
var lastFps = 0;
var lastTiming = {
  beginUs: 0,
  bodyUs: 0,
  endFrameUs: 0,
  totalUs: 0,
  profileTiming: false,
  sections: {},
  panel: {}
};
var profileTimingActive = false;
var profilePanelActive = false;
var profileMarks = null;

function nowUs() {
  if (globalThis.sys && typeof globalThis.sys.micros === "function") {
    return globalThis.sys.micros();
  }
  if (typeof Date !== "undefined" && Date && typeof Date.now === "function") {
    return Date.now() * 1000;
  }
  return 0;
}

function copyOption(target, source, key) {
  if (source && own(source, key)) {
    target[key] = source[key];
  }
}

function applyBeginOverrides(beginOptions, options) {
  var keys = [
    "dirtyMerge",
    "dirtyMergeCoverage",
    "dirtyMergeAreaRatio",
    "dirtyMergePixelBudget",
    "dirtyMaxRects"
  ];
  var i;

  if (own(CONFIG, "dirty")) {
    beginOptions.dirty = CONFIG.dirty;
  }
  if (own(options, "dirty")) {
    beginOptions.dirty = options.dirty;
  }
  for (i = 0; i < keys.length; i += 1) {
    copyOption(beginOptions, CONFIG, keys[i]);
    copyOption(beginOptions, options, keys[i]);
  }
}

function renderPanelRow() {
  if (profilePanelActive) {
    profileMarks.afterPanelRowUs = nowUs();
  }
  ui.control.indicator(indicatorOptions);
  if (profilePanelActive) {
    profileMarks.afterPanelIndicatorUs = nowUs();
  }
  fpsOptions.enabled = showFps;
  lastFps = ui.fps("fps", fpsOptions);
  if (profilePanelActive) {
    profileMarks.afterPanelFpsUs = nowUs();
  }
}

function renderPanel() {
  if (profilePanelActive) {
    profileMarks.afterPanelOpenUs = nowUs();
  }
  ui.value("Mode", mode === 0 ? "Main" : (mode === 1 ? "Net" : "Info"));
  if (profilePanelActive) {
    profileMarks.afterPanelValueUs = nowUs();
  }
  wifiEnabled = ui.toggle("wifi", "WiFi", wifiEnabled);
  if (profilePanelActive) {
    profileMarks.afterPanelWifiUs = nowUs();
  }
  showFps = ui.toggle("showFps", "FPS", showFps);
  if (profilePanelActive) {
    profileMarks.afterPanelFpsToggleUs = nowUs();
  }
  ui.row(renderPanelRow, panelRowOptions);
  if (profilePanelActive) {
    profileMarks.afterPanelRowEndUs = nowUs();
  }
}

function renderRoot() {
  statusOptions.right = ui.control.last() || "RUN";
  ui.statusBar(statusOptions);
  mode = ui.tabs("mode", tabs, mode);
  if (profileTimingActive) {
    profileMarks.afterHeaderUs = nowUs();
  }
  if (profilePanelActive) {
    profileMarks.panelStartUs = profileMarks.afterHeaderUs;
  }
  ui.panel(renderPanel, panelOptions);
  if (profileTimingActive) {
    profileMarks.afterPanelUs = nowUs();
  }
  brightness = ui.slider("brightness", brightness, sliderOptions);
  ui.progress("activity", frame % 101, progressOptions);
  if (profileTimingActive) {
    profileMarks.afterProgressUs = nowUs();
  }
  selected = ui.menu("menu", items, selected, menuOptions);
  ui.softkeys("Back", "OK", "Next");
  if (profileTimingActive) {
    profileMarks.afterMenuUs = nowUs();
  }
}

function drawFrame(options) {
  var startUs = nowUs();
  var afterBeginUs;
  var afterHeaderUs;
  var afterPanelUs;
  var afterProgressUs;
  var afterMenuUs;
  var beforeEndUs;
  var afterEndUs;
  var panelStartUs;
  var afterPanelOpenUs;
  var afterPanelValueUs;
  var afterPanelWifiUs;
  var afterPanelFpsToggleUs;
  var afterPanelRowUs;
  var afterPanelIndicatorUs;
  var afterPanelFpsUs;
  var afterPanelRowEndUs;
  var beginOptions;
  var profileTiming;
  var profilePanel;

  options = options || {};
  profileTiming = options.profileTiming === true || CONFIG.profileTiming === true;
  profilePanel = profileTiming && (options.profilePanel === true || CONFIG.profilePanel === true);
  profileTimingActive = profileTiming;
  profilePanelActive = profilePanel;
  profileMarks = profileTiming ? {} : null;
  beginOptions = {
    clear: own(options, "clear") ? !!options.clear : frame === 0,
    partial: options.partial !== false,
    flush: options.flush !== false,
    input: ui.control.read()
  };
  if (own(options, "batch")) {
    beginOptions.batch = options.batch;
  }
  applyBeginOverrides(beginOptions, options);
  ui.begin(screen, beginOptions);
  if (profileTiming) {
    afterBeginUs = nowUs();
  }

  ui.column(renderRoot, rootOptions);
  if (profileTiming) {
    afterHeaderUs = profileMarks.afterHeaderUs;
    afterPanelUs = profileMarks.afterPanelUs;
    afterProgressUs = profileMarks.afterProgressUs;
    afterMenuUs = profileMarks.afterMenuUs;
  }
  if (profilePanel) {
    panelStartUs = profileMarks.panelStartUs;
    afterPanelOpenUs = profileMarks.afterPanelOpenUs;
    afterPanelValueUs = profileMarks.afterPanelValueUs;
    afterPanelWifiUs = profileMarks.afterPanelWifiUs;
    afterPanelFpsToggleUs = profileMarks.afterPanelFpsToggleUs;
    afterPanelRowUs = profileMarks.afterPanelRowUs;
    afterPanelIndicatorUs = profileMarks.afterPanelIndicatorUs;
    afterPanelFpsUs = profileMarks.afterPanelFpsUs;
    afterPanelRowEndUs = profileMarks.afterPanelRowEndUs;
  }
  if (profileTiming) {
    beforeEndUs = nowUs();
  }
  ui.endFrame();
  afterEndUs = nowUs();
  profileMarks = null;
  profilePanelActive = false;
  profileTimingActive = false;

  frame += 1;
  if (!profileTiming) {
    lastTiming = {
      beginUs: 0,
      bodyUs: 0,
      endFrameUs: 0,
      totalUs: afterEndUs - startUs,
      profileTiming: false,
      sections: {
        headerUs: 0,
        panelUs: 0,
        progressUs: 0,
        menuUs: 0,
        closeUs: 0
      },
      panel: {
        openUs: 0,
        valueUs: 0,
        wifiUs: 0,
        fpsToggleUs: 0,
        rowUs: 0,
        indicatorUs: 0,
        fpsUs: 0,
        rowEndUs: 0,
        afterRowUs: 0
      }
    };
    lastFrameUs = lastTiming.totalUs;
    return;
  }
  lastTiming = {
    beginUs: afterBeginUs - startUs,
    bodyUs: beforeEndUs - afterBeginUs,
    endFrameUs: afterEndUs - beforeEndUs,
    totalUs: afterEndUs - startUs,
    profileTiming: true,
    sections: {
      headerUs: afterHeaderUs - afterBeginUs,
      panelUs: afterPanelUs - afterHeaderUs,
      progressUs: afterProgressUs - afterPanelUs,
      menuUs: afterMenuUs - afterProgressUs,
      closeUs: beforeEndUs - afterMenuUs
    },
    panel: profilePanel ? {
      openUs: afterPanelOpenUs - panelStartUs,
      valueUs: afterPanelValueUs - afterPanelOpenUs,
      wifiUs: afterPanelWifiUs - afterPanelValueUs,
      fpsToggleUs: afterPanelFpsToggleUs - afterPanelWifiUs,
      rowUs: afterPanelRowUs - afterPanelFpsToggleUs,
      indicatorUs: afterPanelIndicatorUs - afterPanelRowUs,
      fpsUs: afterPanelFpsUs - afterPanelIndicatorUs,
      rowEndUs: afterPanelRowEndUs - afterPanelFpsUs,
      afterRowUs: afterPanelUs - afterPanelRowEndUs
    } : {
      openUs: 0,
      valueUs: 0,
      wifiUs: 0,
      fpsToggleUs: 0,
      rowUs: 0,
      indicatorUs: 0,
      fpsUs: 0,
      rowEndUs: 0,
      afterRowUs: 0
    }
  };
  lastFrameUs = lastTiming.totalUs;
}

function runFrame() {
  timer = null;
  drawFrame();
  if (timer === null) {
    timer = setTimeout(runFrame, frameDelayMs);
  }
}

function stop() {
  if (timer !== null) {
    clearTimeout(timer);
    timer = null;
  }
  print("[ui:demo] stopped");
}

function start() {
  if (timer === null) {
    timer = setTimeout(runFrame, 0);
  }
}

function benchmark(count, options) {
  var wasRunning = timer !== null;
  var startUs;
  var elapsedUs;
  var beginUs = 0;
  var bodyUs = 0;
  var endFrameUs = 0;
  var sectionSums = {
    headerUs: 0,
    panelUs: 0,
    progressUs: 0,
    menuUs: 0,
    closeUs: 0
  };
  var panelSums = {
    openUs: 0,
    valueUs: 0,
    wifiUs: 0,
    fpsToggleUs: 0,
    rowUs: 0,
    indicatorUs: 0,
    fpsUs: 0,
    rowEndUs: 0,
    afterRowUs: 0
  };
  var i;

  count = count === undefined ? 20 : count | 0;
  if (count <= 0) {
    count = 1;
  }
  options = options || {};
  if (wasRunning) {
    clearTimeout(timer);
    timer = null;
  }
  screen.resetStats();
  startUs = nowUs();
  for (i = 0; i < count; i += 1) {
    drawFrame(options);
    beginUs += lastTiming.beginUs;
    bodyUs += lastTiming.bodyUs;
    endFrameUs += lastTiming.endFrameUs;
    sectionSums.headerUs += lastTiming.sections.headerUs;
    sectionSums.panelUs += lastTiming.sections.panelUs;
    sectionSums.progressUs += lastTiming.sections.progressUs;
    sectionSums.menuUs += lastTiming.sections.menuUs;
    sectionSums.closeUs += lastTiming.sections.closeUs;
    panelSums.openUs += lastTiming.panel.openUs;
    panelSums.valueUs += lastTiming.panel.valueUs;
    panelSums.wifiUs += lastTiming.panel.wifiUs;
    panelSums.fpsToggleUs += lastTiming.panel.fpsToggleUs;
    panelSums.rowUs += lastTiming.panel.rowUs;
    panelSums.indicatorUs += lastTiming.panel.indicatorUs;
    panelSums.fpsUs += lastTiming.panel.fpsUs;
    panelSums.rowEndUs += lastTiming.panel.rowEndUs;
    panelSums.afterRowUs += lastTiming.panel.afterRowUs;
  }
  elapsedUs = nowUs() - startUs;
  if (wasRunning) {
    timer = setTimeout(runFrame, frameDelayMs);
  }
  return {
    frames: count,
    totalUs: elapsedUs,
    avgFrameUs: elapsedUs / count,
    fps: count * 1000000 / elapsedUs,
    lastFrameUs: lastFrameUs,
    timing: {
      beginUs: beginUs / count,
      bodyUs: bodyUs / count,
      endFrameUs: endFrameUs / count,
      sections: {
        headerUs: sectionSums.headerUs / count,
        panelUs: sectionSums.panelUs / count,
        progressUs: sectionSums.progressUs / count,
        menuUs: sectionSums.menuUs / count,
        closeUs: sectionSums.closeUs / count
      },
      panel: {
        openUs: panelSums.openUs / count,
        valueUs: panelSums.valueUs / count,
        wifiUs: panelSums.wifiUs / count,
        fpsToggleUs: panelSums.fpsToggleUs / count,
        rowUs: panelSums.rowUs / count,
        indicatorUs: panelSums.indicatorUs / count,
        fpsUs: panelSums.fpsUs / count,
        rowEndUs: panelSums.rowEndUs / count,
        afterRowUs: panelSums.afterRowUs / count
      }
    },
    perf: screen.stats(),
    command: screen.surface.commandBuffer && screen.surface.commandBuffer.stats
      ? screen.surface.commandBuffer.stats()
      : null
  };
}

globalThis.uiDemo = {
  screen: screen,
  control: ui.control,
  start: start,
  stop: stop,
  drawFrame: drawFrame,
  benchmark: benchmark,
  press: function (command) {
    return ui.control.press(command);
  },
  encoder: function (delta) {
    return ui.control.encoder(delta);
  },
  setFrameDelay: function (delayMs) {
    frameDelayMs = delayMs | 0;
    if (frameDelayMs < 0) {
      frameDelayMs = 0;
    }
    return frameDelayMs;
  },
  stats: function () {
    return {
      frame: frame,
      frameDelayMs: frameDelayMs,
      lastFrameUs: lastFrameUs,
      timing: lastTiming,
      fps: lastFps
    };
  }
};

timer = setTimeout(runFrame, 0);
print("[ui:demo] running",
      "press=ui.control.press(\"down\")",
      "encoder=ui.control.encoder(1)",
      "stats=uiDemo.stats()",
      "stop=uiDemo.stop()");
