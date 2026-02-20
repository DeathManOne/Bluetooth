#ifndef _BT_CALLBACKS_SCAN_
#define _BT_CALLBACKS_SCAN_

#include <Arduino.h>
#include <BLEDevice.h>

namespace BTCallbacks {
    class periodicScan : public BLEPeriodicScanCallbacks {
        void onCreateSync(esp_bt_status_t status) override {}
        void onCancelSync(esp_bt_status_t status) override {}
        void onTerminateSync(esp_bt_status_t status) override {}
        void onLostSync(uint16_t sync_handle) override {}
        void onSync(esp_ble_periodic_adv_sync_estab_param_t) override {}
        void onReport(esp_ble_gap_periodic_adv_report_t params) override {}
        void onStop(esp_bt_status_t status) override {}
    };

    class extAdvertising : public BLEExtAdvertisingCallbacks {
        void onResult(esp_ble_gap_ext_adv_reprot_t report) override {}
    };

    class advertisedDevice : public BLEAdvertisedDeviceCallbacks {
        void onResult(BLEAdvertisedDevice advertisedDevice) override {
            if (!Serial.availableForWrite())
                { return; }
            Serial.printf("Device: %s\n", advertisedDevice.toString().c_str());
        }
    };
}
#endif