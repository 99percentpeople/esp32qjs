# TLS capability

`tls` is selected through the Build Context and reported by
`sys.info.features.tls`. It enables verified TLS for the HTTP client, raw TCP
sockets, and the WebSocket client when their corresponding transport feature
is selected.

Public HTTPS, TLS, and WSS verify the CA chain, DNS name, and certificate
validity dates. Verification is mandatory. Connect a network interface and
call `sys.time.sync({ servers, timeoutMs? })` before the first public TLS
operation so certificate date checks have a valid wall clock.

Transport setup, endpoints, credentials, and retry policy are supplied by the
application through the selected HTTP, socket, or WebSocket API. Use the
corresponding resource when its URI is present in the current Artifact
documentation index.
