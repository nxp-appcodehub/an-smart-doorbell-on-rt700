/* Copyright 2024-2026 NXP
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * driver_config.h - UWB middleware driver configuration for smart doorbell
 *
 * Hardware: RT700 EVK B + ETNA-TS-250-DevKit UWB module
 *           (Arduino-compatible shield header, same pinout as SR250-ARD)
 *
 * All SPI/GPIO peripheral macros have been moved to the board-specific
 * uwb_bus_board_config.h located at:
 *   freertos_port/board/rt700_evk/uwb_bus_board_config.h
 *
 * This file retains only the phNxpUwbConfig.h inclusion which is required
 * by the UWB middleware configuration layer.
 */

#ifndef DRIVER_CONFIG_H
#define DRIVER_CONFIG_H

#include "phNxpUwbConfig.h"

#endif /* DRIVER_CONFIG_H */
