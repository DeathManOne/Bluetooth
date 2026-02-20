#ifndef _BT_SERVER_
#define _BT_SERVER_

#include <Arduino.h>
#include <BLEDevice.h>
#include "BTCallbacksServer.h"

namespace Bluetooth {
    class server {
        private:
            BLEServer *_SERVER;
            BLEService *_SERVICE;
            BLEAdvertising *_ADVERTISING;
            BLEServerCallbacks *_CALLBACKS_SERVER;
            BLEDescriptorCallbacks *_CALLBACKS_DESCRIPTOR;
            BLECharacteristicCallbacks *_CALLBACKS_CHARACTERISTIC;
            std::map<std::string, BLECharacteristic*> *_CHARACTERISTICS;
        public:
            /**
             * @brief constructor
             */
            server();
            
            /**
             * @brief deconstructor
             */
            ~server();

            /**
             * @brief change default Server callbacks
             * @param callbacks (default: new BTCallbacks::server())
             */
            void setCallbacksServer(BLEServerCallbacks* callbacks = new BTCallbacks::server());

            /**
             * @brief change default Descriptor callbacks
             * @param callbacks (default: new BTCallbacks::descriptor())
             */
            void setCallbacksDescriptor(BLEDescriptorCallbacks* callbacks = new BTCallbacks::descriptor());

            /**
             * @brief change default Characteristic callbacks
             * @param callbacks (default: new BTCallbacks::characteristic())
             */
            void setCallbacksCharacteristic(BLECharacteristicCallbacks* callbacks = new BTCallbacks::characteristic());
            
            /**
             * @brief start server
             * @param serviceUUID uuid of the server
             * @param name name of the server
             */
            void start(std::string serviceUUID, std::string name);
            
            /**
             * @brief stop server
             */
            void stop();
            
            /**
             * @brief add a characteristic of the service with its descriptor
             * @param characteristicUUID uuid of the characteristic
             * @param characteristicValue initial value
             * @param descriptors all of descriptor for this characteristic
             * @param read can be read by client or not
             * @param write can be written by client or not
             * @param notify can be notified by client or not
             * @param broadcast can be broadcasted by client or not
             * @param indicate can be indicated by client or not
             * @param writeNR can be written (with No Response) by client or not
             * @return true if created with descriptors, otherwise false
             */
            bool addCharacteristic(std::string characteristicUUID, std::string characteristicValue,
                std::map<std::string, std::string> descriptors, bool read, bool write, bool notify,
                bool broadcast, bool indicate, bool writeNR) const;
            
            /**
             * @brief change one characteristic
             * @param characteristicUUID uuid of the characteristic
             * @param characteristicValue new value
             * @return true if value changed, otherwise false
             */
            bool ChangeCharacteristic(std::string characteristicUUID, std::string characteristicValue) const;
    };
}
#endif