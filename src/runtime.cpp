/*
 * src/runtime.cpp
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

#include "runtime.h"
#include <BLEDevice.h>
#include <esp_gap_ble_api.h>
#include <esp_gattc_api.h>
#include <esp_gatts_api.h>
#include <array>
#include <atomic>
#include <sdkconfig.h>
#include <cstring>
#include <mutex>
#include <vector>

namespace {
    std::atomic<esp_gap_ble_cb_t> frameworkGapCallback{nullptr};
    std::atomic<esp_gattc_cb_t> frameworkGattClientCallback{nullptr};
    std::atomic<esp_gatts_cb_t> frameworkGattServerCallback{nullptr};
    thread_local esp_gatt_if_t activeGattServerInterface = ESP_GATT_IF_NONE;

    #if defined(BLUETOOTH_REGISTRY_WRAPPING) && BLUETOOTH_REGISTRY_WRAPPING   && \
        defined(CONFIG_BT_ACL_CONNECTIONS)   && CONFIG_BT_ACL_CONNECTIONS > 1
        constexpr size_t configuredClientCapacity = CONFIG_BT_ACL_CONNECTIONS - 1;
        constexpr size_t clientBackendCapacity    = configuredClientCapacity < 3 ? configuredClientCapacity : 3;
    #else
        constexpr size_t clientBackendCapacity    = 1;
    #endif

    struct ClientBackendSlot {
        BLEClient* backend = nullptr;
        bool leased        = false;
    };

    std::array<ClientBackendSlot, clientBackendCapacity>& clientBackends() {
        static std::array<ClientBackendSlot, clientBackendCapacity> slots{};
        return slots;
    }

    struct SecurityPeer {
        BLEAddress address;
        BLESecurityCallbacks* callbacks;
        Bluetooth::detail::SecurityProfile profile;
    };

    std::recursive_mutex securityMutex;
    std::vector<SecurityPeer> securityPeers;

    BLESecurityCallbacks* authenticationOwner                  = nullptr;
    thread_local BLESecurityCallbacks* activeSecurityCallbacks = nullptr;

    template<typename Callback, typename GetCallback, typename RegisterCallback>
    bool installRelay(std::atomic<Callback>& frameworkCallback, Callback relay, GetCallback getCallback, RegisterCallback registerCallback) {
        const Callback currentCallback = getCallback();
        if (currentCallback == relay)                                          { return true; }
        if (currentCallback == nullptr || frameworkCallback.load() != nullptr) { return false; }

        frameworkCallback.store(currentCallback);
        if (registerCallback(relay) != ESP_OK) {
            frameworkCallback.store(nullptr);
            return false;
        }
        return true;
    }

    void gattClientEventRelay(esp_gattc_cb_event_t event, esp_gatt_if_t gattcIf, esp_ble_gattc_cb_param_t* param) {
        const esp_gattc_cb_t callback = frameworkGattClientCallback.load();
        if (callback == nullptr) { return; }

        const std::lock_guard<std::recursive_mutex> lock(securityMutex);
        callback(event, gattcIf, param);
    }

    class ScopedGattServerInterface {
        esp_gatt_if_t _PREVIOUS;

        public:
            explicit ScopedGattServerInterface(esp_gatt_if_t interface) : _PREVIOUS(activeGattServerInterface) {
                activeGattServerInterface = interface;
            }

            ~ScopedGattServerInterface() {
                activeGattServerInterface = _PREVIOUS;
            }
    };

    void gattServerEventRelay(esp_gatts_cb_event_t event, esp_gatt_if_t interface, esp_ble_gatts_cb_param_t* param) {
        const esp_gatts_cb_t callback = frameworkGattServerCallback.load();
        if (callback == nullptr) { return; }

        const std::lock_guard<std::recursive_mutex> lock(securityMutex);
        const ScopedGattServerInterface context(event == ESP_GATTS_CONNECT_EVT ? interface : ESP_GATT_IF_NONE);
        callback(event, interface, param);
    }

    class DeniedSecurityCallbacks final : public BLESecurityCallbacks {
        public:
            uint32_t onPassKeyRequest() override { return 0; }
            void onPassKeyNotify(uint32_t) override {}
            bool onSecurityRequest() override { return false; }
            void onAuthenticationComplete(esp_ble_auth_cmpl_t) override {}
            bool onConfirmPIN(uint32_t) override { return false; }
    };

    DeniedSecurityCallbacks deniedSecurityCallbacks;

    class ScopedSecurityCallbacks {
        BLESecurityCallbacks* _PREVIOUS;
        public:
            explicit ScopedSecurityCallbacks(BLESecurityCallbacks* callbacks) : _PREVIOUS(activeSecurityCallbacks) {
                activeSecurityCallbacks = callbacks;
                BLEDevice::setSecurityCallbacks(callbacks);
            }

            ~ScopedSecurityCallbacks() {
                activeSecurityCallbacks = _PREVIOUS;
                BLEDevice::setSecurityCallbacks(_PREVIOUS);
            }
    };

    struct GapEventContext {
        esp_gap_ble_cb_event_t event;
        const esp_ble_gap_cb_param_t* param;
    };

    thread_local const GapEventContext* currentGapContext = nullptr;

    class ScopedGapContext {
        const GapEventContext _CONTEXT;
        const GapEventContext* _PREVIOUS;
        public:
            ScopedGapContext(esp_gap_ble_cb_event_t event, const esp_ble_gap_cb_param_t* param) : _CONTEXT{event, param}, _PREVIOUS(currentGapContext) {
                currentGapContext = &_CONTEXT;
            }

            ~ScopedGapContext() {
                currentGapContext = _PREVIOUS;
            }
    };

    void gapEventRelay(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* param) {
        const esp_gap_ble_cb_t callback = frameworkGapCallback.load();
        if (callback == nullptr) { return; }

        const std::lock_guard<std::recursive_mutex> lock(securityMutex);
        const ScopedGapContext context(event, param);
        esp_bd_addr_t address;

        if (!Bluetooth::detail::getSecurityPeerAddress(address)) {
            callback(event, param);
            return;
        }

        const BLEAddress remoteAddress(address);
        BLESecurityCallbacks* selected = nullptr;

        for (const SecurityPeer& peer : securityPeers) {
            if (peer.address == remoteAddress) {
                selected = peer.callbacks;
                break;
            }
        }

        if (event == ESP_GAP_BLE_SEC_REQ_EVT) {
            if (selected == nullptr || !Bluetooth::detail::configurePeerSecurity(selected))
                { selected = &deniedSecurityCallbacks; }
            else { authenticationOwner = selected; }
        } else if (event == ESP_GAP_BLE_PASSKEY_REQ_EVT || event == ESP_GAP_BLE_NC_REQ_EVT) {
            const bool denied = selected == nullptr || (authenticationOwner != nullptr && authenticationOwner != selected);

            if (denied) {
                if (event == ESP_GAP_BLE_PASSKEY_REQ_EVT)
                    { esp_ble_passkey_reply(address, false, 0); }
                else { esp_ble_confirm_reply(address, false); }
                selected = nullptr;
            } else { authenticationOwner = selected; }
        } else if (event == ESP_GAP_BLE_PASSKEY_NOTIF_EVT) {
            if (selected == nullptr || (authenticationOwner != nullptr && authenticationOwner != selected)) {
                esp_ble_gap_disconnect(address);
                selected = nullptr;
            } else { authenticationOwner = selected; }
        }

        const ScopedSecurityCallbacks selection(selected);
        callback(event, param);

        if (event == ESP_GAP_BLE_AUTH_CMPL_EVT && selected != nullptr && authenticationOwner == selected)
            { authenticationOwner = nullptr; }
    }

    bool configureRoleSecurity(uint32_t pinCode, Bluetooth::detail::SecurityRole role) {
        const std::lock_guard<std::recursive_mutex> lock(securityMutex);
        if (!BLEDevice::getInitialized() || pinCode > 999999U || authenticationOwner != nullptr)
            { return false; }

        const bool serverRole = role == Bluetooth::detail::SecurityRole::Server;
        esp_ble_auth_req_t authentication = ESP_LE_AUTH_REQ_SC_MITM_BOND;
        esp_ble_io_cap_t capability       = serverRole ? ESP_IO_CAP_OUT : ESP_IO_CAP_IN;
        uint8_t keySize                   = 16;
        uint8_t keys                      = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;

        const auto setKeySize     = [&]() { return esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &keySize, sizeof(keySize)) == ESP_OK; };
        const auto setInitialKeys = [&]() { return esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &keys,    sizeof(keys))    == ESP_OK; };

        if (!serverRole && !setKeySize())                                                                       { return false; }
        if (esp_ble_gap_set_security_param(ESP_BLE_SM_SET_STATIC_PASSKEY, &pinCode, sizeof(pinCode)) != ESP_OK) { return false; }
        if (!serverRole && !setInitialKeys())                                                                   { return false; }

        if (esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &authentication, sizeof(authentication)) != ESP_OK ||
            esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE,      &capability,     sizeof(capability))     != ESP_OK
        ) { return false; }
        if (!serverRole) { return true; }

        uint8_t acceptSpecified = ESP_BLE_ONLY_ACCEPT_SPECIFIED_AUTH_ENABLE;
        return
            setKeySize()     &&
            setInitialKeys() &&
            esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY,                    &keys,            sizeof(keys))            == ESP_OK &&
            esp_ble_gap_set_security_param(ESP_BLE_SM_ONLY_ACCEPT_SPECIFIED_SEC_AUTH, &acceptSpecified, sizeof(acceptSpecified)) == ESP_OK;
    }
}

namespace Bluetooth {
    namespace detail {
        bool isValidUUID(const std::string& value) {
            if (value.size() != 4 && value.size() != 8 && value.size() != 36)
                { return false; }

            for (size_t index = 0; index < value.size(); ++index) {
                const char character = value[index];
                const bool separator = value.size() == 36 && (index == 8 || index == 13 || index == 18 || index == 23);

                if (separator) {
                    if (character != '-')
                        { return false; }
                    continue;
                }

                const bool hexadecimal =
                    (character >= '0' && character <= '9') ||
                    (character >= 'a' && character <= 'f') ||
                    (character >= 'A' && character <= 'F');
                if (!hexadecimal)
                    { return false; }
            }
            return true;
        }

        BLEClient* acquireClientBackend() {
            const std::lock_guard<std::recursive_mutex> lock(securityMutex);
            if (!BLEDevice::getInitialized()) { return nullptr; }

            for (ClientBackendSlot& slot : clientBackends()) {
                if (slot.leased ||
                    (slot.backend != nullptr && slot.backend->isConnected())) {
                    continue;
                }

                if (slot.backend == nullptr) {
                    slot.backend = BLEDevice::createClient();
                    if (slot.backend == nullptr) { return nullptr; }
                }

                slot.backend->setClientCallbacks(nullptr);
                slot.leased = true;
                return slot.backend;
            }

            return nullptr;
        }

        bool releaseClientBackend(BLEClient* backend) {
            const std::lock_guard<std::recursive_mutex> lock(securityMutex);
            if (backend == nullptr)                         { return false; }

            for (ClientBackendSlot& slot : clientBackends()) {
                if (slot.backend != backend)                { continue; }
                if (!slot.leased || backend->isConnected()) { return false; }

                backend->setClientCallbacks(nullptr);
                slot.leased = false;
                return true;
            }
            return false;
        }

        std::recursive_mutex& backendMutex() {
            return securityMutex;
        }

        bool retainServerConnection(const BLEAddress& address) {
            if (activeGattServerInterface == ESP_GATT_IF_NONE) { return false; }
            BLEAddress peerAddress(address);
            return esp_ble_gatts_open(activeGattServerInterface, *peerAddress.getNative(), true) == ESP_OK;
        }

        bool initializeBLE() {
            if (!BLEDevice::getInitialized()) { BLEDevice::init(""); }
            if (!BLEDevice::getInitialized()) { return false; }

            BLEDevice::setEncryptionLevel(static_cast<esp_ble_sec_act_t>(0));
            return
                installRelay(frameworkGapCallback,        gapEventRelay,        esp_ble_gap_get_callback,   esp_ble_gap_register_callback)   &&
                installRelay(frameworkGattClientCallback, gattClientEventRelay, esp_ble_gattc_get_callback, esp_ble_gattc_register_callback) &&
                installRelay(frameworkGattServerCallback, gattServerEventRelay, esp_ble_gatts_get_callback, esp_ble_gatts_register_callback);
        }

        bool initializeBLE(const std::string& deviceName) {
            if (!initializeBLE())
                { return false; }
            return esp_ble_gap_set_device_name(deviceName.c_str()) == ESP_OK;
        }

        bool registerSecurityPeer(const BLEAddress& address, BLESecurityCallbacks* callbacks, const SecurityProfile& profile) {
            if (callbacks == nullptr || !BLEDevice::getInitialized() ||
                profile.pinCode > 999999U) {
                return false;
            }

            const std::lock_guard<std::recursive_mutex> lock(securityMutex);
            for (const SecurityPeer& peer : securityPeers) {
                if (peer.callbacks != callbacks && peer.address == address)
                    { return false; }
            }

            for (SecurityPeer& peer : securityPeers) {
                if (peer.callbacks == callbacks) {
                    peer.address = address;
                    peer.profile = profile;
                    return true;
                }
            }

            securityPeers.push_back({address, callbacks, profile});
            return true;
        }

        void unregisterSecurityPeer(BLESecurityCallbacks* callbacks) {
            const std::lock_guard<std::recursive_mutex> lock(securityMutex);
            if (authenticationOwner == callbacks)
                { authenticationOwner = nullptr; }

            for (auto entry = securityPeers.begin(); entry != securityPeers.end(); ++entry) {
                if (entry->callbacks == callbacks) {
                    securityPeers.erase(entry);
                    return;
                }
            }
        }

        bool getSecurityPeerAddress(esp_bd_addr_t address) {
            if (address == nullptr || currentGapContext == nullptr || currentGapContext->param == nullptr)
                { return false; }

            const esp_ble_gap_cb_param_t* param = currentGapContext->param;
            const uint8_t* source               = nullptr;

            switch (currentGapContext->event) {
                case ESP_GAP_BLE_SEC_REQ_EVT:
                case ESP_GAP_BLE_PASSKEY_REQ_EVT:
                    source = param->ble_security.ble_req.bd_addr;
                    break;
                case ESP_GAP_BLE_NC_REQ_EVT:
                case ESP_GAP_BLE_PASSKEY_NOTIF_EVT:
                    source = param->ble_security.key_notif.bd_addr;
                    break;
                case ESP_GAP_BLE_AUTH_CMPL_EVT:
                    source = param->ble_security.auth_cmpl.bd_addr;
                    break;
                default:
                    return false;
            }

            std::memcpy(address, source, sizeof(esp_bd_addr_t));
            return true;
        }

        bool configurePeerSecurity(BLESecurityCallbacks* callbacks) {
            if (callbacks == nullptr) { return false; }

            const std::lock_guard<std::recursive_mutex> lock(securityMutex);
            if (authenticationOwner != nullptr) { return authenticationOwner == callbacks; }

            for (const SecurityPeer& peer : securityPeers) {
                if (peer.callbacks != callbacks) { continue; }

                bool configured = false;
                switch (peer.profile.role) {
                    case SecurityRole::Client:
                        configured = configureClientSecurity(peer.profile.pinCode);
                        break;
                    case SecurityRole::Server:
                        configured = configureServerSecurity(peer.profile.pinCode);
                        break;
                    default:
                        return false;
                }

                if (configured && peer.profile.authenticationRequired)
                    { authenticationOwner = callbacks; }
                return configured;
            }
            return false;
        }

        bool configureClientSecurity(uint32_t pinCode) {
            return configureRoleSecurity(pinCode, SecurityRole::Client);
        }

        bool configureServerSecurity(uint32_t pinCode) {
            return configureRoleSecurity(pinCode, SecurityRole::Server);
        }
    }
}
