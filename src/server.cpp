/*
 * src/server.cpp
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

#include "../include/BTServer.h"
#include "runtime.h"

#include <BLE2902.h>
#include <esp_gap_ble_api.h>

using namespace Bluetooth;

BLEServer* server::_SHARED_SERVER = nullptr;
server* server::_OWNER            = nullptr;

void server::ServerCallbacks::onConnect(BLEServer*) {}

void server::ServerCallbacks::onConnect(BLEServer* bleServer, esp_ble_gatts_cb_param_t* param) {
    const detail::SecurityProfile profile {
        detail::SecurityRole::Server,
        _PARENT._PIN_CODE,
        _PARENT._PIN_ENABLED
    };

    if (_PARENT._INCOMING_CONN_ID != UINT16_MAX || !detail::registerSecurityPeer(BLEAddress(param->connect.remote_bda), &_PARENT._SECURITY_CALLBACKS, profile)) {
        bleServer->disconnect(param->connect.conn_id);
        return;
    }

    if (!detail::retainServerConnection(BLEAddress(param->connect.remote_bda))) {
        detail::unregisterSecurityPeer(&_PARENT._SECURITY_CALLBACKS);
        bleServer->disconnect(param->connect.conn_id);
        return;
    }

    if (_PARENT._PIN_ENABLED && (!detail::configurePeerSecurity(&_PARENT._SECURITY_CALLBACKS) || esp_ble_set_encryption(param->connect.remote_bda, ESP_BLE_SEC_ENCRYPT_MITM) != ESP_OK)) {
        detail::unregisterSecurityPeer(&_PARENT._SECURITY_CALLBACKS);
        bleServer->disconnect(param->connect.conn_id);
        return;
    }

    uint8_t disabledSubscription[2] = {0, 0};
    for (BLEDescriptor* descriptor : _PARENT._DESCRIPTORS) {
        if (descriptor->getUUID().equals(BLEUUID(uint16_t(0x2902)))) {
            descriptor->setValue(disabledSubscription, sizeof(disabledSubscription));
        }
    }

    _PARENT._INCOMING_CONN_ID = param->connect.conn_id;
    if (_PARENT._CALLBACKS_SERVER != nullptr) {
        _PARENT._CALLBACKS_SERVER->onConnect(bleServer);
        _PARENT._CALLBACKS_SERVER->onConnect(bleServer, param);
    }
}

void server::ServerCallbacks::onDisconnect(BLEServer*) {}

void server::ServerCallbacks::onDisconnect(BLEServer* bleServer, esp_ble_gatts_cb_param_t* param) {
    if (param->disconnect.conn_id != _PARENT._INCOMING_CONN_ID) {
        if (_PARENT._INCOMING_CONN_ID == UINT16_MAX)
            { _PARENT._RESTART_ADVERTISING.store(true); }
        return;
    }

    detail::unregisterSecurityPeer(&_PARENT._SECURITY_CALLBACKS);
    _PARENT._INCOMING_CONN_ID = UINT16_MAX;
    _PARENT._RESTART_ADVERTISING.store(true);

    if (_PARENT._CALLBACKS_SERVER != nullptr) {
        _PARENT._CALLBACKS_SERVER->onDisconnect(bleServer);
        _PARENT._CALLBACKS_SERVER->onDisconnect(bleServer, param);
    }
}

void server::ServerCallbacks::onMtuChanged(BLEServer* bleServer, esp_ble_gatts_cb_param_t* param) {
    if (_PARENT._CALLBACKS_SERVER != nullptr)
        { _PARENT._CALLBACKS_SERVER->onMtuChanged(bleServer, param); }
}

server::server() : _SERVER_CALLBACKS(*this), _SECURITY_CALLBACKS(*this) {
    this->_PIN_CODE                 = 0;
    this->_REMAINING_HANDLES        = 0;
    this->_PIN_ENABLED              = false;
    this->_RUNNING                  = false;
    this->_SERVER                   = nullptr;
    this->_SERVICE                  = nullptr;
    this->_ADVERTISING              = nullptr;
    this->_CALLBACKS_SERVER         = nullptr;
    this->_CALLBACKS_DESCRIPTOR     = nullptr;
    this->_CALLBACKS_CHARACTERISTIC = nullptr;
    this->_CHARACTERISTICS          = new std::map<std::string, BLECharacteristic*>();
}

server::~server() {
    detail::unregisterSecurityPeer(&this->_SECURITY_CALLBACKS);

    if (this->_ADVERTISING != nullptr) { this->_ADVERTISING->stop(); }
    if (this->_SERVER      != nullptr) {
        this->_SERVER->setCallbacks(nullptr);
        if (this->_SERVICE != nullptr)
            { this->_SERVER->removeService(this->_SERVICE); }
    }

    for (BLEDescriptor* descriptor : this->_DESCRIPTORS) { delete descriptor; }
    for (const auto& entry : *this->_CHARACTERISTICS)    { delete entry.second; }

    delete this->_SERVICE;
    delete this->_CHARACTERISTICS;
    delete this->_CALLBACKS_SERVER;
    delete this->_CALLBACKS_DESCRIPTOR;
    delete this->_CALLBACKS_CHARACTERISTIC;

    if (_OWNER == this)
        { _OWNER = nullptr; }
}

void server::setCallbacksServer(BLEServerCallbacks* callbacks) {
    detail::replaceOwnedCallback(this->_CALLBACKS_SERVER, callbacks);
}

void server::setCallbacksDescriptor(BLEDescriptorCallbacks* callbacks) {
    detail::replaceOwnedCallback(this->_CALLBACKS_DESCRIPTOR, callbacks, [this](BLEDescriptorCallbacks* replacement) {
        for (BLEDescriptor* descriptor : this->_DESCRIPTORS)
            { descriptor->setCallbacks(replacement); }
    });
}

void server::setCallbacksCharacteristic(BLECharacteristicCallbacks* callbacks) {
    detail::replaceOwnedCallback(this->_CALLBACKS_CHARACTERISTIC, callbacks, [this](BLECharacteristicCallbacks* replacement) {
        for (const auto& entry : *this->_CHARACTERISTICS)
            { entry.second->setCallbacks(replacement); }
    });
}

bool server::setPIN(uint32_t pinCode) {
    if (this->_SERVER != nullptr || pinCode > 999999U) { return false; }

    this->_PIN_CODE    = pinCode;
    this->_PIN_ENABLED = true;

    return true;
}

void server::init(std::string serviceUUID, std::string name, uint16_t numHandles) {
    if (!detail::isValidUUID(serviceUUID))                                       { return; }
    if (numHandles         == 0)                                                 { return; }
    if (this->_SERVER      != nullptr)                                           { return; }
    if (this->_SERVICE     != nullptr)                                           { return; }
    if (this->_ADVERTISING != nullptr)                                           { return; }
    if (_OWNER             != nullptr)                                           { return; }
    if (!detail::initializeBLE(name))                                            { return; }
    if (this->_PIN_ENABLED && !detail::configureServerSecurity(this->_PIN_CODE)) { return; }

    if (_SHARED_SERVER == nullptr) { _SHARED_SERVER = BLEDevice::createServer(); }
    this->_SERVER = _SHARED_SERVER;
    _OWNER        = this;

    this->_SERVER->setCallbacks(&this->_SERVER_CALLBACKS);
    this->_SERVICE           = this->_SERVER->createService(BLEUUID(serviceUUID), numHandles);
    this->_REMAINING_HANDLES = numHandles - 1;

    this->_ADVERTISING = BLEDevice::getAdvertising();
    BLEAdvertisementData advertisement;
    advertisement.setFlags(ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT);
    advertisement.setCompleteServices(BLEUUID(serviceUUID));
    this->_ADVERTISING->setAdvertisementData(advertisement);
    this->_ADVERTISING->setScanResponse(true);
    this->_ADVERTISING->setMinPreferred(0x12);
}

void server::start() {
    if (this->_RUNNING)                                             { return; }
    if (this->_SERVICE == nullptr || this->_ADVERTISING == nullptr) { return; }

    this->_RESTART_ADVERTISING.store(false);
    this->_SERVICE->start();
    this->_ADVERTISING->start();
    this->_RUNNING = true;
}

void server::startAdvertising() {
    if (!this->_RUNNING || this->_ADVERTISING == nullptr)
        { return; }
    this->_ADVERTISING->start();
}

void server::update() {
    if (this->_RESTART_ADVERTISING.exchange(false))
        { this->startAdvertising(); }
}

void server::stop() {
    this->_RESTART_ADVERTISING.store(false);

    if (!this->_RUNNING)               { return; }
    if (this->_ADVERTISING != nullptr) { this->_ADVERTISING->stop(); }
    if (this->_SERVICE     != nullptr) { this->_SERVICE->stop(); }

    this->_RUNNING = false;
}

bool server::addCharacteristic(std::string characteristicUUID, std::string characteristicValue, std::map<std::string, std::string> descriptors, bool read, bool write, bool notify, bool broadcast, bool indicate, bool writeNR) {
    if (this->_SERVICE == nullptr || this->_RUNNING)                { return false; }
    if (!detail::isValidUUID(characteristicUUID))                   { return false; }
    if (characteristicValue.size() > ESP_GATT_MAX_ATTR_LEN)         { return false; }

    for (const auto& entry : descriptors) {
        if (!detail::isValidUUID(entry.first))                      { return false; }
        if (entry.second.size() > ESP_GATT_MAX_ATTR_LEN)            { return false; }
        if (BLEUUID(entry.first).equals(BLEUUID(uint16_t(0x2902)))) { return false; }
    }

    if (detail::findCharacteristic(this->_CHARACTERISTICS, BLEUUID(characteristicUUID)) != nullptr)
        { return false; }
    const size_t requiredHandles = 2 + descriptors.size() + ((notify || indicate) ? 1 : 0);

    if (requiredHandles > this->_REMAINING_HANDLES)
        { return false; }
    BLECharacteristic *characteristic = this->_SERVICE->createCharacteristic(BLEUUID(characteristicUUID), BLECharacteristic::PROPERTY_READ);

    characteristic->setWriteProperty(write);
    characteristic->setNotifyProperty(notify);
    characteristic->setBroadcastProperty(broadcast);
    characteristic->setIndicateProperty(indicate);
    characteristic->setWriteNoResponseProperty(writeNR);

    if (!read)              { characteristic->setReadProperty(false); }
    if (this->_PIN_ENABLED) { characteristic->setAccessPermissions(ESP_GATT_PERM_READ_ENC_MITM | ESP_GATT_PERM_WRITE_ENC_MITM); }

    characteristic->setValue(characteristicValue);
    characteristic->setCallbacks(this->_CALLBACKS_CHARACTERISTIC);

    if (notify || indicate) {
        BLE2902* subscription = new BLE2902();
        subscription->setAccessPermissions(
            this->_PIN_ENABLED
                ? (ESP_GATT_PERM_READ_ENC_MITM | ESP_GATT_PERM_WRITE_ENC_MITM)
                : (ESP_GATT_PERM_READ          | ESP_GATT_PERM_WRITE)
        );
        subscription->setCallbacks(this->_CALLBACKS_DESCRIPTOR);

        characteristic->addDescriptor(subscription);
        this->_DESCRIPTORS.push_back(subscription);
    }

    for (auto d = descriptors.begin(); d != descriptors.end(); ++d) {
        BLEDescriptor *descriptor = new BLEDescriptor(BLEUUID(d->first), ESP_GATT_MAX_ATTR_LEN);

        descriptor->setAccessPermissions(this->_PIN_ENABLED ? ESP_GATT_PERM_READ_ENC_MITM : ESP_GATT_PERM_READ);
        descriptor->setValue(d->second);
        descriptor->setCallbacks(this->_CALLBACKS_DESCRIPTOR);

        characteristic->addDescriptor(descriptor);
        this->_DESCRIPTORS.push_back(descriptor);
    }

    this->_CHARACTERISTICS->insert({characteristicUUID, characteristic});
    this->_REMAINING_HANDLES -= static_cast<uint16_t>(requiredHandles);

    return characteristic->getValue().compare(characteristicValue) == 0;
}

bool server::ChangeCharacteristic(std::string characteristicUUID, std::string characteristicValue) const {
    if (!detail::isValidUUID(characteristicUUID))
        { return false; }
    BLECharacteristic* characteristic = detail::findCharacteristic(this->_CHARACTERISTICS, BLEUUID(characteristicUUID));
    if (characteristic == nullptr || characteristic->getValue().compare(characteristicValue) == 0)
        { return false; }
    characteristic->setValue(characteristicValue);
    return characteristic->getValue().compare(characteristicValue) == 0;
}
