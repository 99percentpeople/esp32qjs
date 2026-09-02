# `net` Module

`net` is exposed when `sys.info.features.net` is enabled. It initializes the
shared ESP-NETIF runtime and observes every interface registered by Wi-Fi,
Ethernet, PPP, or an embedding application. It does not create link drivers,
store credentials, connect an interface, or test Internet reachability.

- `net.status()`
  Return `{ ready, primaryInterface, interfaces, truncated }`. Each interface
  contains `{ key, description, name, up, ready, defaultRoute, routePriority,
  ipv4, ipv6 }`. `ready` means the interface is up and has a non-zero IPv4 or
  preferred IPv6 address. Snapshots include at most the configured interface
  limit, eight by default.
- `net.watch()`
  Return an independent bounded `EventQueue` of
  `{ type: "status", status }`. The first event is an initial snapshot; later
  events are convergent snapshots after IP or default-route changes. Close the
  queue when finished.

```js
var changes = net.watch();
print(JSON.stringify(changes.receive(0).status));
var next = Future.call(changes.receive, changes, [10000]);
print(JSON.stringify(next.wait().status));
changes.close();
```
