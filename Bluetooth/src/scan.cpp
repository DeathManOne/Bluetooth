/*
 * src/scan.cpp
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

#include "../include/BTScan.h"
#include "runtime.h"
using namespace Bluetooth;

scan *scan::_OWNER = nullptr;

scan::scan(int scanTime) {
    this->_SCAN      = nullptr;
    this->_SCAN_TIME = scanTime;
}

scan::~scan() {
    if (this->_SCAN != nullptr && _OWNER == this) {
        this->_SCAN->stop();
        _OWNER = nullptr;
    }
}

BLEScanResults scan::start(bool includeUnnamed) {
    if (this->_SCAN_TIME < 0)     { return BLEScanResults(); }
    if (!detail::initializeBLE()) { return BLEScanResults(); }

    this->_SCAN = BLEDevice::getScan();
    _OWNER      = this;

    this->_SCAN->setAdvertisedDeviceCallbacks(nullptr);
    this->_SCAN->setActiveScan(true);
    this->_SCAN->setInterval(500);
    this->_SCAN->setWindow(500);

    BLEScanResults results = this->_SCAN->start(this->_SCAN_TIME, false);
    if (includeUnnamed) { return results; }

    int index = 0;
    while (index < results.getCount()) {
        BLEAdvertisedDevice device = results.getDevice(index);

        if (device.getName().empty()) {
            this->_SCAN->erase(device.getAddress());
            results = this->_SCAN->getResults();
        } else { ++index; }
    }

    return results;
}

void scan::stop() const {
    if (this->_SCAN == nullptr || _OWNER != this) { return; }
    this->_SCAN->stop();
}

void scan::clear() const {
    if (this->_SCAN == nullptr || _OWNER != this) { return; }
    this->_SCAN->clearResults();
}
