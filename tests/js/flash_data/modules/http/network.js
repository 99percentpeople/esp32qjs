test("http/network", function () {
  var cfg = test.requireConfig("wifiSsid", "wifiPassword", "httpUrl");
  var wifiStatus = wifi.status();
  var response;
  var syncResponse;
  var localServer;
  var localUrl;
  var bodyLimitError;
  var syncBodyLimitError = "";
  var cancelError;
  var cancelHandle;
  var cancelAccepted;
  var staleCancelAccepted;
  var replacementResponse;
  var body;

  if (!wifiStatus.connected) {
    wifiStatus = waitFor(function (resolve, reject) {
      wifi.async.connect(cfg.wifiSsid, cfg.wifiPassword, 15000, function (nextStatus, error) {
        if (error) {
          reject(error);
          return;
        }
        resolve(nextStatus);
      });
    }, 20000);
  }

  test.ok(wifiStatus.connected, "wifi should be connected before fetch");

  response = waitFor(function (resolve, reject) {
    http.async.fetch(cfg.httpUrl, function (nextResponse, error) {
      if (error) {
        reject(error);
        return;
      }
      resolve(nextResponse);
    });
  }, 20000);

  test.ok(response.status >= 200 && response.status < 600, "fetch status should be valid");
  body = response.text();
  test.ok(typeof body === "string", "fetch body should be text");

  syncResponse = http.fetch(cfg.httpUrl, { timeoutMs: 15000 });
  test.ok(syncResponse.status >= 200 && syncResponse.status < 600,
    "synchronous fetch worker status should be valid");
  test.ok(typeof syncResponse.text() === "string",
    "synchronous fetch worker body should be text");

  if (body.length > 1) {
    try {
      http.fetch(cfg.httpUrl, { timeoutMs: 15000, maxBodyBytes: 1 });
    } catch (syncBodyLimitFailure) {
      syncBodyLimitError = syncBodyLimitFailure && syncBodyLimitFailure.message
        ? syncBodyLimitFailure.message
        : String(syncBodyLimitFailure);
    }
    test.ok(syncBodyLimitError.indexOf("maxBodyBytes") >= 0,
      "synchronous response capture should enforce maxBodyBytes");
  }

  localServer = http.server({ port: 18081, host: "0.0.0.0" });
  localServer.get("/bounded", function () {
    return Response.text("0123456789abcdef");
  });
  localServer.start();
  localUrl = "http://" + wifiStatus.ip + ":18081/bounded";

  bodyLimitError = waitFor(function (resolve, reject) {
    http.async.fetch(localUrl, { maxBodyBytes: 8 }, function (nextResponse, error) {
      if (error) {
        resolve(String(error));
        return;
      }
      reject("bounded fetch unexpectedly returned status " + nextResponse.status);
    });
  }, 5000);
  test.ok(bodyLimitError.indexOf("maxBodyBytes") >= 0,
    "response capture should stop at the configured body limit");

  cancelError = waitFor(function (resolve, reject) {
    cancelHandle = http.async.fetch(localUrl, function (nextResponse, error) {
      if (error) {
        resolve(String(error));
        return;
      }
      reject("cancelled fetch unexpectedly returned status " + nextResponse.status);
    });
    cancelAccepted = http.async.cancel(cancelHandle);
  }, 5000);
  test.ok(typeof cancelHandle === "number", "async fetch should return an opaque handle");
  test.ok(cancelAccepted, "async cancel should accept an active handle");
  test.ok(cancelError.indexOf("cancelled") >= 0, "cancelled fetch should report cancellation");
  test.ok(!http.async.cancel(cancelHandle), "completed request handles should become stale");

  replacementResponse = waitFor(function (resolve, reject) {
    http.async.fetch(localUrl, function (nextResponse, error) {
      if (error) {
        reject(error);
        return;
      }
      resolve(nextResponse);
    });
    staleCancelAccepted = http.async.cancel(cancelHandle);
  }, 5000);
  test.ok(!staleCancelAccepted, "a stale handle should not cancel a reused request slot");
  test.equal(replacementResponse.status, 200, "replacement request should complete");

  localServer.close();
  wifi.disconnect();

  return { status: response.status, syncStatus: syncResponse.status, bodyLength: body.length };
});
