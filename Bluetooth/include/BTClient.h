/*
 * include/BTClient.h
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
#include <BLEDevice.h>
#include "BTScan.h"

namespace Bluetooth {
    /**
     * @brief BLE central client operating on one selected remote service.
     * @note Owns its application callbacks and local resources.
     * Exclusively leases one native BLEClient backend from a bounded pool.
     * Application BLE operations must be serialized across all instances.
     */
    class client {
        private:
            class ClientCallbacks final : public BLEClientCallbacks {
                client& _PARENT;
                public:
                    explicit ClientCallbacks(client& parent) : _PARENT(parent) {}
                    void onConnect(BLEClient* bleClient) override;
                    void onDisconnect(BLEClient* bleClient) override;
            };

            class SecurityCallbacks final : public BLESecurityCallbacks {
                client& _PARENT;
                public:
                    explicit SecurityCallbacks(client& parent) : _PARENT(parent) {}
                    uint32_t onPassKeyRequest() override;
                    void onPassKeyNotify(uint32_t passKey) override;
                    bool onSecurityRequest() override;
                    void onAuthenticationComplete(esp_ble_auth_cmpl_t result) override;
                    bool onConfirmPIN(uint32_t pin) override;
            };

            ClientCallbacks _CLIENT_CALLBACKS;
            SecurityCallbacks _SECURITY_CALLBACKS;
            std::atomic<int> _AUTHENTICATION_STATE{0}; // 0: pending; 1: success; -1: failure.
            bool _REQUEST_ENCRYPTION;
            bool _AUTO_RECONNECT_ENABLED = true;
            bool _RECONNECT_ARMED        = false;
            std::atomic<uint32_t> _LAST_DISCONNECT_AT{0};
            uint32_t _LAST_RECONNECT_AT  = 0;
            std::string _RECONNECT_ADDRESS;
            std::string _RECONNECT_SERVICE_UUID;
            int _RECONNECT_MTU           = 23;
            int _RECONNECT_SCAN_TIME     = 5;
            int _RECONNECT_TIMEOUT       = 10;

            uint32_t _PIN_CODE;

            BLEClient *_CLIENT;
            BLEAdvertisedDevice *_DEVICE;
            BLEClientCallbacks *_CALLBACKS_CLIENT;
            BLESecurityCallbacks *_CALLBACKS_SECURITY;

            notify_callback *_CALLBACKS_NOTIFY;

            std::map<std::string, BLERemoteCharacteristic*> *_CHARACTERISTICS;
            std::map<std::string, BLERemoteCharacteristic*> *_TX;

            void addTx(std::string uuid, BLERemoteCharacteristic *characteristic);
            void characteristics(const std::string& serviceUUID);
            bool connectPeer(const std::string& address, int MTU, int scanTime, int connectTimeout, const std::string& serviceUUID);
            bool disconnectPeer();
            void clearCharacteristicReferences();
            void clearDiscovery();
        public:
            /**
             * @brief Disable copy construction to preserve exclusive resource ownership.
             */
            client(const client&) = delete;

            /**
             * @brief Disable copy assignment to prevent shared ownership and double deletion.
             */
            client& operator=(const client&) = delete;

            /**
             * @brief Initialize the client and lease a native BLE backend.
             * @param pinCode Default pairing passkey, from 0 to 999999.
             * Zero represents 000000; it does not disable pairing.
             * @param requestEncryption Request authenticated encryption on connection.
             * When true, connect() waits for successful authentication before
             * discovering services and subscribing to characteristics.
             * When false, connect() does not wait for authentication; the peer
             * may still require pairing for protected operations.
             * @pre No concurrent BLE initialization or client ownership changes.
             * @note An exhausted backend pool, initialization failure or invalid
             * PIN leaves this object inactive; its connect() calls return false.
             * A leased slot remains reserved while disconnected and is released
             * only when this object is destroyed.
             * @note Security settings are global to the BLE stack.
             * Stored bonding keys may be reused without requesting the PIN again.
             * @note Automatic reconnection is enabled by default after a successful
             * connect(). Call update() regularly outside BLE callbacks to process it.
             */
            explicit client(uint32_t pinCode = 000000, bool requestEncryption = true);
            
            /**
             * @brief Release owned resources and return the native backend to its pool.
             * @pre The client must be fully disconnected, with no pending connection
             * attempt, method call or callback execution. Call outside BLE callbacks.
             * If initialized, the BLE stack must remain active during destruction.
             * @note Releases owned callback objects, security configuration and
             * local containers. The native BLEClient backend is retained for reuse.
             * Does not remove stored bonding keys.
             */
            ~client();

            /**
             * @brief Replace the connection event callbacks.
             * @param callbacks Object allocated with new; ownership is transferred
             * to this client. nullptr disables application connection callbacks.
             * With no argument, removes application connection callbacks.
             * @pre Client fully disconnected, with no pending operation, concurrent
             * access or callback execution. The object must have no other owner.
             * @note Deletes the previous callback unless the pointer is unchanged.
             * The connection callback reports link establishment before connect()
             * completes authentication and service discovery.
             */
            void setCallbacksClient(BLEClientCallbacks* callbacks = nullptr);

            /**
             * @brief Restore built-in security handling using the configured PIN.
             * @pre Client fully disconnected, with no pending operation, concurrent
             * access or callback execution.
             * @note Deletes the previous application security callback.
             * The internal authentication relay remains active.
             */
            void setCallbacksSecurity();

            /**
             * @brief Replace the application security callbacks.
             * @param callbacks Object allocated with new; ownership is transferred
             * to this client. nullptr removes application security callbacks.
             * @pre Client fully disconnected, with no pending operation, concurrent
             * access or callback execution. The object must have no other owner.
             * @note Deletes the previous callback unless the pointer is unchanged.
             * The internal authentication relay remains active.
             * With nullptr, uses the configured PIN, accepts security requests
             * and rejects numeric comparison confirmation.
             */
            void setCallbacksSecurity(BLESecurityCallbacks* callbacks);

            /**
             * @brief Replace the callback used for notifications and indications.
             * @param callbacks Function object allocated with new; ownership is
             * transferred to this client. nullptr or an empty function disables
             * automatic subscription and prevents explicit subscription activation.
             * With no argument, removes the receive callback.
             * @pre Client fully disconnected, with no pending operation, concurrent
             * access or callback execution. The object must have no other owner.
             * @note Deletes the previous function object unless the pointer is unchanged.
             * This setter does not cancel existing subscriptions.
             */
            void setCallbacksNotify(notify_callback* callbacks = nullptr);

            /**
             * @brief Return the connection state currently reported by the BLE backend.
             * @return true while connected; false if disconnected or inactive.
             * @note A disconnection request may remain pending after disconnect().
             * A true result alone does not confirm authentication or subscription.
             */
            bool isConnected() const;

            /**
             * @brief Enable or pause automatic reconnection after unexpected link loss.
             * @param enabled true enables retries; false pauses them without closing
             * an active connection or discarding the remembered connection settings.
             * @pre Call outside BLE callbacks, without concurrent application BLE access.
             * @note Enabled by default. Requires a previous successful connect().
             * Enabling waits at least five seconds before another attempt is eligible.
             * After disconnect(), a new successful connect() is required to arm retries.
             */
            void setAutoReconnect(bool enabled);

            /**
             * @brief Process a pending automatic reconnection for this client.
             * @pre Call regularly from loop(), outside BLE callbacks, with application
             * BLE operations serialized across all client and server instances.
             * @note Does nothing while connected, disabled or not armed. An eligible
             * call attempts reconnection using the last successful peer MAC address,
             * MTU, scan duration, timeout and service UUID. Discovery and automatic
             * subscriptions run again using the currently configured receive callback.
             * @note Waits at least five seconds after link loss and after each completed
             * retry. Unsuccessful retries remain armed. Each attempt blocks like connect();
             * this method does not create a background task or guarantee a total timeout.
             */
            void update();

            /**
             * @brief Scan for a peer, connect and discover the selected service.
             * @param address Peer MAC address in XX:XX:XX:XX:XX:XX format.
             * Hexadecimal letters are case-insensitive.
             * @param MTU Requested MTU, from 23 to 517. The negotiated value
             * is not confirmed by this method.
             * @param scanTime Positive scan duration in seconds. Performs one scan.
             * @param connectTimeout Positive retry budget in seconds, representable
             * as uint32_t milliseconds. Also used as a separate authentication
             * waiting budget when encryption is requested.
             * @param serviceUUID Service UUID: 4 or 8 hexadecimal characters,
             * or a standard 36-character UUID with hyphens.
             * If empty, selects the first advertised service UUID, when available.
             * @return true if still connected after discovery and, when requested,
             * authentication succeeded. Otherwise returns false.
             * @pre Call outside BLE callbacks, without concurrent application access.
             * The client must not already be connected.
             * @note Blocking operation. The retry budget does not interrupt an
             * underlying connection attempt or bound subsequent GATT operations;
             * it is not a timeout for the entire method.
             * @note Subscribes automatically to supported characteristics when a
             * receive callback is configured. Prefers notifications over indications.
             * A true return does not confirm service presence or subscription success.
             * @note Authentication failure or expiry requests asynchronous disconnection;
             * isConnected() may briefly remain true after a false return.
             * @note A successful call remembers its parameters and arms automatic
             * reconnection through update(). A valid new attempt cancels the previous
             * reconnection intention; a failed manual attempt is not automatically retried.
             */
            bool connect(std::string address, int MTU, int scanTime, int connectTimeout, std::string serviceUUID = "");

            /**
             * @brief Request disconnection and clear local discovery references.
             * @return true if inactive or already reported disconnected when checked;
             * false if the backend still reports a connection.
             * @pre Call outside BLE callbacks, without concurrent application access.
             * @note Disconnection is asynchronous. Poll isConnected() for completion.
             * Clears the saved advertised device and writable-characteristic lookup.
             * Does not remove bonding keys or reset stored server values.
             * @note Cancels automatic retries, even if already disconnected.
             * A new successful connect() is required to arm them again.
             */
            bool disconnect();

            /**
             * @brief Read a readable characteristic from the selected service.
             * @param uuid Characteristic UUID, using the formats accepted by connect().
             * UUID matching is case-insensitive.
             * @return Received bytes, preserving embedded zeros. Returns an empty
             * string if disconnected, the UUID is invalid or absent, the characteristic
             * is not readable, or the framework reports a read failure.
             * @pre Call outside BLE callbacks, without concurrent application access.
             * @note Blocking operation without a timeout implemented by this wrapper.
             * An empty characteristic value cannot be distinguished from a read failure.
             */
            std::string readCharacteristic(const std::string& uuid) const;

            /**
             * @brief Enable or disable reception for a characteristic.
             * @param uuid Characteristic UUID, using the formats accepted by connect().
             * UUID matching is case-insensitive.
             * @param enabled true to subscribe; false to unsubscribe.
             * @return true if still connected and the read-back 0x2902 descriptor
             * matches the requested configuration. Otherwise returns false.
             * @pre Call outside BLE callbacks, without concurrent application access.
             * @note Requires a discovered characteristic supporting notifications
             * or indications and containing a 0x2902 descriptor.
             * Enabling also requires a configured, non-empty receive callback.
             * Prefers notifications when both modes are supported.
             * @note Blocking operation without a timeout implemented by this wrapper.
             * A false return does not guarantee that the previous configuration
             * was preserved; no rollback is performed.
             * Descriptor confirmation alone does not prove callback registration
             * or subsequent data delivery.
             */
            bool setSubscription(const std::string& uuid, bool enabled);

            /**
             * @brief Write bytes to a writable characteristic in the selected service.
             * @param uuid Characteristic UUID, using the formats accepted by connect().
             * UUID matching is case-insensitive.
             * @param data Bytes to send, preserving embedded zeros.
             * Must not exceed ESP_GATT_MAX_ATTR_LEN from the installed framework.
             * @pre Call outside BLE callbacks, without concurrent application access.
             * @note Uses write with response when supported, otherwise write without
             * response. Silently ignores invalid UUIDs, oversized data, disconnected
             * clients and UUIDs absent from the writable-characteristic lookup.
             * @note Blocking operation without a timeout implemented by this wrapper.
             * Adds a 250 ms delay after a matching write attempt.
             * Does not report write success or implement application-level fragmentation.
             */
            void sendCommand(std::string uuid, std::string data) const;
    };
}
