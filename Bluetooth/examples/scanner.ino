/*
 * examples/scanner.ino
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
#include <BTScan.h>

void setup() {
    Serial.begin(115200);
    while (!Serial)
        { delay(10); }
    Serial.println("Send 's' to start a 5-second BLE scan.");
}

void loop() {
    if (!Serial.available())  { return; }
    if (Serial.read() != 's') { return; }

    Bluetooth::scan scanner(5);

    Serial.println("SCAN START");
    const uint32_t startedAt = millis();
    BLEScanResults results   = scanner.start();
    const uint32_t elapsed   = millis() - startedAt;

    Serial.printf("SCAN END: %lu ms, %d device(s)\n", static_cast<unsigned long>(elapsed), results.getCount());

    for (int i = 0; i < results.getCount(); ++i)
        { Serial.println(results.getDevice(i).toString().c_str()); }
    scanner.clear();
    Serial.println("RESULTS CLEARED");
}
