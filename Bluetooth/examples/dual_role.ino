/*
 * examples/dual_role.ino
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
#include <BTServer.h>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace {
    constexpr char localName[]               = "ESP32-BLE-DUAL";
    constexpr uint32_t localPIN              = 135790;
    constexpr char localServiceUUID[]        = "22345678-1234-1234-1234-1234567890bb";
    constexpr char localCharacteristicUUID[] = "22345678-1234-1234-1234-1234567890bc";

    struct Target {
        const char* serviceUUID;
        const char* characteristicUUID;
        uint32_t pin;
        bool requestEncryption;
    };

    const Target targets[] = {
        {"12345678-1234-1234-1234-1234567890bb", "12345678-1234-1234-1234-1234567890bc", 246810, true},
        {"", "", 0, false},
        {"", "", 0, false}
    };

    constexpr size_t targetCount = sizeof(targets) / sizeof(targets[0]);

    Bluetooth::server localServer;
    std::unique_ptr<Bluetooth::client> clients[targetCount];
    std::atomic<BLECharacteristic*> pendingLocalNotification{nullptr};

    size_t selectedTarget = 0;
    bool ready            = false;

    struct DiscoveredServer {
        std::string name;
        std::string address;
    };

    std::vector<DiscoveredServer> discoveredServers;
    std::string serverNumber;
    std::string connectedAddresses[targetCount];

    bool choosingServer      = false;
    bool invalidServerNumber = false;

    class LocalCallbacks final : public BLECharacteristicCallbacks {
        void onWrite(BLECharacteristic* characteristic, esp_ble_gatts_cb_param_t*) override {
            pendingLocalNotification.store(characteristic);
        }
    };

    class ConnectionCallbacks final : public BLEClientCallbacks {
        size_t _INDEX;
        public:
            explicit ConnectionCallbacks(size_t index) : _INDEX(index) {}
        private:
            void onConnect(BLEClient*) override {
                Serial.printf("CLIENT %u LINK CONNECTED\n",    static_cast<unsigned int>(_INDEX + 1));
            }
            void onDisconnect(BLEClient*) override {
                Serial.printf("CLIENT %u LINK DISCONNECTED\n", static_cast<unsigned int>(_INDEX + 1));
            }
    };

    void printHelp() {
        Serial.println("1 / 2 / 3: select an outgoing client");
        Serial.println("c: scan, then choose a server for the selected client");
        Serial.println("r: read the selected remote characteristic");
        Serial.println("w: write DUAL ROLE EXAMPLE to the selected characteristic");
        Serial.println("d: request disconnection of the selected client");
        Serial.println("i: show all outgoing connection states");
        Serial.println("h: show commands");
        Serial.println("Unexpected link loss: automatic retry after 5 seconds; 'd' cancels retries.");
    }

    void scanServers() {
        discoveredServers.clear();
        Bluetooth::scan scanner(5);
        BLEScanResults results = scanner.start(true);

        for (int index = 0; index < results.getCount(); ++index) {
            BLEAdvertisedDevice device = results.getDevice(index);
            discoveredServers.push_back({device.getName(), device.getAddress().toString()});
        }
        scanner.clear();

        if (discoveredServers.empty()) {
            Serial.println("NO DEVICES FOUND");
            return;
        }
        for (size_t index = 0; index < discoveredServers.size(); ++index) {
            const DiscoveredServer& server = discoveredServers[index];
            Serial.printf(
                "%u: %s [%s]\n", static_cast<unsigned int>(index + 1),
                server.name.empty() ? "(unnamed)" : server.name.c_str(), server.address.c_str());
        }
        serverNumber.clear();
        invalidServerNumber = false;
        choosingServer      = true;
        Serial.printf(
            "CLIENT %u: enter a device number and press Enter; x cancels.\n",
            static_cast<unsigned int>(selectedTarget + 1)
        );
    }
}

void setup() {
    Serial.begin(115200);
    const uint32_t startedAt = millis();
    while (!Serial && static_cast<uint32_t>(millis() - startedAt) < 3000U) {
        delay(10);
    }

    if (!localServer.setPIN(localPIN)) {
        Serial.println("ERROR: local PIN configuration rejected");
        return;
    }
    localServer.setCallbacksCharacteristic(new LocalCallbacks());
    localServer.init(localServiceUUID, localName, 5);
    if (!localServer.addCharacteristic(
            localCharacteristicUUID, "LOCAL SERVER READY",
            {{"2901", "Local read/write and notify"}},
            true, true, true, false, false, false)) {
        Serial.println("ERROR: local characteristic creation failed");
        return;
    }
    localServer.start();

    for (size_t index = 0; index < targetCount; ++index) {
        const Target& target = targets[index];
        clients[index].reset(new Bluetooth::client(target.pin, target.requestEncryption));
        clients[index]->setCallbacksClient(new ConnectionCallbacks(index));
        if (target.serviceUUID[0] == '\0' || target.characteristicUUID[0] == '\0') {
            clients[index]->setCallbacksNotify(nullptr);
            continue;
        }
        clients[index]->setCallbacksNotify(new notify_callback(
            [index](BLERemoteCharacteristic*, uint8_t* data, size_t length, bool isNotify) {
                Serial.printf("CLIENT %u %s: %u byte(s): ",
                    static_cast<unsigned int>(index + 1),
                    isNotify ? "NOTIFICATION" : "INDICATION",
                    static_cast<unsigned int>(length));
                Serial.write(data, length);
                Serial.println();
            }
        ));
    }

    ready = true;
    Serial.printf(
        "LOCAL SERVER START REQUESTED: %s, PIN %06lu\n",
        localName, static_cast<unsigned long>(localPIN)
    );
    Serial.println("SELECTED CLIENT: 1");
    printHelp();
}

void loop() {
    if (!ready) { delay(10); return; }

    localServer.update();
    BLECharacteristic* local = pendingLocalNotification.exchange(nullptr);

    if (local != nullptr) {
        const std::string value = local->getValue();
        Serial.print("LOCAL WRITE: ");
        Serial.write(reinterpret_cast<const uint8_t*>(value.data()), value.size());
        Serial.println();
        local->notify();
    }

    if (!Serial.available()) {
        if (!choosingServer) {
            for (size_t index = 0; index < targetCount; ++index) {
                if (Serial.available())        { break; }
                if (clients[index] != nullptr) { clients[index]->update(); }
            }
        }
        delay(10);
        return;
    }
    const int command = Serial.read();
    if (choosingServer) {
        if (command == 'x') {
            choosingServer = false;
            discoveredServers.clear();
            Serial.println("SERVER SELECTION CANCELLED");
            return;
        }
        if (command != '\r' && command != '\n') {
            if (command >= '0' && command <= '9' && serverNumber.size() < 10)
                { serverNumber.push_back(static_cast<char>(command)); } 
            else { invalidServerNumber = true; }
            return;
        }

        if (serverNumber.empty() && !invalidServerNumber) { return; }
        uint64_t number = 0;

        for (char digit : serverNumber)
            { number = number * 10 + (digit - '0'); }
        serverNumber.clear();

        if (invalidServerNumber || number == 0 || number > discoveredServers.size()) {
            invalidServerNumber = false;
            Serial.println("INVALID DEVICE NUMBER: enter a listed number, or x to cancel.");
            return;
        }
    
        const std::string address = discoveredServers[static_cast<size_t>(number - 1)].address;
        choosingServer            = false;
        discoveredServers.clear();

        for (size_t index = 0; index < targetCount; ++index) {
            if (clients[index] != nullptr && clients[index]->isConnected() &&
                connectedAddresses[index] == address) {
                Serial.println("DEVICE ALREADY CONNECTED TO AN OUTGOING CLIENT");
                return;
            }
        }

        const Target& target      = targets[selectedTarget];
        Bluetooth::client* client = clients[selectedTarget].get();

        Serial.printf("CONNECT START: %s\n", address.c_str());
        const bool connected      = client->connect(address, 23, 5, 10, target.serviceUUID);
    
        if (client->isConnected()) { connectedAddresses[selectedTarget] = address; }
        Serial.println(connected ? "CONNECT SUCCEEDED" : "CONNECT FAILED");
        return;
    }
    if (command == '\r' || command == '\n') { return; }
    if (command >= '1' && command <= '3') {
        selectedTarget = static_cast<size_t>(command - '1');
        Serial.printf("SELECTED CLIENT: %u\n", static_cast<unsigned int>(selectedTarget + 1));
        return;
    }
    if (command == 'i') {
        for (size_t index = 0; index < targetCount; ++index) {
            Serial.printf("CLIENT %u: %s [%s]\n", static_cast<unsigned int>(index + 1),
                clients[index] == nullptr     ? "DISABLED"  :
                clients[index]->isConnected() ? "CONNECTED" : "DISCONNECTED",
                connectedAddresses[index].c_str());
        }
        return;
    }
    if (command != 'c' && command != 'r' && command != 'w' && command != 'd') {
        printHelp();
        return;
    }

    Bluetooth::client* client = clients[selectedTarget].get();
    if (client == nullptr) { Serial.println("CLIENT UNAVAILABLE"); return; }
    const Target& target      = targets[selectedTarget];

    if (command == 'd') {
        client->disconnect();
        Serial.println("DISCONNECT REQUESTED: wait for 'i' to report DISCONNECTED");
    } else if (command == 'c') {
        if (client->isConnected()) { Serial.println("ALREADY CONNECTED"); return; }
        Serial.println("SCAN START");
        scanServers();
    } else if (!client->isConnected()) {
        Serial.println("Connect the selected client with 'c' first.");
    } else if (target.serviceUUID[0] == '\0' || target.characteristicUUID[0] == '\0') {
        Serial.println("READ/WRITE DISABLED: configure the client's service and characteristic UUIDs in targets[].");
    } else if (command == 'r') {
        const std::string value = client->readCharacteristic(target.characteristicUUID);
        Serial.printf("READ: %u byte(s): ", static_cast<unsigned int>(value.size()));
        Serial.write(reinterpret_cast<const uint8_t*>(value.data()), value.size());
        Serial.println();
    } else {
        client->sendCommand(target.characteristicUUID, "DUAL ROLE EXAMPLE");
        Serial.println("WRITE REQUESTED");
    }
}
