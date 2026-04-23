(function () {
  function stringifyError(error) {
    if (error === undefined) {
      return "undefined";
    }
    if (error === null) {
      return "null";
    }
    if (typeof error === "string") {
      return error;
    }
    if (typeof error.message === "string" && error.message.length > 0) {
      return error.message;
    }
    try {
      return JSON.stringify(error);
    } catch (_) {
      return String(error);
    }
  }

  function emit(prefix, payload) {
    print(prefix + JSON.stringify(payload));
  }

  function config() {
    return globalThis.testConfig || {};
  }

  function helper(name, fn) {
    var details;

    try {
      details = fn();
      emit("__TEST_PASS__:", {
        name: name,
        details: details === undefined ? null : details,
      });
    } catch (error) {
      if (error && error.__testSkip) {
        emit("__TEST_SKIP__:", {
          name: name,
          reason: error.reason || "",
        });
        return;
      }
      emit("__TEST_FAIL__:", {
        name: name,
        error: stringifyError(error),
      });
    }
  }

  helper.ok = function (value, message) {
    if (!value) {
      throw new Error(message || "expected truthy value");
    }
  };

  helper.equal = function (actual, expected, message) {
    if (actual !== expected) {
      throw new Error((message || "values differ") + ": expected " + expected + ", got " + actual);
    }
  };

  helper.skip = function (reason) {
    throw {
      __testSkip: true,
      reason: reason || "",
    };
  };

  helper.config = config;
  helper.requireConfig = function () {
    var cfg = config();
    var values = {};
    var i;

    for (i = 0; i < arguments.length; i++) {
      var key = arguments[i];
      if (!cfg[key]) {
        throw new Error("missing test config: " + key);
      }
      values[key] = cfg[key];
    }

    return values;
  };

  helper.run = helper;
  globalThis.test = helper;
})();
