#include "../include/BTServer.h"
using namespace Bluetooth;

server::server() {
    this->_CHARACTERISTICS = new std::map<std::string, BLECharacteristic*>();
    this->setCallbacksServer();
    this->setCallbacksDescriptor();
    this->setCallbacksCharacteristic();
}

server::~server() {
    this->_SERVICE->stop();
    delete this->_SERVER;
    delete this->_SERVICE;
    delete this->_CHARACTERISTICS;
    delete this->_ADVERTISING;
    delete this->_CALLBACKS_SERVER;
    delete this->_CALLBACKS_DESCRIPTOR;
    delete this->_CALLBACKS_CHARACTERISTIC;
}

void server::setCallbacksServer(BLEServerCallbacks* callbacks) {
    this->_CALLBACKS_SERVER = callbacks;
}

void server::setCallbacksDescriptor(BLEDescriptorCallbacks* callbacks) {
    this->_CALLBACKS_DESCRIPTOR = callbacks;
}

void server::setCallbacksCharacteristic(BLECharacteristicCallbacks* callbacks) {
    this->_CALLBACKS_CHARACTERISTIC = callbacks;
}

void server::start(std::string serviceUUID, std::string name) {
    if (this->_SERVER != nullptr)
        { return; }
    if (this->_SERVICE != nullptr)
        { return; }
    if (this->_ADVERTISING != nullptr)
        { return; }

    BLEDevice::init(name);
    this->_SERVER = BLEDevice::createServer();
    this->_SERVER->setCallbacks(this->_CALLBACKS_SERVER);
    this->_SERVICE = this->_SERVER->createService(BLEUUID(serviceUUID));

    this->_ADVERTISING = BLEDevice::getAdvertising();
    this->_ADVERTISING->addServiceUUID(BLEUUID(serviceUUID));
    this->_ADVERTISING->setScanResponse(true);
    this->_ADVERTISING->setMinPreferred(0x06);  // functions that help with iPhone connections issue
    this->_ADVERTISING->setMinPreferred(0x12);
    BLEDevice::startAdvertising();

    this->_SERVICE->start();
}

void server::stop() {
    this->_ADVERTISING->stop();
    this->_SERVICE->stop();
    this->_CHARACTERISTICS->clear();
}

bool server::addCharacteristic(std::string characteristicUUID, std::string characteristicValue,
    std::map<std::string, std::string> descriptors, bool read, bool write, bool notify,
    bool broadcast, bool indicate, bool writeNR) const {
    /**
     * PROPERTY_READ
	 * PROPERTY_WRITE
     * PROPERTY_NOTIFY
     * PROPERTY_BROADCAST
     * PROPERTY_INDICATE
     * PROPERTY_WRITE_NR --> WRITE with No Response
     */
    bool exist = false;
    for (auto c = this->_CHARACTERISTICS->begin(); c != this->_CHARACTERISTICS->end() ; ++c) {
        if (c->first.compare(characteristicUUID) != 0)
            { continue; }
        exist = true;
        break;
    }
    if (exist)
        { return false; }

    BLECharacteristic *characteristic = this->_SERVICE->createCharacteristic(
        BLEUUID(characteristicUUID), BLECharacteristic::PROPERTY_READ);
    characteristic->setWriteProperty(write);
    characteristic->setNotifyProperty(notify);
    characteristic->setBroadcastProperty(broadcast);
    characteristic->setIndicateProperty(indicate);
    characteristic->setWriteNoResponseProperty(writeNR);
    if (!read)
        { characteristic->setReadProperty(false); }
    characteristic->setValue(characteristicValue);
    characteristic->setCallbacks(this->_CALLBACKS_CHARACTERISTIC);

    for (auto d = descriptors.begin(); d != descriptors.end(); ++d) {
        BLEDescriptor *descriptor = new BLEDescriptor(BLEUUID(d->first));
        descriptor->setAccessPermissions(ESP_GATT_PERM_READ); // ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE
        descriptor->setValue(d->second);
        descriptor->setCallbacks(this->_CALLBACKS_DESCRIPTOR);
        characteristic->addDescriptor(descriptor);
    }
    this->_CHARACTERISTICS->insert({characteristicUUID, characteristic});
    return characteristic->getValue().compare(characteristicValue) == 0;
}

bool server::ChangeCharacteristic(std::string characteristicUUID, std::string characteristicValue) const {
    for (auto c = this->_CHARACTERISTICS->begin(); c != this->_CHARACTERISTICS->end() ; ++c) {
        if (c->first.compare(characteristicUUID) != 0)
            { continue; }
        if (c->second->getValue().compare(characteristicValue) == 0)
            { return false; }
        c->second->setValue(characteristicValue);
        return c->second->getValue().compare(characteristicValue) == 0;
    }
    return false;
}