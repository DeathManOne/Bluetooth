/*
 * examples/server.ino
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

#include <Arduino.h>
#include <BTServer.h>
#include <atomic>

namespace {
    constexpr bool usePIN               = true;
    constexpr uint32_t pairingPIN       = 246810;
    constexpr char serviceUUID[]        = "12345678-1234-1234-1234-1234567890bb";
    constexpr char characteristicUUID[] = "12345678-1234-1234-1234-1234567890bc";

    Bluetooth::server bleServer;
    std::atomic<BLECharacteristic*> pendingNotification{nullptr};

    class CharacteristicCallbacks : public BLECharacteristicCallbacks {
        public:
            void onWrite(BLECharacteristic* characteristic, esp_ble_gatts_cb_param_t*) override {
                const std::string value = characteristic->getValue();
                Serial.print("WRITE RECEIVED: ");
                Serial.write(reinterpret_cast<const uint8_t*>(value.data()), value.size());
                Serial.println();
                pendingNotification.store(characteristic);
            }
    };
}

void setup() {
    Serial.begin(115200);

    const uint32_t startedAt = millis();
    while (!Serial && static_cast<uint32_t>(millis() - startedAt) < 3000U) { delay(10); }

    if (usePIN && !bleServer.setPIN(pairingPIN)) {
        Serial.println("ERROR: PIN configuration rejected");
        return;
    }

    bleServer.setCallbacksCharacteristic(new CharacteristicCallbacks());
    bleServer.init(serviceUUID, "ESP32-BLE-EXAMPLE", 5);

    const bool added = bleServer.addCharacteristic(
        characteristicUUID, "BLE SERVER OK",
        {{"2901", "Read/write and notify"}},
        true,   // Read.
        true,   // Write with response.
        true,   // Notify.
        false,  // Broadcast.
        false,  // Indicate.
        false   // Write without response.
    );

    if (!added) {
        Serial.println("ERROR: characteristic creation failed");
        return;
    }

    bleServer.start();

    Serial.println("SERVER START REQUESTED");
    Serial.println("Connect to ESP32-BLE-EXAMPLE using a BLE client.");
    Serial.println("Read the value, enable notifications, then write text.");
}

void loop() {
    bleServer.update();
    BLECharacteristic* characteristic = pendingNotification.exchange(nullptr);
    if (characteristic != nullptr) { characteristic->notify(); }
    delay(10);
}
