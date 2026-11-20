
# Bluetooth Low Energy (BLE) for arduino nano ESP32

With the supplied PlatformIO configuration: a local server and up to three
simultaneous outgoing clients. The local server accepts one incoming client.
Outgoing clients connect to distinct remote devices.

The firmware supports at most one incoming BLE connection and three outgoing
BLE connections simultaneously. These are upper limits: the actual capacity
also depends on the ESP32 model and its Bluetooth stack configuration. If the
device or its configuration supports fewer connections, that lower limit applies.
The library cannot override hardware or Bluetooth stack limits.

## Backend compatibility
The backend adaptations have been verified with Arduino-ESP32 2.0.17 and its
bundled Bluedroid BLE library. They depend on internal C++ symbols intercepted
through the linker flags in library.json. Other core versions trigger a compile
warning and require a compatibility review; they are not automatically supported.

## CLIENT
### Initialization
```c++
#include <BTClient.h>

// ---------- OPTIONAL ----------
#include <BLEDevice.h>
void characteristicNotify(BLERemoteCharacteristic *remoteCharacteristic, uint8_t *data, size_t length, bool isNotify) {
  // Process exactly length bytes; data is not necessarily null-terminated.
  // isNotify is true for a notification and false for an indication.
  // Copy any required data before returning.
  // Do not perform blocking BLE operations from this callback.
}
// ---------- END OF OPTIONAL ----------

Bluetooth::client *_BT_CLIENT = nullptr;
constexpr uint32_t _BT_PIN_CODE = 123456;
constexpr bool requestEncryption = true;

void setup() {
  _BT_CLIENT = new Bluetooth::client(_BT_PIN_CODE, requestEncryption);
}
```

requestEncryption defaults to true: the client requests authenticated encryption
and waits for authentication before discovery and automatic subscriptions.

Set it to false for peripherals that do not require pairing. This skips the
automatic encryption request and authentication wait; the peer may still require
pairing for protected operations.

Valid PINs range from 0 to 999999. Zero represents 000000, not disabled security.
An invalid PIN or an exhausted native client pool leaves the new object inactive;
its connect() calls return false. Each initialized object reserves one pool slot,
even while disconnected. Destroy it to release that slot.

Security settings are global to the BLE stack. Bonded peers may reuse stored
keys without requesting the PIN again. Changing the PIN does not remove bonds.

Authentication failure or expiry makes connect() return false and request
asynchronous disconnection. isConnected() may briefly remain true.

### Automatic reconnection
Automatic reconnection is enabled by default after a successful connect().
Call update() regularly for each client from loop(), outside BLE callbacks:
```c++
void loop() {
    _BT_CLIENT->update();
}
```

For simultaneous roles, also call the local server's update(). Process all client
updates sequentially. Each client remembers its last successful MAC address,
MTU, scan duration, timeout and service UUID. After unexpected link loss, update()
waits at least five seconds before retrying. Failed retries continue, with at
least five seconds between the end of an attempt and the next one.

A retry blocks like connect(); it does not run in a background task. Actual
timing depends on calls to update() and other BLE operations. Discovery and
automatic subscriptions run again using the configured receive callback.
The saved address must still identify the peer; address changes require a new
manual selection and connection.

Use setAutoReconnect(false) to pause retries without disconnecting an active
link. setAutoReconnect(true) enables them again, with a minimum five-second
delay before the next eligible attempt. Neither call starts an initial connection.
disconnect() cancels retries, even if already disconnected; a new successful
connect() is then required. Failed manual connection attempts are not retried
automatically. Invalid parameters rejected before an attempt preserve any prior
reconnection intention.

### Delete
Request disconnection first, then wait until isConnected() reports false.
Destroy the client only when no connection attempt, method call or callback
is executing, and while the BLE stack remains active.

Run outside BLE callbacks:
```c++
delete _BT_CLIENT;
_BT_CLIENT = nullptr;
```

