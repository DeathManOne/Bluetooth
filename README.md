
# Bluetooth Low Engergy (BLE) for arduino nano ESP32
## CLIENT
### Initialization
```c++
#include <BTClient.h>

// ---------- OPTIONAL ----------
#include <BLEDevice.h>
void characteristicNotify(BLERemoteCharacteristic *remoteCharacteristic, uint8_t *data, size_t length, bool isNotify) {
  if (!isNotify) { return; }
  // what you want to do
}
// ---------- END OF OPTIONAL ----------

Bluetooth::client *_BT_CLIENT;
uint32_t _BT_PIN_CODE = 123456;

void setup() {
  _BT_CLIENT = new Bluetooth::client(_BT_PIN_CODE);
}
```

### Delete
```c++
delete _BT_CLIENT;
```

### Commands
#### sets callbacks
sets them before connection
```c++
_BT_CLIENT->setCallbacksClient(new BTCallbacks::client());
_BT_CLIENT->setCallbacksSecurity(new BTCallbacks::security());
_BT_CLIENT->setCallbacksNotify(new notify_callback(BTCallbacks::characteristicNotify));
```
#### connection
```c++
std::string address = "00:00:00:xx:xx:xx"
int mtu = 517 // between 17 and 517
int scanTime = 5 // in seconds
int timeoutConnect = 10 // in seconds

// ---------- WITH OPTIONNAL NOTIFY ----------
_BT_CLIENT->setCallbacksNotify(new notify_callback(characteristicNotify));
// ---------- END OF OPTIONNAL ----------

bool connected = _BT_CLIENT->connect(address, mtu, scanTime, timeoutConnect);
```
#### check if bluetoth is connected
```c++
bool isConnected = _BT_CLIENT->isConnected();
```
#### disconnect
```c++
bool diconnected = BT_CLIENT->disconnect();
```
#### send a command (or other)
```c++
std::string uuid = "FFF0";
std::string data = "my favorite text";

_BT_CLIENT->sendCommand(uuid, data)
```
## SCANNER
## SERVER

