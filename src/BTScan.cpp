#include "../include/BTScan.h"
using namespace Bluetooth;

scan::scan(int scanTime) {
    this->_SCAN_TIME = new int(scanTime);
    this->setCallbacksPeriodicScan();
    this->setCallbacksExtAdvertising();
    this->setCallbacksAdvertisedDevice();
}

scan::~scan() {
    delete this->_SCAN_TIME;
    delete this->_SCAN;
    delete this->_CALLBACKS_PERIODIC_SCAN;
    delete this->_CALLBACKS_EXT_ADVERTISING;
    delete this->_CALLBACKS_ADVERTISED_DEVICE;
}

void scan::setCallbacksPeriodicScan(BLEPeriodicScanCallbacks* callbacks) {
    this->_CALLBACKS_PERIODIC_SCAN = callbacks;
}

void scan::setCallbacksExtAdvertising(BLEExtAdvertisingCallbacks* callbacks) {
    this->_CALLBACKS_EXT_ADVERTISING = callbacks;
}

void scan::setCallbacksAdvertisedDevice(BLEAdvertisedDeviceCallbacks* callbacks) {
    this->_CALLBACKS_ADVERTISED_DEVICE = callbacks;
}

BLEScanResults scan::start() {
    BLEDevice::init("");
    this->_SCAN = BLEDevice::getScan();
    this->_SCAN->setAdvertisedDeviceCallbacks(this->_CALLBACKS_ADVERTISED_DEVICE);
    this->_SCAN->setExtendedScanCallback(this->_CALLBACKS_EXT_ADVERTISING);
    this->_SCAN->setPeriodicScanCallback(this->_CALLBACKS_PERIODIC_SCAN);
    this->_SCAN->setActiveScan(true);
    this->_SCAN->setInterval(500);
    this->_SCAN->setWindow(500);
    return this->_SCAN->start(*this->_SCAN_TIME, false);
}

void scan::stop() const {
    this->clear();
    this->_SCAN->stop();
}

void scan::clear() const {
    this->_SCAN->clearResults();
}