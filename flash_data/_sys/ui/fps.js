(function (global) {
  var owns = Object.prototype.hasOwnProperty;
  var ui = global.ui;
  var states = {};

  if (!ui || !ui.__loaded) {
    throw new Error("load(\"_sys/ui.js\") before load(\"_sys/ui/fps.js\")");
  }
  if (ui.__fpsLoaded) {
    return;
  }

  function own(obj, key) {
    return obj !== null && obj !== undefined && owns.call(obj, key);
  }

  var textOptionKeys = [
    "x",
    "y",
    "width",
    "height",
    "padding",
    "align",
    "background",
    "border",
    "borderColor",
    "radius",
    "borderRadius",
    "color",
    "font",
    "spacing",
    "textAlign",
    "valign",
    "outline",
    "outlineColor",
    "outlineWidth",
    "style"
  ];

  function copyTextOptions(source) {
    var target = {};
    var i;
    var key;

    source = source || {};
    for (i = 0; i < textOptionKeys.length; i += 1) {
      key = textOptionKeys[i];
      if (own(source, key)) {
        target[key] = source[key];
      }
    }
    return target;
  }

  function nowUs() {
    if (global.esp32 && typeof global.esp32.micros === "function") {
      return global.esp32.micros();
    }
    if (typeof Date !== "undefined" && Date && typeof Date.now === "function") {
      return Date.now() * 1000;
    }
    return 0;
  }

  function formatFixed(value, precision) {
    var scale = 1;
    var rounded;
    var whole;
    var fraction;
    var i;

    precision = precision | 0;
    if (precision <= 0) {
      return String(Math.round(value));
    }
    for (i = 0; i < precision; i += 1) {
      scale *= 10;
    }
    rounded = Math.round(value * scale);
    whole = (rounded / scale) | 0;
    fraction = String(rounded - whole * scale);
    while (fraction.length < precision) {
      fraction = "0" + fraction;
    }
    return String(whole) + "." + fraction;
  }

  function stateFor(id) {
    if (!states[id]) {
      states[id] = {
        started: false,
        startUs: 0,
        frames: 0,
        value: 0,
        text: "",
        textValue: null,
        textEnabled: null,
        textPrecision: null,
        textLabel: null
      };
    }
    return states[id];
  }

  function updateText(state, enabled, label, precision) {
    if (state.textEnabled === enabled &&
        state.textPrecision === precision &&
        state.textLabel === label &&
        state.textValue === state.value) {
      return state.text;
    }
    state.textEnabled = enabled;
    state.textPrecision = precision;
    state.textLabel = label;
    state.textValue = state.value;
    state.text = enabled
      ? (label ? label + " " + formatFixed(state.value || 0, precision) : formatFixed(state.value || 0, precision))
      : "";
    return state.text;
  }

  ui.fps = function (id, options) {
    var key = String(id);
    var state = stateFor(key);
    var sampleUs;
    var now;
    var elapsed;
    var precision;
    var enabled;
    var label;
    var text;
    var textOptions;
    var charWidth;
    var reserveChars;

    options = options || {};
    sampleUs = own(options, "sampleMs") ? (options.sampleMs | 0) * 1000 : 1000000;
    if (sampleUs <= 0) {
      sampleUs = 1000000;
    }
    now = nowUs();
    if (!state.started || now < state.startUs) {
      state.started = true;
      state.startUs = now;
      state.frames = 0;
      state.value = 0;
    }
    state.frames += 1;
    elapsed = now - state.startUs;
    if (elapsed >= sampleUs) {
      state.value = state.frames * 1000000 / elapsed;
      state.frames = 0;
      state.startUs = now;
    }

    enabled = own(options, "enabled") ? !!options.enabled : true;
    precision = own(options, "precision") ? options.precision | 0 : 1;
    label = own(options, "label") ? String(options.label) : "FPS";
    text = updateText(state, enabled, label, precision);

    textOptions = copyTextOptions(options);
    textOptions.id = "__fps:" + key;
    if (!own(textOptions, "padding")) {
      textOptions.padding = { x: 4, y: 0 };
    }
    if (!own(textOptions, "height")) {
      textOptions.height = 14;
    }
    if (!own(textOptions, "width")) {
      charWidth = own(options, "charWidth") ? options.charWidth | 0 : 6;
      reserveChars = label ? label.length + 7 + precision : 6 + precision;
      textOptions.width = reserveChars * charWidth + 8;
    }
    if (!own(textOptions, "align") && own(textOptions, "textAlign")) {
      textOptions.align = textOptions.textAlign;
    }

    ui.text(text, textOptions);
    return state.value || 0;
  };

  ui.__fpsLoaded = true;
})(globalThis);
