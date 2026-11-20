/*
 * examples/client.ino
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
#include <BTClient.h>
#include <BTScan.h>
#include <string>

namespace {
    constexpr bool usePIN               = true;
    constexpr uint32_t pairingPIN       = 246810;
    constexpr char serverName[]         = "ESP32-BLE-EXAMPLE";
    constexpr char serviceUUID[]        = "12345678-1234-1234-1234-1234567890bb";
    constexpr char characteristicUUID[] = "12345678-1234-1234-1234-1234567890bc";

    Bluetooth::client* bleClient = nullptr;

    class ConnectionCallbacks final : public BLEClientCallbacks {
        void onConnect(BLEClient*) override { Serial.println("REMOTE LINK CONNECTED"); }
        void onDisconnect(BLEClient*) override { Serial.println("REMOTE LINK DISCONNECTED"); }
    };

    void printHelp() {
        Serial.println("c: find and connect to ESP32-BLE-EXAMPLE");
        Serial.println("r: read the characteristic");
        Serial.println("w: write CLIENT EXAMPLE");
        Serial.println("0: disable notifications");
        Serial.println("1: enable notifications");
        Serial.println("d: request disconnection");
        Serial.println("i: show connection status");
        Serial.println("Unexpected link loss: automatic retry after 5 seconds; 'd' cancels retries.");
    }

    std::string findServer() {
        std::string address;
        Bluetooth::scan scanner(5);
        BLEScanResults results = scanner.start(false);
        unsigned int matches   = 0;

        for (int index = 0; index < results.getCount(); ++index) {
            BLEAdvertisedDevice device = results.getDevice(index);
            if (device.getName() == serverName) {
                address = device.getAddress().toString();
                ++matches;
            }
        }

        scanner.clear();
        if (matches != 1) {
            Serial.println(matches == 0
                ? "SERVER NOT FOUND"
                : "MULTIPLE SERVERS: keep only one example server powered");
            return {};
        }
        return address;
    }
}

void setup() {
    Serial.begin(115200);

    const uint32_t startedAt = millis();
    while (!Serial && static_cast<uint32_t>(millis() - startedAt) < 3000U)
        { delay(10); }
    static Bluetooth::client client(pairingPIN, usePIN);

    client.setCallbacksClient(new ConnectionCallbacks());
    client.setCallbacksNotify(new notify_callback(
        [](BLERemoteCharacteristic*, uint8_t* data, size_t length, bool isNotify) {
            Serial.printf("%s: %u byte(s): ",
                isNotify ? "NOTIFICATION" : "INDICATION",
                static_cast<unsigned int>(length));
            Serial.write(data, length);
            Serial.println();
        }
    ));
    bleClient = &client;
    printHelp();
}

void loop() {
    if (!Serial.available()) {
        bleClient->update();
        delay(10);
        return;
    }

    const int command = Serial.read();
    if (command == '\r' || command == '\n') { return; }
    if (command == 'i') {
        Serial.println(bleClient->isConnected() ? "CONNECTED" : "DISCONNECTED");
        return;
    }
    if (command == 'd') {
        bleClient->disconnect();
        Serial.println("DISCONNECT REQUESTED: use 'i' to check completion");
        return;
    }
    if (command == 'c') {
        if (bleClient->isConnected()) {
            Serial.println("ALREADY CONNECTED");
            return;
        }

        Serial.println("SCAN START");
        const std::string address = findServer();
        if (address.empty()) { return; }

        Serial.printf("SERVER FOUND: %s\n", address.c_str());
        Serial.println("CONNECT START");

        const bool connected = bleClient->connect(address, 23, 5, 10, serviceUUID);
        Serial.println(connected ? "CONNECT SUCCEEDED" : "CONNECT FAILED");
        return;
    }

    if (command != 'r' && command != 'w' && command != '0' && command != '1') {
        printHelp();
        return;
    }
    if (!bleClient->isConnected()) {
        Serial.println("Connect with 'c' first.");
        return;
    }
    if (command == 'r') {
        const std::string value = bleClient->readCharacteristic(characteristicUUID);
        Serial.printf("READ: %u byte(s): ", static_cast<unsigned int>(value.size()));
        Serial.write(reinterpret_cast<const uint8_t*>(value.data()), value.size());
        Serial.println();
    } else if (command == 'w') {
        bleClient->sendCommand(characteristicUUID, "CLIENT EXAMPLE");
        Serial.println("WRITE REQUESTED");
    } else {
        const bool confirmed = bleClient->setSubscription(characteristicUUID, command == '1');
        Serial.println(confirmed
            ? "SUBSCRIPTION CONFIGURATION CONFIRMED"
            : "SUBSCRIPTION CONFIGURATION NOT CONFIRMED"
        );
    }
}
