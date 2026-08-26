test("websocket/offline", function () {
  var status = websocketClient.status();
  var invalidUrl = "";
  var invalidHeader = "";
  var sendError = "";
  var wifiError = "";

  test.ok(websocketClient.MAX_MESSAGE_BYTES >= 256,
    "WebSocket message limit should be exposed");
  test.ok(!status.open && !status.connected,
    "WebSocket client should start closed");
  test.equal(status.closing, false,
    "WebSocket client should not start in cleanup");

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
    websocketClient.send("not connected");
  } catch (disconnectedError) {
    sendError = String(disconnectedError && disconnectedError.message
      ? disconnectedError.message : disconnectedError);
  }
  test.ok(sendError.indexOf("not connected") >= 0,
    "send should reject a disconnected client");

  try {
    websocketClient.open({
      url: "ws://127.0.0.1:1",
      autoReconnect: false,
      networkTimeoutMs: 1000,
      maxMessageBytes: 1024
    });
  } catch (notConnectedError) {
    wifiError = String(notConnectedError && notConnectedError.message
      ? notConnectedError.message : notConnectedError);
  }
  test.ok(wifiError.indexOf("Wi-Fi must be connected") >= 0,
    "open should reject use before the TCP/IP stack is ready");
  test.ok(!websocketClient.close(), "close should be idempotent while closed");
  test.ok(!websocketClient.status().open,
    "WebSocket client should report closed after close");

  return { maxMessageBytes: websocketClient.MAX_MESSAGE_BYTES };
});