Each client object exclusively leases its own native BLE backend from a bounded
pool. Native backends are retained after destruction and reused by later objects.
Serialize application BLE operations across all instances, including connection
attempts, construction and destruction. Do not call blocking BLE methods from
callbacks. Concurrent connections do not imply concurrent API calls.

Do not independently create another BLEClient or deinitialize the BLE stack
while using the library.

### Commands
#### Callback configuration
Configure callbacks while fully disconnected, with no pending operations,
concurrent access or callback execution.

The following calls remove application callbacks; the internal security relay
continues to use the constructor PIN, accept security requests and reject numeric
comparison confirmation. The library does not print received data to Serial.
```c++
_BT_CLIENT->setCallbacksClient();
_BT_CLIENT->setCallbacksSecurity();
_BT_CLIENT->setCallbacksNotify();
```

Custom callback objects must be allocated with new. The client takes exclusive
ownership and deletes them when replaced or destroyed.
Connection callbacks report link establishment before authentication and service
discovery finish. A link callback alone does not establish that connect() will
return true.

Passing nullptr to setCallbacksNotify() disables automatic subscriptions and
prevents explicit subscription activation. It does not cancel existing subscriptions.
The same applies when setCallbacksNotify() is called with no argument. Configure
an explicit receive callback before connecting to enable automatic subscriptions.

#### connection
```c++
std::string address = "00:11:22:33:44:55"; // Replace with the peer address.
int mtu = 23;                           // Requested MTU: 23 to 517.
int scanTime = 5;                       // Scan duration in seconds.
int timeoutConnect = 10;                // Retry and authentication waiting budgets.
std::string serviceUUID = "12345678-1234-1234-1234-1234567890bb";

// Optional: configure a custom receive callback before connecting.
_BT_CLIENT->setCallbacksNotify(new notify_callback(characteristicNotify));

bool connected = _BT_CLIENT->connect(
    address, mtu, scanTime, timeoutConnect, serviceUUID
);
```

Replace the address and service UUID with those of your peripheral.
If serviceUUID is omitted, the client selects the first advertised service UUID,
when available. Devices may expose services without advertising their UUIDs.

When encryption is requested, connect() waits for successful authentication
before discovery and automatic subscription.

timeoutConnect does not bound the entire operation: it cannot interrupt a
blocking framework connection attempt or subsequent GATT operations.
A true return confirms the connection and requested authentication, but does
not guarantee service presence or successful subscriptions.

#### check if bluetooth is connected
```c++
bool isConnected = _BT_CLIENT->isConnected();
```

#### Disconnect
```c++
bool disconnected = _BT_CLIENT->disconnect();
```

Disconnection is asynchronous. A false return means the backend still reports
a connection when checked; it does not necessarily indicate request failure.
Poll isConnected() from the application loop until it reports false.

#### Read a characteristic
```c++
std::string value = _BT_CLIENT->readCharacteristic(
    "12345678-1234-1234-1234-1234567890bc"
);
```

The returned string preserves binary data, including embedded zeros.
An empty value cannot be distinguished from a read failure.
Invalid or unknown UUIDs, disconnected clients and non-readable characteristics
also return an empty string.

#### Enable or disable a subscription
```c++
const std::string notifyUUID = "12345678-1234-1234-1234-1234567890be";

bool disabled = _BT_CLIENT->setSubscription(notifyUUID, false);
bool enabled = _BT_CLIENT->setSubscription(notifyUUID, true);
```

Enabling requires a configured receive callback. The characteristic must support
notifications or indications and provide a 0x2902 descriptor.
Notifications are preferred when both modes are supported.

A true return confirms that the descriptor read-back matches the requested
configuration while connected. It does not guarantee subsequent data delivery.
A false return does not guarantee that the previous configuration was preserved.

Call these blocking methods outside BLE callbacks, without concurrent application
access. This wrapper implements no timeout for these GATT operations.

