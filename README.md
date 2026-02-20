
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

std::string _BT_ADDRESS = "00:00:00:xx:xx:xx"
int _BT_MTU = 517 // between 17 and 517
int _BT_SCAN_TIME = 5 // in seconds
int _BT_TIMEOUT_CONNECT = 10 // in seconds

void setup() {
  _BT_CLIENT = new Bluetooth::client(_BT_PIN_CODE);
}
```

### Delete
```c++
delete _BT_CLIENT;
```

### Commands
#### connection
```c++
// ---------- WITH OPTIONNAL NOTIFY ----------
_BT_CLIENT->setCallbacksNotify(new notify_callback(characteristicNotify));
// ---------- END OF OPTIONNAL ----------

do { _BT_CLIENT->connect(_BT_ADDRESS, _BT_MTU, _BT_SCAN_TIME, _BT_TIMEOUT_CONNECT); }
while (!_BT_CLIENT->isConnected());
```
## SCANNER
## SERVER
