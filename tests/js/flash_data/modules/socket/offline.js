test("socket/offline", function () {
  var tcp = socket.openTCP({ localPort: 0 });
  var udp = socket.openUDP({ localPort: 0 });
  var listener = socket.listenTCP({ localPort: 0, backlog: 2 });
  var tls = sys.info.features.tls ? socket.openTCP({ tls: true }) : null;
  var tcpStatus = tcp.status();
  var udpStatus = udp.status();
  var listenerStatus = listener.status();
  var tlsStatus = tls === null ? null : tls.status();
  var maxTransferBytes = socket.MAX_TRANSFER_BYTES;
  var invalidOptions = "";
  var staleError = "";

  test.equal(tcpStatus.protocol, "tcp", "TCP object should report its protocol");
  test.equal(udpStatus.protocol, "udp", "UDP object should report its protocol");
  test.equal(tcpStatus.secure, false, "plain TCP should report secure=false");
  if (tlsStatus !== null) {
    test.equal(tlsStatus.protocol, "tcp", "TLS socket should remain a TCP stream");
    test.equal(tlsStatus.secure, true, "TLS TCP should report secure=true");
  }
  test.ok(!tcpStatus.connected && !tcpStatus.listening,
    "new TCP object should be inactive");
  test.ok(listenerStatus.listening,
    "listenTCP should return an active listener");
  test.equal(typeof tcpStatus.id, "undefined",
    "socket status must not expose an internal numeric id");
  test.ok(maxTransferBytes >= 256,
    "socket transfer limit should be exposed");
  test.equal(tcp.recv(64, 0), null,
    "inactive TCP recv should be non-blocking");
  test.equal(udp.receiveFrom(64, 0), null,
    "UDP receiveFrom should return null when no datagram is ready");
  test.equal(listener.accept(0), null,
    "listener accept should return null when no client is ready");

  try {
    socket.listenTCP({});
  } catch (optionsError) {
    invalidOptions = String(optionsError && optionsError.message
      ? optionsError.message : optionsError);
  }
  test.ok(invalidOptions.indexOf("localPort") >= 0,
    "listenTCP should require localPort");

  test.ok(tcp.close(), "TCP object should close");
  test.ok(!tcp.close(), "socket object close should be idempotent");
  try {
    tcp.status();
  } catch (error) {
    staleError = String(error && error.message ? error.message : error);
  }
  test.ok(staleError.indexOf("closed") >= 0 || staleError.indexOf("stale") >= 0,
    "closed socket methods should reject the stale object");
  test.ok(udp.close(), "UDP object should close");
  test.ok(listener.close(), "listener object should close");
  if (tls !== null) {
    test.ok(tls.close(), "TLS socket should close before connecting");
  }

  return {
    maxTransferBytes: maxTransferBytes
  };
});
