# Changelog

## 2.0.0

### Added
- A compile-time warning when using a core version other than Arduino-ESP32
  2.0.17, with which the backend adaptations have been verified.
- Automatic reconnection after unexpected outgoing link loss through client::update(),
  using the last successful connection settings and a minimum five-second retry
  interval. Explicit disconnect() cancels retries; setAutoReconnect() controls them.
- Simultaneous local BLE server and outgoing client roles using the existing
  Bluedroid backend: one incoming connection and up to three outgoing connections.
  Lower limits imposed by the ESP32 model or Bluetooth stack configuration apply.
- A bounded pool of reusable native clients, with one exclusively leased backend
  per client instance. Destroying a fully disconnected instance releases its slot.
- The `dual_role.ino` example, with independent outgoing client selection and a
  numbered scan list for choosing each remote device by name and address.
- Documentation of connection limits, client lifetimes, sequential API access
  and configuration of remote services, characteristics and pairing parameters.

### Changed
- Centralized UUID validation, characteristic lookup, owned callback replacement,
  callback relay installation, security configuration and client discovery cleanup.
- Consolidated native registry and remote descriptor cleanup adaptations in
  backend.cpp. Removed client constructor calls and conditionals without effect,
  and an advertising preference immediately overwritten by the next assignment.
- Incoming connections now start with notifications and indications disabled,
  including connections from bonded devices. Clients must subscribe again.
- Client examples explicitly configure receive callbacks and process automatic
  reconnection from loop(), outside BLE callbacks.
- Centralized BLE initialization and per-peer security routing for server and
  client roles sharing the same Bluetooth stack.
- Protected GAP and GATT callback processing and native client registry updates
  with a common recursive mutex. Registry interception uses PlatformIO linker flags.
- Replaced the single shared client owner with independent pooled client backends.

### Fixed
- Remote characteristic destruction could erase descriptor map entries while
  iterating over them. A linker wrapper now deletes the descriptors and clears
  the map before invoking the native destructor.
- Native client registration could use application ID 255, which the backend
  registry treats as a placeholder. Connection attempts now skip that value.
- Recreating a server could accumulate previously advertised service UUIDs.
  Server initialization now replaces the advertisement with the current UUID.
- Outgoing client disconnection could also disconnect the incoming phone client.
  Accepted incoming links are now explicitly retained by the local server.
- Native client creation and callback dispatch could access shared backend state
  without common synchronization.

### Removed
- BTCallbacksClient.h and its default callback classes and serial notification
  printer. The internal connection and security relays remain active. Calling
  setCallbacksClient() or setCallbacksNotify() without arguments now removes the
  application callback; reception requires an explicitly configured callback.
