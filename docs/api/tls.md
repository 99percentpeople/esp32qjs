# TLS capability

`tls` is a Build Context capability, not a JavaScript global. When
`sys.info.features.tls` is true, TLS is available to the HTTP client, raw TCP
sockets, and the WebSocket client if their own native features are also
selected. A selected transport still rejects TLS when this capability is
absent; there is no plaintext downgrade.

Public HTTPS, TLS, and WSS verify the CA chain, DNS name, and certificate
validity dates. There is no option to disable verification. Connect a network
interface and call `sys.time.sync({ servers, timeoutMs? })` before the first
public TLS operation so certificate date checks have a valid wall clock.

The capability does not create a network interface, connect Wi-Fi, store
credentials, select an endpoint, or implement retry policy. Use the separate
`doc://framework/http-client`, `doc://framework/socket`, or
`doc://framework/websocket-client` resource only when its URI is present in the
current Artifact documentation index.
