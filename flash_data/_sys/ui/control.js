(function (global) {
  var owns = Object.prototype.hasOwnProperty;
  var ui = global.ui;
  var commandNames = ["up", "down", "left", "right", "ok", "back"];
  var pending = {};
  var held = {};
  var bindings = [];
  var lastCommand = "";

  if (!ui || !ui.__loaded) {
    throw new Error("load(\"_sys/ui.js\") before load(\"_sys/ui/control.js\")");
  }
  if (ui.__controlLoaded) {
    return;
  }

  function own(obj, key) {
    return obj !== null && obj !== undefined && owns.call(obj, key);
  }

  function isCommand(name) {
    var i;

    for (i = 0; i < commandNames.length; i += 1) {
      if (commandNames[i] === name) {
        return true;
      }
    }
    return false;
  }

  function normalizeCommand(name) {
    name = String(name);
    if (!isCommand(name)) {
      throw new Error("ui.control command must be up, down, left, right, ok, or back");
    }
    return name;
  }

  function emptyInput() {
    return {
      up: false,
      down: false,
      left: false,
      right: false,
      ok: false,
      back: false,
      encoderDelta: 0,
      touch: null
    };
  }

  function mergeInput(target, source) {
    var name;

    if (!source) {
      return target;
    }
    for (name in source) {
      if (own(source, name)) {
        if (isCommand(name)) {
          target[name] = target[name] || !!source[name];
        } else if (name === "encoderDelta") {
          target.encoderDelta += source.encoderDelta | 0;
        } else if (name === "touch" && source.touch) {
          target.touch = source.touch;
        }
      }
    }
    return target;
  }

  function remember(command) {
    lastCommand = String(command).toUpperCase();
  }

  function normalizeBinding(command, value, defaults) {
    var binding;

    defaults = defaults || {};
    if (typeof value === "number") {
      binding = { pin: value };
    } else {
      binding = value || {};
    }
    if (typeof binding.pin !== "number") {
      throw new Error("ui.control.bindButtons(...) binding requires a pin number");
    }
    return {
      command: normalizeCommand(own(binding, "command") ? binding.command : command),
      pin: binding.pin | 0,
      activeLow: own(binding, "activeLow") ? !!binding.activeLow :
        (own(defaults, "activeLow") ? !!defaults.activeLow : true),
      pull: own(binding, "pull") ? binding.pull :
        (own(defaults, "pull") ? defaults.pull : undefined),
      previous: false
    };
  }

  function configureBinding(binding, defaults) {
    var pull;

    if (!global.gpio || defaults.configure === false) {
      return;
    }
    if (typeof global.gpio.pinMode === "function") {
      global.gpio.pinMode(binding.pin, global.gpio.INPUT);
    }
    if (typeof global.gpio.setPull === "function") {
      pull = binding.pull;
      if (pull === false || pull === null) {
        return;
      }
      if (pull === undefined) {
        pull = binding.activeLow ? global.gpio.PULLUP : global.gpio.PULLDOWN;
      }
      if (pull) {
        global.gpio.setPull(binding.pin, pull);
      }
    }
  }

  function readButtons(input) {
    var i;
    var binding;
    var active;

    if (!global.gpio || typeof global.gpio.digitalRead !== "function") {
      return input;
    }
    for (i = 0; i < bindings.length; i += 1) {
      binding = bindings[i];
      active = !!global.gpio.digitalRead(binding.pin);
      if (binding.activeLow) {
        active = !active;
      }
      if (active) {
        input[binding.command] = true;
        if (!binding.previous) {
          remember(binding.command);
        }
      }
      binding.previous = active;
    }
    return input;
  }

  ui.control = {
    press: function (command) {
      command = normalizeCommand(command);
      pending[command] = true;
      remember(command);
      return this;
    },
    hold: function (command, active) {
      command = normalizeCommand(command);
      held[command] = active !== false;
      if (held[command]) {
        remember(command);
      }
      return this;
    },
    encoder: function (delta) {
      delta = delta | 0;
      pending.encoderDelta = (pending.encoderDelta || 0) + delta;
      if (delta !== 0) {
        lastCommand = "ENC " + (delta > 0 ? "+" : "") + delta;
      }
      return this;
    },
    touch: function (x, y, pressed) {
      pending.touch = {
        x: x | 0,
        y: y | 0,
        pressed: pressed !== false
      };
      lastCommand = "TOUCH";
      return this;
    },
    bindButtons: function (map, options) {
      var key;
      var binding;

      bindings = [];
      options = options || {};
      map = map || {};
      for (key in map) {
        if (own(map, key) && map[key] !== null && map[key] !== undefined && map[key] !== false) {
          binding = normalizeBinding(key, map[key], options);
          bindings.push(binding);
          configureBinding(binding, options);
        }
      }
      return this;
    },
    read: function (extra) {
      var input = emptyInput();
      var name;

      mergeInput(input, held);
      mergeInput(input, pending);
      mergeInput(input, extra);
      readButtons(input);
      pending = {};
      return input;
    },
    last: function () {
      return lastCommand;
    },
    clear: function () {
      pending = {};
      held = {};
      lastCommand = "";
      return this;
    },
    indicator: function (options) {
      var text;

      options = options || {};
      text = lastCommand || (own(options, "idle") ? String(options.idle) : "IDLE");
      if (own(options, "label") && options.label) {
        text = String(options.label) + " " + text;
      }
      return ui.badge(text, options);
    }
  };

  ui.__controlLoaded = true;
})(globalThis);
