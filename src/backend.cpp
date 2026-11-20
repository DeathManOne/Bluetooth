/*
 * src/backend.cpp
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
#include <BLERemoteCharacteristic.h>

namespace Bluetooth {
    namespace detail {
        void prepareClientConnection() {
            const std::lock_guard<std::recursive_mutex> lock(backendMutex());
            if (BLEDevice::m_appId == ESP_GATT_IF_NONE) { ++BLEDevice::m_appId; }
        }
    }
}

#if defined(BLUETOOTH_REGISTRY_WRAPPING) && BLUETOOTH_REGISTRY_WRAPPING

namespace {
    template<typename Function, typename... Arguments>
    void withRegistryLock(Function function, Arguments... arguments) {
        const std::lock_guard<std::recursive_mutex> lock(Bluetooth::detail::backendMutex());
        function(arguments...);
    }
}

extern "C" {
    void __real__ZN9BLEDevice13addPeerDeviceEPvbt(void* peer, bool clientRole, uint16_t connectionId);

    void __real__ZN9BLEDevice16updatePeerDeviceEPvbt(void* peer, bool clientRole, uint16_t connectionId);

    void __real__ZN9BLEDevice16removePeerDeviceEtb(uint16_t connectionId, bool clientRole);

    void __wrap__ZN9BLEDevice13addPeerDeviceEPvbt(void* peer, bool clientRole, uint16_t connectionId) {
        withRegistryLock(__real__ZN9BLEDevice13addPeerDeviceEPvbt, peer, clientRole, connectionId);
    }

    void __wrap__ZN9BLEDevice16updatePeerDeviceEPvbt(void* peer, bool clientRole, uint16_t connectionId) {
        withRegistryLock(__real__ZN9BLEDevice16updatePeerDeviceEPvbt, peer, clientRole, connectionId);
    }

    void __wrap__ZN9BLEDevice16removePeerDeviceEtb(uint16_t connectionId, bool clientRole) {
        withRegistryLock(__real__ZN9BLEDevice16removePeerDeviceEtb, connectionId, clientRole);
    }
}

#endif

#if defined(BLUETOOTH_REMOTE_CLEANUP_WRAPPING) && BLUETOOTH_REMOTE_CLEANUP_WRAPPING

extern "C" {
    void __real__ZN23BLERemoteCharacteristicD1Ev(BLERemoteCharacteristic* characteristic);

    void __wrap__ZN23BLERemoteCharacteristicD1Ev(BLERemoteCharacteristic* characteristic) {
        const std::lock_guard<std::recursive_mutex> lock(Bluetooth::detail::backendMutex());
        auto* descriptors = characteristic->getDescriptors();
        for (const auto& entry : *descriptors)
            { delete entry.second; }
        descriptors->clear();
        __real__ZN23BLERemoteCharacteristicD1Ev(characteristic);
    }
}

#endif
