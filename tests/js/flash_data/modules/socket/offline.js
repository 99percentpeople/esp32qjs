test("socket/offline", function () {
  var tcpId = socket.open("tcp", 0);
  var udpId = socket.open("udp", 0);
  var tcpStatus = socket.status(tcpId);
  var udpStatus = socket.status(udpId);
  var maxTransferBytes = socket.MAX_TRANSFER_BYTES;
  var invalidProtocol = "";

  test.equal(tcpStatus.protocol, "tcp", "TCP handle should report its protocol");
  test.equal(udpStatus.protocol, "udp", "UDP handle should report its protocol");
  test.ok(!tcpStatus.connected && !tcpStatus.listening,
    "new TCP handle should be inactive");
  test.ok(maxTransferBytes >= 256,
    "socket transfer limit should be exposed");
  test.equal(socket.tcp.recv(tcpId, 64, 0), null,
    "inactive TCP recv should be non-blocking");
  test.equal(socket.udp.recvfrom(udpId, 64, 0), null,
    "UDP recvfrom should return null when no datagram is ready");

  try {
    socket.open("invalid", 0);
  } catch (protocolError) {
    invalidProtocol = String(protocolError && protocolError.message
      ? protocolError.message : protocolError);
  }
  test.ok(invalidProtocol.indexOf("tcp or udp") >= 0,
    "socket.open should reject unknown protocols");

  test.ok(socket.close(tcpId), "TCP handle should close");
  test.ok(!socket.close(tcpId), "socket.close should be idempotent");
  test.ok(socket.close(udpId), "UDP handle should close");

  return {
    maxTransferBytes: maxTransferBytes
  };
});