#### Send data
```c++
std::string uuid = "12345678-1234-1234-1234-1234567890bd";
std::string data = "my favorite text";

_BT_CLIENT->sendCommand(uuid, data);
```

Replace characteristic UUIDs with those exposed by your peripheral.
sendCommand() preserves embedded zeros and accepts up to ESP_GATT_MAX_ATTR_LEN
bytes from the installed framework.

It prefers write with response when supported, otherwise write without response.
It provides no success status and silently ignores invalid targets or oversized data.
The call is blocking and adds a 250 ms delay after a matching write attempt.

### Complete example
Upload [server.ino](examples/server.ino) to one ESP32 and
[client.ino](examples/client.ino) to another.

Both examples use ESP32-BLE-EXAMPLE, the same service and characteristic UUIDs,
and PIN 246810. Keep usePIN and pairingPIN consistent in both sketches.
The server can run without a serial console.

Open the client console at 115200 baud:

- c: scan for the server and connect.
- r: read the current value.
- w: write CLIENT EXAMPLE.
- 0 / 1: disable or enable notifications.
- d: request disconnection.
- i: display the current connection state.

The client subscribes automatically after authentication.
After d, wait until i reports DISCONNECTED before reconnecting with c.
Unexpected link losses trigger automatic retries; d cancels these retries.

The UUIDs in the earlier snippets are illustrative; the paired examples use
one shared read/write/notify characteristic ending in 90bc.

### Local server with multiple outgoing clients
Use [dual_role.ino](examples/dual_role.ino) for simultaneous server and client roles.
The local server is named ESP32-BLE-DUAL and uses PIN 135790. A phone can read,
write and subscribe to its characteristic ending in 90bc, under the service
22345678-1234-1234-1234-1234567890bb.

Configure each outgoing client in targets[] with the service UUID, characteristic
UUID, PIN and encryption requirement of the peripheral you intend to use.
The first client's settings match server.ino. The other two request no encryption
and have empty UUIDs for connection-only use. Empty UUIDs disable the example's
read/write commands for that client; configure them to exchange application data.
Choosing a device does not determine its protocol or pairing requirements.

At 115200 baud, select a client with 1, 2 or 3, then use c to scan. The console
lists discovered BLE devices by number, name and address, including unnamed
devices. Enter a device number and press Enter to connect, or x to cancel.
Discovery alone does not guarantee that a device accepts connections.
After connection, use r to read, w to write DUAL ROLE EXAMPLE, or d to disconnect.
Use i to display all outgoing connection states and h for help. After d, wait
for i to report DISCONNECTED before reconnecting. Automatic subscriptions are
enabled for clients with configured UUIDs when the remote service supports them.
Unexpected losses are retried independently for each previously connected client.
Retries pause while the console is waiting for a device selection.

Local writes are echoed through notifications. Rapid local writes may be
coalesced. Scanning, connection and GATT calls block the application loop;
local notification processing resumes when they return.

## SCANNER
### initialization
```c++
#include <BTScan.h>
Bluetooth::scan *_BT_SCAN;

void setup() {
  int scanTime = 5; // in seconds
  _BT_SCAN = new Bluetooth::scan(scanTime);
}
```

### delete
```c++
delete _BT_SCAN;
```

### command
The scanner returns discovered devices through start() and does not expose callbacks.
Each result provides the device address and address type, plus its name and advertised
service UUIDs when available. A scan does not discover the complete list of GATT services;
those are discovered after connecting.

#### start scanning
By default, all discovered devices are returned, including unnamed devices.
Pass false to exclude devices whose name is empty. Filtering occurs after scanning.
```c++
BLEScanResults devices = _BT_SCAN->start(); // Equivalent to start(true).
  for (int d = 0; d < devices.getCount(); d++)
    { Serial.println(devices.getDevice(d).toString().c_str()); }
```

