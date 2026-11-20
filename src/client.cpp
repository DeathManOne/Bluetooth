/*
 * src/client.cpp
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

#include "../include/BTClient.h"
#include "runtime.h"

#include <esp_gatt_common_api.h>
#include <esp_gap_ble_api.h>
#include <esp32-hal.h>
#include <stdint.h>

using namespace Bluetooth;

void client::ClientCallbacks::onConnect(BLEClient* bleClient) {
    if (_PARENT._CALLBACKS_CLIENT != nullptr)
        { _PARENT._CALLBACKS_CLIENT->onConnect(bleClient); }
}

void client::ClientCallbacks::onDisconnect(BLEClient* bleClient) {
    detail::unregisterSecurityPeer(&_PARENT._SECURITY_CALLBACKS);
    _PARENT._LAST_DISCONNECT_AT.store(millis());
    if (_PARENT._CALLBACKS_CLIENT != nullptr)
        { _PARENT._CALLBACKS_CLIENT->onDisconnect(bleClient); }
}

uint32_t client::SecurityCallbacks::onPassKeyRequest() {
    return _PARENT._CALLBACKS_SECURITY != nullptr
        ? _PARENT._CALLBACKS_SECURITY->onPassKeyRequest()
        : _PARENT._PIN_CODE;
}

void client::SecurityCallbacks::onPassKeyNotify(uint32_t passKey) {
    if (_PARENT._CALLBACKS_SECURITY != nullptr)
        { _PARENT._CALLBACKS_SECURITY->onPassKeyNotify(passKey); }
}

bool client::SecurityCallbacks::onSecurityRequest() {
    return
        _PARENT._CALLBACKS_SECURITY == nullptr           ||
        _PARENT._CALLBACKS_SECURITY->onSecurityRequest();
}

bool client::SecurityCallbacks::onConfirmPIN(uint32_t pin) {
    return
        _PARENT._CALLBACKS_SECURITY != nullptr         &&
        _PARENT._CALLBACKS_SECURITY->onConfirmPIN(pin);
}

void client::SecurityCallbacks::onAuthenticationComplete(esp_ble_auth_cmpl_t result) {
    if (_PARENT._CLIENT == nullptr || !BLEAddress(result.bd_addr).equals(_PARENT._CLIENT->getPeerAddress()))
        { return; }
    if (_PARENT._CALLBACKS_SECURITY != nullptr)
        { _PARENT._CALLBACKS_SECURITY->onAuthenticationComplete(result); }
    _PARENT._AUTHENTICATION_STATE.store(result.success ? 1 : -1);
}

namespace {
    constexpr uint32_t reconnectIntervalMs = 5000U;
}

client::client(uint32_t pinCode, bool requestEncryption) : _CLIENT_CALLBACKS(*this), _SECURITY_CALLBACKS(*this), _REQUEST_ENCRYPTION(requestEncryption) {
    this->_PIN_CODE           = pinCode;
    this->_CLIENT             = nullptr;
    this->_DEVICE             = nullptr;
    this->_CHARACTERISTICS    = nullptr;
    this->_CALLBACKS_CLIENT   = nullptr;
    this->_CALLBACKS_NOTIFY   = nullptr;
    this->_CALLBACKS_SECURITY = nullptr;

    this->_TX = new std::map<std::string, BLERemoteCharacteristic*>();

    if (pinCode > 999999U)        { return; }
    if (!detail::initializeBLE()) { return; }

    this->_CLIENT = detail::acquireClientBackend();
}

client::~client() {
    const std::lock_guard<std::recursive_mutex> lock(detail::backendMutex());
    detail::unregisterSecurityPeer(&this->_SECURITY_CALLBACKS);

    if (this->_CLIENT != nullptr)
        { this->_CLIENT->setClientCallbacks(nullptr); }
    this->disconnect();

    delete this->_TX;
    delete this->_CALLBACKS_CLIENT;
    delete this->_CALLBACKS_NOTIFY;
    delete this->_CALLBACKS_SECURITY;

    detail::releaseClientBackend(this->_CLIENT);
    this->_CLIENT = nullptr;
}

void client::setCallbacksClient(BLEClientCallbacks* callbacks) {
    detail::replaceOwnedCallback(this->_CALLBACKS_CLIENT, callbacks, [this](BLEClientCallbacks*) {
        if (this->_CLIENT != nullptr)
            { this->_CLIENT->setClientCallbacks(&this->_CLIENT_CALLBACKS); }
    });
}

void client::setCallbacksSecurity() {
    this->setCallbacksSecurity(nullptr);
}

void client::setCallbacksSecurity(BLESecurityCallbacks* callbacks) {
    detail::replaceOwnedCallback(this->_CALLBACKS_SECURITY, callbacks);
}

void client::setCallbacksNotify(notify_callback* callbacks) {
    detail::replaceOwnedCallback(this->_CALLBACKS_NOTIFY, callbacks);
}

bool client::isConnected() const {
    return
        this->_CLIENT != nullptr     &&
        this->_CLIENT->isConnected();
}

void client::setAutoReconnect(bool enabled) {
    this->_AUTO_RECONNECT_ENABLED = enabled;
    this->_LAST_RECONNECT_AT      = millis();
}

void client::update() {
    if (!this->_AUTO_RECONNECT_ENABLED ||
        !this->_RECONNECT_ARMED        ||
        this->_CLIENT == nullptr       ||
        this->_CLIENT->isConnected()
    ) { return; }

    const uint32_t now = millis();
    if (static_cast<uint32_t>(now - this->_LAST_DISCONNECT_AT.load()) < reconnectIntervalMs ||
        static_cast<uint32_t>(now - this->_LAST_RECONNECT_AT)         < reconnectIntervalMs
    ) { return; }

    this->connectPeer(
        this->_RECONNECT_ADDRESS,
        this->_RECONNECT_MTU,
        this->_RECONNECT_SCAN_TIME,
        this->_RECONNECT_TIMEOUT,
        this->_RECONNECT_SERVICE_UUID
    );
    this->_LAST_RECONNECT_AT = millis();
}

bool client::connect(std::string address, int MTU, int scanTime, int connectTimeout, std::string serviceUUID) {
    if (!serviceUUID.empty()            && !detail::isValidUUID(serviceUUID)) { return false; }
    if (this->_CLIENT == nullptr        || this->_CLIENT->isConnected())      { return false; }
    if (MTU < ESP_GATT_DEF_BLE_MTU_SIZE || MTU > ESP_GATT_MAX_MTU_SIZE)       { return false; }
    if (scanTime       <= 0)                                                  { return false; }
    if (connectTimeout <= 0)                                                  { return false; }
    if (address.size() != 17)                                                 { return false; }

    for (size_t index = 0; index < address.size(); ++index) {
        const char character = address[index];

        if (index % 3 == 2) {
            if (character != ':')
                { return false; }
        } else {
            const bool hexadecimal =
                (character >= '0' && character <= '9') ||
                (character >= 'a' && character <= 'f') ||
                (character >= 'A' && character <= 'F');
            if (!hexadecimal) { return false; }
        }
    }

    const uint64_t requestedTimeoutMs = static_cast<uint64_t>(connectTimeout) * 1000ULL;
    if (requestedTimeoutMs > UINT32_MAX) { return false; }

    this->_RECONNECT_ARMED = false;
    const bool connected   = this->connectPeer(address, MTU, scanTime, connectTimeout, serviceUUID);

    if (connected) {
        this->_RECONNECT_ADDRESS      = address;
        this->_RECONNECT_SERVICE_UUID = serviceUUID;
        this->_RECONNECT_MTU          = MTU;
        this->_RECONNECT_SCAN_TIME    = scanTime;
        this->_RECONNECT_TIMEOUT      = connectTimeout;
        this->_LAST_RECONNECT_AT      = millis();
        this->_RECONNECT_ARMED        = true;
    }
    return connected;
}

bool client::connectPeer(const std::string& address, int MTU, int scanTime, int connectTimeout, const std::string& serviceUUID) {
    if (this->_CLIENT == nullptr || this->_CLIENT->isConnected()) { return false; }

    const BLEAddress targetAddress(address);
    const uint32_t timeoutMs = static_cast<uint32_t>(static_cast<uint64_t>(connectTimeout) * 1000ULL);

    this->clearDiscovery();
    this->_CLIENT->setClientCallbacks(&this->_CLIENT_CALLBACKS);

    Bluetooth::scan scan(scanTime);
    BLEScanResults devices = scan.start();

    for (int d = 0; d < devices.getCount(); d++) {
        if (!devices.getDevice(d).getAddress().equals(targetAddress))
            { continue; }
        this->_DEVICE = new BLEAdvertisedDevice(devices.getDevice(d));
        break;
    }

    scan.stop();
    if (this->_DEVICE == nullptr)
        { return false; }
    const detail::SecurityProfile profile {detail::SecurityRole::Client, this->_PIN_CODE, this->_REQUEST_ENCRYPTION};

    bool connected           = false;
    const uint32_t startedAt = millis();

    do {
        if (!detail::registerSecurityPeer(targetAddress, &this->_SECURITY_CALLBACKS, profile))
            { return false; }

        if (!detail::configurePeerSecurity(&this->_SECURITY_CALLBACKS)) {
            detail::unregisterSecurityPeer(&this->_SECURITY_CALLBACKS);
            return false;
        }

        detail::prepareClientConnection();
        this->_AUTHENTICATION_STATE.store(0);
        connected = this->_CLIENT->connect(this->_DEVICE);
    } while (!connected && static_cast<uint32_t>(millis() - startedAt) < timeoutMs);

    if (!this->_CLIENT->isConnected()) {
        detail::unregisterSecurityPeer(&this->_SECURITY_CALLBACKS);
        return false;
    }

    if (this->_REQUEST_ENCRYPTION) {
        BLEAddress peerAddress = this->_CLIENT->getPeerAddress();

        if (esp_ble_set_encryption(*peerAddress.getNative(), ESP_BLE_SEC_ENCRYPT_MITM) != ESP_OK) {
            this->disconnectPeer();
            return false;
        }

        const uint32_t authenticationStartedAt = millis();
        while (
            this->isConnected()                     &&
            this->_AUTHENTICATION_STATE.load() == 0 &&
            static_cast<uint32_t>(millis() - authenticationStartedAt) < timeoutMs
        ) { delay(10); }

        if (!this->isConnected() || this->_AUTHENTICATION_STATE.load() != 1) {
            this->disconnectPeer();
            return false;
        }
    }

    this->_CLIENT->setMTU(MTU);
    this->_CLIENT->getServices();

    if (!this->_CLIENT->isConnected())
        { return false; }
    this->characteristics(serviceUUID);
    return this->_CLIENT->isConnected();
}

bool client::disconnect() {
    this->_RECONNECT_ARMED = false;
    return this->disconnectPeer();
}

bool client::disconnectPeer() {
    if (this->_CLIENT != nullptr && this->_CLIENT->isConnected())
        { this->_CLIENT->disconnect(); }
    this->clearDiscovery();

    return
        this->_CLIENT == nullptr      ||
        !this->_CLIENT->isConnected();
}

void client::clearCharacteristicReferences() {
    this->_CHARACTERISTICS = nullptr;
    this->_TX->clear();
}

void client::clearDiscovery() {
    this->clearCharacteristicReferences();
    delete this->_DEVICE;
    this->_DEVICE = nullptr;
}

void client::characteristics(const std::string& serviceUUID) {
    this->clearCharacteristicReferences();

    BLERemoteService* remote = nullptr;
    if (!serviceUUID.empty())                  { remote = this->_CLIENT->getService(BLEUUID(serviceUUID)); }
    else if (this->_DEVICE->haveServiceUUID()) { remote = this->_CLIENT->getService(this->_DEVICE->getServiceUUID()); }

    if (remote == nullptr)                     { return; }
    this->_CHARACTERISTICS = remote->getCharacteristics();

    for (auto c = this->_CHARACTERISTICS->begin(); c != this->_CHARACTERISTICS->end() ; ++c) {
        if (
            (c->second->canNotify() || c->second->canIndicate()) &&
            this->_CALLBACKS_NOTIFY != nullptr                   &&
            *this->_CALLBACKS_NOTIFY
        ) { c->second->registerForNotify(*this->_CALLBACKS_NOTIFY, c->second->canNotify()); }
        if (c->second->canWrite())
            { this->addTx(c->first, c->second); }
        if (c->second->canWriteNoResponse())
            { this->addTx(c->first, c->second); }
    }
}

void client::addTx(std::string uuid, BLERemoteCharacteristic *characteristic) {
    for (auto t = this->_TX->begin(); t != this->_TX->end(); ++t) {
        if (t->first.compare(uuid) == 0)
            { return; }
    }
    this->_TX->insert({uuid, characteristic});
}

std::string client::readCharacteristic(const std::string& uuid) const {
    if (!detail::isValidUUID(uuid) || !this->isConnected() || this->_CHARACTERISTICS == nullptr)
        { return {}; }
    BLERemoteCharacteristic* characteristic = detail::findCharacteristic(this->_CHARACTERISTICS, BLEUUID(uuid));

    if (characteristic == nullptr || !characteristic->canRead()) { return {}; }
    return characteristic->readValue();
}

bool client::setSubscription(const std::string& uuid, bool enabled) {
    if (!detail::isValidUUID(uuid) || !this->isConnected() || this->_CHARACTERISTICS == nullptr) { return false; }
    if (enabled && (this->_CALLBACKS_NOTIFY == nullptr || !*this->_CALLBACKS_NOTIFY))            { return false; }

    BLERemoteCharacteristic* characteristic = detail::findCharacteristic(this->_CHARACTERISTICS, BLEUUID(uuid));
    if (characteristic == nullptr)                                                               { return false; }
    if (!characteristic->canNotify() && !characteristic->canIndicate())                          { return false; }

    BLERemoteDescriptor* descriptor = characteristic->getDescriptor(BLEUUID(uint16_t(0x2902)));
    if (descriptor == nullptr)                                                                   { return false; }

    const bool notifications = characteristic->canNotify();
    characteristic->registerForNotify(enabled ? *this->_CALLBACKS_NOTIFY : notify_callback{}, notifications);
    if (!this->isConnected())                                                                    { return false; }

    const std::string value = descriptor->readValue();
    const uint8_t expected  = enabled ? (notifications ? 0x01 : 0x02) : 0x00;

    return
        this->isConnected()                        &&
        value.size() == 2                          &&
        static_cast<uint8_t>(value[0]) == expected &&
        value[1] == '\0';
}

void client::sendCommand(std::string uuid, std::string data) const {
    if (!detail::isValidUUID(uuid))                                { return; }
    if (data.size()    > ESP_GATT_MAX_ATTR_LEN)                    { return; }
    if (this->_CLIENT == nullptr || !this->_CLIENT->isConnected()) { return; }

    BLERemoteCharacteristic* characteristic = detail::findCharacteristic(this->_TX, BLEUUID(uuid));
    if (characteristic == nullptr)                                 { return; }
    characteristic->writeValue(data, characteristic->canWrite());
    delay(250);
}
