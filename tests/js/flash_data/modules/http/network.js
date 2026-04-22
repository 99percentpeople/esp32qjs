test("http/network", function () {
  var cfg = test.requireConfig("wifiSsid", "wifiPassword", "httpUrl");
  var wifiStatus = wifi.status();
  var response;
  var body;

  if (!wifiStatus.connected) {
    wifiStatus = waitFor(function (resolve, reject) {
      wifi.connect(cfg.wifiSsid, cfg.wifiPassword, 15000, function (nextStatus, error) {
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
    fetch(cfg.httpUrl, function (nextResponse, error) {
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

  wifi.disconnect();

  return { status: response.status, bodyLength: body.length };
});
