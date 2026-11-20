/*
 * include/BTServer.h
 *
 * Copyright (c) 2026 DeathManOne
 * https://github.com/DeathManOne
 *
 * This file is part of the Bluetooth library.
 *
 * Bluetooth is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Bluetooth is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Bluetooth.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <stdint.h>
#include <atomic>
#include <map>
#include <string>
#include <vector>
#include <BLEDevice.h>

namespace Bluetooth {
    class server {
        private:
            class ServerCallbacks final : public BLEServerCallbacks {
                server& _PARENT;
                public:
                    explicit ServerCallbacks(server& parent) : _PARENT(parent) {}
                    void onConnect(BLEServer* bleServer) override;
                    void onConnect(BLEServer* bleServer, esp_ble_gatts_cb_param_t* param) override;
                    void onDisconnect(BLEServer* bleServer) override;
                    void onDisconnect(BLEServer* bleServer, esp_ble_gatts_cb_param_t* param) override;
                    void onMtuChanged(BLEServer* bleServer, esp_ble_gatts_cb_param_t* param) override;
            };

            class SecurityCallbacks final : public BLESecurityCallbacks {
                server& _PARENT;
                public:
                    explicit SecurityCallbacks(server& parent) : _PARENT(parent) {}
                    uint32_t onPassKeyRequest() override { return _PARENT._PIN_CODE; }
                    void onPassKeyNotify(uint32_t) override {}
                    bool onSecurityRequest() override { return true; }
                    void onAuthenticationComplete(esp_ble_auth_cmpl_t) override {}
                    bool onConfirmPIN(uint32_t) override { return false; }
            };

            ServerCallbacks _SERVER_CALLBACKS;
            SecurityCallbacks _SECURITY_CALLBACKS;
            std::atomic<bool> _RESTART_ADVERTISING{false};
            uint16_t _INCOMING_CONN_ID = UINT16_MAX;

            static BLEServer* _SHARED_SERVER;
            static server* _OWNER;

            bool _RUNNING;
            bool _PIN_ENABLED;
            uint32_t _PIN_CODE;
            uint16_t _REMAINING_HANDLES;

            BLEServer *_SERVER;
            BLEService *_SERVICE;
            BLEAdvertising *_ADVERTISING;
            BLEServerCallbacks *_CALLBACKS_SERVER;
            BLEDescriptorCallbacks *_CALLBACKS_DESCRIPTOR;
            BLECharacteristicCallbacks *_CALLBACKS_CHARACTERISTIC;

            std::vector<BLEDescriptor*> _DESCRIPTORS;
            std::map<std::string, BLECharacteristic*> *_CHARACTERISTICS;
        public:
            /**
             * @brief Disable copy construction to preserve exclusive resource ownership.
             */
            server(const server&) = delete;

            /**
             * @brief Disable copy assignment to prevent shared ownership and double deletion.
             */
            server& operator=(const server&) = delete;

            /**
             * @brief Create an uninitialized server without application callbacks.
             * @note Does not initialize BLE, create a service or start advertising.
             */
            server();
            
            /**
             * @brief Stop advertising, remove the service and release owned resources.
             * @pre No clients may be connected and no callbacks or method calls
             * on this object may be executing. Call outside BLE callbacks.
             * If initialized, the BLE stack must remain active during destruction.
             * @note Releases characteristics, descriptors and owned callbacks.
             * The shared BLE server and advertising object are not deleted.
             */
            ~server();

            /**
             * @brief Replace the server event callback.
             * @param callbacks Object allocated with new; ownership is transferred
             * to this server. nullptr disables application server callbacks only.
             * With no argument, disables application server callbacks.
             * @pre No clients connected, no callbacks executing and no concurrent
             * server access. The supplied object must have no other owner.
             * @note The internal event relay remains installed. Releases the previous
             * application callback unless the supplied pointer is unchanged.
             * Disconnection handling through update() remains active with nullptr.
             */
            void setCallbacksServer(BLEServerCallbacks* callbacks = nullptr);

            /**
             * @brief Replace the callback for existing and future descriptors.
             * @param callbacks Object allocated with new; ownership is transferred
             * to this server. nullptr disables descriptor callbacks.
             * With no argument, disables descriptor callbacks.
             * @pre No clients connected, no callbacks executing and no concurrent
             * server access. The supplied object must have no other owner.
             * @note Releases the previous callback unless the supplied pointer
             * is unchanged.
             */
            void setCallbacksDescriptor(BLEDescriptorCallbacks* callbacks = nullptr);

            /**
             * @brief Replace the callback for existing and future characteristics.
             * @param callbacks Object allocated with new; ownership is transferred
             * to this server. nullptr selects the framework's default callback.
             * With no argument, selects the framework's default callback.
             * @pre No clients connected, no callbacks executing and no concurrent
             * server access. The supplied object must have no other owner.
             * @note Releases the previous callback unless the supplied pointer
             * is unchanged.
             */
            void setCallbacksCharacteristic(BLECharacteristicCallbacks* callbacks = nullptr);

            /**
             * @brief Enable PIN-based pairing and authenticated GATT access.
             * @param pinCode Six-digit passkey represented as an integer,
             * from 0 to 999999. Zero means 000000, not disabled protection.
             * @return true if the configuration was stored; false if the PIN
             * is out of range or this object is already initialized.
             * @pre Call before init(), without concurrent server access.
             * @note Protection is disabled by default. init() applies the pairing
             * configuration; subsequently created attributes require authenticated
             * encryption. The device remains discoverable.
             * Authenticated encryption is requested when an incoming connection
             * is accepted, before the first protected attribute access.
             * @note Security parameters are global to the BLE stack.
             * Bonded peers may reconnect using stored keys without entering the PIN.
             * Changing the PIN does not automatically remove existing bonds.
             */
            bool setPIN(uint32_t pinCode);

            /**
             * @brief Prepare a BLE service without starting it or advertising.
             * @param serviceUUID Service UUID: 4 or 8 hexadecimal characters,
             * or a standard 36-character UUID with hyphens.
             * @param name Local BLE device name.
             * @param numHandles Handle capacity to reserve, including the service
             * itself. Must be nonzero; defaults to 15.
             * @pre Call outside BLE callbacks, without concurrent server access.
             * @note Only one object may own the shared BLE server at a time.
             * Invalid parameters, repeated initialization, or an existing owner
             * cause an early return. No success status is returned.
             * @note Configures advertising for this service UUID, replacing the
             * advertisement from any previously destroyed server instance.
             */
            void init(std::string serviceUUID, std::string name, uint16_t numHandles = 15);

            /**
             * @brief Register configured attributes, start the service and advertise.
             * @pre Call after init() and characteristic configuration, outside
             * BLE callbacks and without concurrent server access.
             * @note Has no effect if uninitialized or already marked running.
             * No success status is returned by the underlying BLE start methods.
             */
            void start();

            /**
             * @brief Request advertising to start again without restarting the service.
             * @pre No concurrent server access.
             * @note Has no effect unless the server is marked running.
             * Can be called for an explicit advertising request.
             * Disconnection-triggered restarts are processed by update().
             */
            void startAdvertising();

            /**
             * @brief Process a pending advertising restart after disconnection.
             * @pre Call regularly from the main loop, outside BLE callbacks
             * and without concurrent calls to other server methods.
             * @note Restarts advertising only while the server is marked running.
             * Custom server callbacks remain supported; the internal relay records
             * disconnections independently of the application callback.
             */
            void update();

            /**
             * @brief Request advertising and the service to stop.
             * @pre Call outside BLE callbacks, without concurrent server access.
             * @note Preserves characteristics, descriptors and stored values.
             * Has no effect if not marked running. Does not explicitly disconnect
             * clients. Call start() to request a restart.
             */
            void stop();

            /**
             * @brief Configure a characteristic and its descriptors before starting.
             * @param characteristicUUID Unique characteristic UUID: 4 or 8 hexadecimal
             * characters, or a standard 36-character UUID with hyphens.
             * @param characteristicValue Initial value, up to ESP_GATT_MAX_ATTR_LEN bytes.
             * @param descriptors Map of descriptor UUIDs to initial values.
             * UUIDs follow the same textual formats; values must not exceed
             * ESP_GATT_MAX_ATTR_LEN bytes. These descriptors are read-only.
             * @param read Set the read property.
             * @param write Set the write-with-response property.
             * @param notify Set the notification property.
             * @param broadcast Set the broadcast property.
             * @param indicate Set the indication property.
             * @param writeNR Set the write-without-response property.
             * @return true if local creation completed and the stored characteristic
             * value matches the requested value; false if validation fails,
             * the characteristic already exists, or the handle budget is insufficient.
             * @pre Service initialized and not marked running. No clients connected,
             * no callbacks executing and no concurrent server access.
             * @note Reserves two handles plus one per descriptor. A single writable
             * 0x2902 descriptor is added automatically when notify or indicate is set;
             * supplying 0x2902 in descriptors is rejected.
             * @note Every accepted incoming connection resets 0x2902 to 00 00
             * before application connection callbacks run. Clients must subscribe
             * again after reconnecting, including previously bonded clients.
             * @note Attributes are registered with the BLE stack by start().
             * A true return does not confirm successful GATT registration.
             * Notification and indication properties do not trigger automatic sends.
             */
            bool addCharacteristic(std::string characteristicUUID, std::string characteristicValue,
                std::map<std::string, std::string> descriptors, bool read, bool write, bool notify,
                bool broadcast, bool indicate, bool writeNR
            );

            /**
             * @brief Change the stored value of an existing characteristic.
             * @param characteristicUUID UUID identifying the characteristic,
             * using the same textual formats as addCharacteristic().
             * @param characteristicValue New value, up to ESP_GATT_MAX_ATTR_LEN bytes.
             * @return true if a different value was stored successfully;
             * false if the UUID is invalid or unknown, the value is unchanged,
             * or the framework rejects the update.
             * @pre No concurrent access to the characteristic being updated.
             * @note Does not send a notification or indication automatically.
             */
            bool ChangeCharacteristic(std::string characteristicUUID, std::string characteristicValue) const;
    };
}
