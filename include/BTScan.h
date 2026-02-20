#ifndef _BT_SCAN_
#define _BT_SCAN_

#include <Arduino.h>
#include <BLEDevice.h>
#include "BTCallbacksScan.h"

namespace Bluetooth {
    class scan {
        private:
            int *_SCAN_TIME;
            BLEScan *_SCAN;
            BLEPeriodicScanCallbacks *_CALLBACKS_PERIODIC_SCAN;
            BLEExtAdvertisingCallbacks *_CALLBACKS_EXT_ADVERTISING;
            BLEAdvertisedDeviceCallbacks *_CALLBACKS_ADVERTISED_DEVICE;
        public:
            /**
             * @brief constructor
             * @param scanTime time of scan in seconds
             */
            explicit scan(int scanTime);
            
            /**
             * @brief deconstructor
             */
            ~scan();
            
            /**
             * @brief change default Periodic scan callbacks
             * @param callbacks (default: new BTCallbacks::periodicScan())
             */
            void setCallbacksPeriodicScan(BLEPeriodicScanCallbacks* callbacks = new BTCallbacks::periodicScan());
            
            /**
             * @brief change default Extended advertising callbacks
             * @param callbacks (default: BTCallbacks::extAdvertising())
             */
            void setCallbacksExtAdvertising(BLEExtAdvertisingCallbacks* callbacks = new BTCallbacks::extAdvertising());
            
            /**
             * @brief change default Advertised device callbacks
             * @param callbacks (default: new BTCallbacks::advertisedDevice())
             */
            void setCallbacksAdvertisedDevice(BLEAdvertisedDeviceCallbacks* callbacks = new BTCallbacks::advertisedDevice());

            /**
             * @brief start scanning all device around
             * @return BLEScanResults (list of devices found)
             */
            BLEScanResults start();
            
            /**
             * @brief clear and stop periodic scan
             */
            void stop() const;
            
            /**
             * @brief clear result from scan
             */
            void clear() const;
    };
}
#endif