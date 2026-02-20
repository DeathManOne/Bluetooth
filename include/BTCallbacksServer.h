#ifndef _BT_CALLBACKS_SERVER_
#define _BT_CALLBACKS_SERVER_

#include <Arduino.h>
#include <BLEDevice.h>

namespace BTCallbacks {
    class server : public BLEServerCallbacks {
        void onConnect(BLEServer *pServer, esp_ble_gatts_cb_param_t *param) override {}
        void onDisconnect(BLEServer *pServer, esp_ble_gatts_cb_param_t *param) override {}
        void onMtuChanged(BLEServer *server, esp_ble_gatts_cb_param_t *param) override {}
    };

    class descriptor : public BLEDescriptorCallbacks {
        void onRead(BLEDescriptor *descriptor) override {}
	    void onWrite(BLEDescriptor *descriptor) override {}
    };

    class characteristic : public BLECharacteristicCallbacks {
        void onRead(BLECharacteristic *pCharacteristic, esp_ble_gatts_cb_param_t *param) override {}
        void onWrite(BLECharacteristic *pCharacteristic, esp_ble_gatts_cb_param_t *param) override {}
        void onNotify(BLECharacteristic *characteristic) override {}
        void onStatus(BLECharacteristic *characteristic, Status s, uint32_t code) override {}
    };
}
#endif