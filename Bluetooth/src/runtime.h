/*
 * src/runtime.h
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

#include <esp_arduino_version.h>

#if ESP_ARDUINO_VERSION != ESP_ARDUINO_VERSION_VAL(2, 0, 17)
#warning "Bluetooth backend adaptations were verified with Arduino-ESP32 2.0.17; check compatibility with this core version."
#endif

#include <stdint.h>
#include <string>
#include <mutex>
#include <map>
#include <esp_bt_defs.h>

class BLEAddress;
class BLEClient;
class BLESecurityCallbacks;
class BLEUUID;

namespace Bluetooth {
    namespace detail {
        /** @brief Local BLE role used to configure peer authentication. */
        enum class SecurityRole {Client, Server};

        /** @brief Security settings associated with a registered peer. */
        struct SecurityProfile {
            SecurityRole role;           ///< Local role for this peer.
            uint32_t pinCode;             ///< Six-digit PIN, in the range 0 to 999999.
            bool authenticationRequired;  ///< Reserve authentication ownership when configured.
        };

        /**
         * @brief Replace an exclusively owned callback after updating its users.
         * @tparam Callback Callback type deleted through the stored pointer.
         * @tparam BeforeReplace Callable accepting the replacement pointer.
         * @param current Owned pointer updated after deletion of the old callback.
         * @param replacement New owned callback, or nullptr to disable it.
         * @param beforeReplace Called before deletion to update borrowed references.
         * @pre No concurrent callback use or replacement; the new callback has
         * no other owner. beforeReplace must not throw or delete either callback.
         * @note Equal pointers cause no action, including no beforeReplace call.
         */
        template<typename Callback, typename BeforeReplace>
        void replaceOwnedCallback(Callback*& current, Callback* replacement, BeforeReplace beforeReplace) {
            if (current == replacement) { return; }
            beforeReplace(replacement);
            delete current;
            current = replacement;
        }

        /**
         * @brief Replace an exclusively owned callback without updating users.
         * @tparam Callback Callback type deleted through the stored pointer.
         * @param current Owned pointer updated after deletion of the old callback.
         * @param replacement New owned callback, or nullptr to disable it.
         * @pre No concurrent callback use or replacement; the new callback has
         * no other owner and borrowed references need no update.
         * @note Equal pointers cause no action.
         */
        template<typename Callback>
        void replaceOwnedCallback(Callback*& current, Callback* replacement) {
            replaceOwnedCallback(current, replacement, [](Callback*) {});
        }

        /**
         * @brief Find a borrowed characteristic using native UUID comparison.
         * @tparam Characteristic Local or remote characteristic type.
         * @param characteristics Borrowed map, or nullptr if unavailable.
         * @param uuid UUID to locate.
         * @return First matching pointer, or nullptr if absent or unavailable.
         * @pre Map entries are non-null and remain valid during the lookup.
         * No concurrent modification or destruction of the map or its objects.
         * @note Ownership is not transferred.
         */
        template<typename Characteristic>
        Characteristic* findCharacteristic(const std::map<std::string, Characteristic*>* characteristics, const BLEUUID& uuid) {
            if (characteristics == nullptr) { return nullptr; }
            for (const auto& entry : *characteristics) {
                if (entry.second->getUUID().equals(uuid)) { return entry.second; }
            }
            return nullptr;
        }

        /**
         * @brief Validate a textual 16-bit, 32-bit or 128-bit UUID.
         * @param value Four or eight hexadecimal digits, or 36 characters
         * with hyphens at the standard UUID separator positions.
         * @return true if the complete string matches a supported format.
         */
        bool isValidUUID(const std::string& value);

        /**
         * @brief Skip the application ID reserved by the native registry.
         * @pre Call immediately before each native client connection attempt.
         * Application BLE operations must remain sequential across instances.
         * @note Protects the counter check with backendMutex(); it does not
         * reserve an ID or prevent eventual application counter exhaustion.
         */
        void prepareClientConnection();

        /**
         * @brief Exclusively lease a disconnected native client from the pool.
         * @return Retained backend, or nullptr if BLE is uninitialized, the pool
         * is exhausted or native creation fails. The caller must not delete it.
         * @pre Application BLE operations are sequential across instances.
         */
        BLEClient* acquireClientBackend();

        /**
         * @brief Detach connection callbacks and release a native client lease.
         * @param backend Leased backend; retained for later reuse, never deleted.
         * @return false for null, unknown, unleased or connected backends.
         * @pre No pending operations, callbacks or concurrent application access.
         */
        bool releaseClientBackend(BLEClient* backend);

        /** @brief Return the recursive mutex shared by backend relays and adapters. */
        std::recursive_mutex& backendMutex();

        /**
         * @brief Request retention of an accepted incoming physical link.
         * @param address Address of the incoming peer.
         * @return true if the native open request is accepted, not completion.
         * @pre Call within the relayed GATTS connection event for this peer.
         */
        bool retainServerConnection(const BLEAddress& address);

        /**
         * @brief Initialize BLE if needed and install GAP and GATT event relays.
         * @return true if initialization and all relay installations succeed.
         * @pre Call outside BLE callbacks with sequential application BLE access.
         * @note Disables native automatic encryption in favor of per-peer routing.
         * Failure does not roll back initialization or already installed relays.
         */
        bool initializeBLE();

        /**
         * @brief Initialize BLE and configure the shared local device name.
         * @param deviceName Name passed to the native GAP configuration.
         * @return true if initializeBLE() and the name-setting request succeed.
         * @pre Same application access constraints as initializeBLE().
         */
        bool initializeBLE(const std::string& deviceName);

        /**
         * @brief Copy the peer address from the current relayed security event.
         * @param address Output buffer of sizeof(esp_bd_addr_t) bytes.
         * @return false for a null buffer or no supported current GAP event;
         * the output buffer is unchanged on failure.
         */
        bool getSecurityPeerAddress(esp_bd_addr_t address);

        /**
         * @brief Register or update the security route owned by a callback object.
         * @param address Peer address; must not belong to another registered route.
         * @param callbacks Borrowed non-null callback, valid until unregistered.
         * @param profile Role, PIN and authentication ownership policy.
         * @return false if BLE is uninitialized, the callback or PIN is invalid,
         * or another callback already owns this address.
         */
        bool registerSecurityPeer(const BLEAddress& address, BLESecurityCallbacks* callbacks, const SecurityProfile& profile);

        /**
         * @brief Remove a security route and release its authentication ownership.
         * @param callbacks Borrowed route identity; not deleted by this function.
         * @note Has no effect if neither a route nor ownership matches.
         */
        void unregisterSecurityPeer(BLESecurityCallbacks* callbacks);

        /**
         * @brief Apply a registered peer's role-specific security settings.
         * @param callbacks Borrowed route identity.
         * @return true if configured or already owning authentication; false if
         * unregistered, invalid, blocked by another owner or native setup fails.
         * @note Reserves ownership when the profile requires authentication.
         * Does not itself start encryption or pairing.
         */
        bool configurePeerSecurity(BLESecurityCallbacks* callbacks);

        /**
         * @brief Configure shared security parameters for the local client role.
         * @param pinCode PIN from 0 to 999999; zero represents 000000.
         * @return false if BLE is uninitialized, the PIN is invalid, authentication
         * has an owner or a native parameter-setting request fails.
         * @note Does not start pairing; failure may leave some parameters changed.
         */
        bool configureClientSecurity(uint32_t pinCode);

        /**
         * @brief Configure shared security parameters for the local server role.
         * @param pinCode PIN from 0 to 999999; zero represents 000000.
         * @return false if BLE is uninitialized, the PIN is invalid, authentication
         * has an owner or a native parameter-setting request fails.
         * @note Does not start pairing; failure may leave some parameters changed.
         */
        bool configureServerSecurity(uint32_t pinCode);
    }
}