#### clear result of scanning
```c++
_BT_SCAN->clear();
```

#### stop scanning
Requests scanning to stop without clearing the results.
Call clear() only after scanning has finished and the results are no longer in use.
```c++
_BT_SCAN->stop();
```

## SERVER

### Initialization and characteristic configuration
Keep a persistent server accessible from setup() and loop():
```c++
#include <BTServer.h>

Bluetooth::server btServer;
```

Run the following configuration from setup():
```c++
btServer.init(
    "12345678-1234-1234-1234-1234567890bb",
    "ESP32-BLE-TEST",
    4
);

const bool added = btServer.addCharacteristic(
    "12345678-1234-1234-1234-1234567890bc",
    "BLE SERVER OK",
    {{"2901", "Server test value"}},
    true,   // Read.
    false,  // Write with response.
    false,  // Notify.
    false,  // Broadcast.
    false,  // Indicate.
    false   // Write without response.
);

if (!added) {
    return;
}

btServer.start();
```

The handle budget includes one handle for the service, two per characteristic
and one per descriptor. Notify or indicate automatically adds one writable
Client Characteristic Configuration descriptor (0x2902). Do not supply 0x2902
in the descriptor map.

Each accepted incoming connection starts with notifications and indications
disabled (0x2902 = 00 00), including connections from previously bonded devices.
The client must subscribe again after reconnecting. A client application may
perform this subscription automatically.

Recreating the server replaces the advertised service UUID with the current one;
UUIDs from previous server instances are not accumulated in advertisements.

### Optional PIN protection
To enable protection, insert this call in setup(), before btServer.init():
```c++
if (!btServer.setPIN(246810)) {
    return;
}
```

Omit this call to retain the default unprotected access.
Valid PINs range from 0 to 999999; zero represents 000000.

PIN protection requires authenticated encryption for characteristic access,
descriptor access and notification/indication subscriptions. The device remains
discoverable. Authenticated encryption is requested when an incoming connection
is accepted; pairing can therefore begin before the first protected access.

Bonded devices may reconnect using stored keys without entering the PIN again.
Changing the PIN does not automatically remove existing bonds.
Security parameters are global to the BLE stack.

### Updating a value
```c++
bool changed = btServer.ChangeCharacteristic(
    "12345678-1234-1234-1234-1234567890bc",
    "ESP32 UPDATE"
);
```

Returns false if the value is unchanged, the UUID is invalid or unknown,
or the framework rejects the update. This does not automatically send a
notification or indication.

### Callbacks
Register custom server, characteristic or descriptor callbacks with the
corresponding setCallbacks...() method. Supply objects allocated with new;
the server takes exclusive ownership and deletes them when replaced or destroyed.
Configure callbacks with no clients connected and no callbacks executing.

### Reconnection after disconnection
Call update() regularly from loop():
```c++
void loop() {
    btServer.update();
}
```

The internal callback relay records incoming client disconnections. update()
processes the pending advertising restart without restarting the service.
Outgoing client disconnections leave the accepted incoming connection active.

Custom server callbacks remain supported. Passing nullptr to setCallbacksServer()
disables application callbacks but preserves this internal handling.

An explicitly stopped server is not advertised again by update().
Use start() to restart it. startAdvertising() remains available for explicit
advertising requests while the server is marked running.

### Stop and restart
```c++
btServer.stop();
btServer.start();
```

Stopping preserves characteristics, descriptors and values; it does not
explicitly disconnect clients.

### Lifetime
Only one server object may own the shared BLE server at a time.
It accepts one incoming client and may coexist with the outgoing clients.
Use the API sequentially. Do not independently call BLEDevice::createServer()
or deinitialize the BLE stack while using the shared server.

Destroy a configured server only with no clients connected, no callbacks or
method calls executing, and the BLE stack still active. Destruction removes
its service and releases its owned resources; the shared BLE server is retained.
