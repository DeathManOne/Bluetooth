#ifndef _BT_CALLBACKS_CLIENT_
#define _BT_CALLBACKS_CLIENT_

#include <Arduino.h>
#include <BLEDevice.h>

namespace BTCallbacks {
    class client : public BLEClientCallbacks {
        void onConnect(BLEClient *client) override {}
        void onDisconnect(BLEClient *client) override {}
    };
    
    class security : public BLESecurityCallbacks {
        uint32_t onPassKeyRequest() override { return 123456; }
        void onPassKeyNotify(uint32_t pass_key) override {}
        bool onSecurityRequest() override { return true; }
        void onAuthenticationComplete(esp_ble_auth_cmpl_t authCmpl) override {}
        bool onConfirmPIN(uint32_t pin) override {
            vTaskDelay(500);
            return true;
        }
    };

    static void characteristicNotify(BLERemoteCharacteristic *remoteCharacteristic, uint8_t *data, size_t length, bool isNotify);
    void characteristicNotify(BLERemoteCharacteristic *remoteCharacteristic, uint8_t *data, size_t length, bool isNotify) {
        if (!isNotify)
            { return; }
        if (!Serial.availableForWrite())
            { return; }
        Serial.print("Notify callback for characteristic ");
        Serial.print(remoteCharacteristic->getUUID().toString().c_str());
        Serial.print(" of data length ");
        Serial.println(length);
        Serial.print("data: ");
        std::string value = reinterpret_cast<char *>(data);
        Serial.println(value.c_str());
    }
}
#endif