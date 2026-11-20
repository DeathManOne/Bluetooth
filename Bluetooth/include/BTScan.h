/*
 * include/BTScan.h
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

#pragma once

#include <BLEDevice.h>

namespace Bluetooth {
    class scan {
        private:
            static scan *_OWNER;
            int _SCAN_TIME;
            BLEScan *_SCAN;
        public:
            /**
             * @brief Disable copy construction to preserve scanner control identity.
             */
            scan(const scan&) = delete;

            /**
             * @brief Disable copy assignment to preserve scanner control identity.
             */
            scan& operator=(const scan&) = delete;

            /**
             * @brief Configure a BLE scanner.
             * @param scanTime Duration in seconds. A negative value prevents
             * scanning; zero requests a scan with no time limit.
             */
            explicit scan(int scanTime);

            /**
             * @brief Request scanning to stop if this object controls the scanner.
             * @pre No method call on this object may be executing.
             * @note The global BLEDevice scanner and stored results are not destroyed.
             */
            ~scan();

            /**
             * @brief Start an active scan and wait for it to finish.
             * @param includeUnnamed Include devices with an empty name when true
             * (default). When false, remove them after scanning completes.
             * @return Discovered devices matching the name filter, or empty
             * results if the configured duration is negative.
             * @pre No other scan may be running on the global scanner.
             * Do not call from a BLE callback.
             * @note Devices referenced by the results are shared with the
             * global scanner. A clear(), a new scan, or BLE deinitialization
             * may invalidate them.
             * @note Multiple objects may be used sequentially; the object
             * that most recently started a scan controls the scanner.
             */
            BLEScanResults start(bool includeUnnamed = true);

            /**
             * @brief Request that scanning stop without clearing the results.
             * @note Has no effect if this object does not control the global scanner.
             * Returning does not guarantee that an executing callback has finished.
             */
            void stop() const;

            /**
             * @brief Release the devices stored by the scanner.
             * @pre Scanning must have finished and no previously returned
             * results may still be in use.
             * @note Invalidates devices referenced by copies of BLEScanResults.
             * Has no effect if this object does not control the global scanner.
             */
            void clear() const;
    };
}
