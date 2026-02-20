#ifndef _BT_CLIENT_
#define _BT_CLIENT_

#include <Arduino.h>
#include <BLEDevice.h>
#include "scan.h"
#include "callbacksClient.h"

namespace Bluetooth {
    class client {
        private:
            BLEClient *_CLIENT;
            BLEAdvertisedDevice *_DEVICE;
            BLESecurity *_SECURITY;
            BLEClientCallbacks *_CALLBACKS_CLIENT;
            BLESecurityCallbacks *_CALLBACKS_SECURITY;
            notify_callback *_CALLBACKS_NOTIFY;
            std::map<std::string, BLERemoteCharacteristic*> *_CHARACTERISTICS;
            std::map<std::string, BLERemoteCharacteristic*> *_TX;
            void addTx(std::string uuid, BLERemoteCharacteristic *characteristic);
            void characteristics();
        public:
            /**
             * @brief constructor
             * @param pinCode (default: 000000) if the server ask a pinCode
             */
            explicit client(uint32_t pinCode = 000000);
            
            /**
             * @brief deconstructor
             */
            ~client();

            /**
             * @brief change default Client callbacks
             * @param callbacks (default: new BTCallbacks::client())
             */
            void setCallbacksClient(BLEClientCallbacks* callbacks = new BTCallbacks::client());

            /**
             * @brief change default Security callbacks
             * @param callbacks (default: new BTCallbacks::security())
             */
            void setCallbacksSecurity(BLESecurityCallbacks* callbacks = new BTCallbacks::security());

            /**
             * @brief change default Notify callbacks
             * @param callbacks (default: new notify_callback(BTCallbacks::characteristicNotify))
             */
            void setCallbacksNotify(notify_callback* callbacks = new notify_callback(BTCallbacks::characteristicNotify));

            /**
             * @brief check whether the client is connected to server
             */
            bool isConnected() const;

            /**
             * @brief connect the client to the server after a scan, not needed to scan before
             * @param address address mac of the server to connect
             * @param MTU maximal transmission unity
             * @param scanTime time of scan in seconds
             * @param connectTimeout time to found the server in seconds
             * @return true if server found and connection established, otherwise false
             */
            bool connect(std::string address, int MTU, int scanTime, int connectTimeout);
            
            /**
             * @brief disconnect the client from the server
             * @return true if disconnected, otherwise false
             */
            bool disconnect();
            
            /**
             * @brief send a command to the server
             * @param uuid id of transmission characteristic
             * @param data message to send
             */
            void sendCommand(std::string uuid, std::string data) const;
    };
}
#endif