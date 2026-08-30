test("websocket/offline", function () {
  var capabilities = websocketClient.capabilities();
  var invalidUrl = "";
  var invalidHeader = "";
  var wifiError = "";
  var reconnectOptionError = "";

  test.ok(capabilities.maxMessageBytes >= 256,
    "WebSocket message limit should be exposed");
  test.equal(capabilities.nativeReconnect, false,
    "native WebSocket reconnect should be disabled");
  test.equal(typeof websocketClient.send, "undefined",
    "module should not duplicate handle send");
  test.equal(typeof websocketClient.status, "undefined",
    "module should not duplicate handle status");
  test.equal(typeof websocketClient.close, "undefined",
    "module should not duplicate handle close");

  try {
    websocketClient.open({ url: "http://example.com" });
  } catch (urlError) {
    invalidUrl = String(urlError && urlError.message ? urlError.message : urlError);
  }
  test.ok(invalidUrl.indexOf("ws://") >= 0,
    "WebSocket URL should require a WebSocket scheme");

  try {
    websocketClient.open({
      url: "ws://127.0.0.1:1",
      authorization: "Bearer bad\r\nInjected: yes"
    });
  } catch (headerError) {
    invalidHeader = String(headerError && headerError.message
      ? headerError.message : headerError);
  }
  test.ok(invalidHeader.indexOf("authorization") >= 0,
    "authorization should reject line injection");

  try {
    websocketClient.open({
      url: "ws://127.0.0.1:1",
      autoReconnect: true
    });
  } catch (optionError) {
    reconnectOptionError = String(optionError && optionError.message
      ? optionError.message : optionError);
  }
  test.ok(reconnectOptionError.indexOf("unknown option") >= 0,
    "native WebSocket open should reject JavaScript reconnect policy options");

  try {
    websocketClient.open({
      url: "ws://127.0.0.1:1",
      networkTimeoutMs: 1000,
      maxMessageBytes: 1024
    });
  } catch (notConnectedError) {
    wifiError = String(notConnectedError && notConnectedError.message
      ? notConnectedError.message : notConnectedError);
  }
  test.ok(wifiError.indexOf("Wi-Fi must be connected") >= 0,
    "open should reject use before the TCP/IP stack is ready");
  return { maxMessageBytes: capabilities.maxMessageBytes };
});
