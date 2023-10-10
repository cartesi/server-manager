// Copyright Cartesi and individual authors (see AUTHORS)
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#ifndef HTIF_DEFINES_H
#define HTIF_DEFINES_H

#include <cstdint>

/// \brief HTIF protocol shifts
enum HTIF_protocol_shift : uint64_t { HTIF_DEV_SHIFT = 56, HTIF_CMD_SHIFT = 48, HTIF_DATA_SHIFT = 0 };

/// \brief HTIF protocol masks
enum HTIF_protocol_mask : uint64_t {
    HTIF_DEV_MASK = 0xFF00000000000000,
    HTIF_CMD_MASK = 0x00FF000000000000,
    HTIF_DATA_MASK = 0x0000FFFFFFFFFFFF
};

/// \brief HTIF device identifiers
enum HTIF_device : uint64_t { HTIF_DEVICE_HALT = 0, HTIF_DEVICE_CONSOLE = 1, HTIF_DEVICE_YIELD = 2 };

/// \brief HTIF device commands
enum HTIF_device_command : uint64_t {
    HTIF_HALT_HALT = 0,
    HTIF_CONSOLE_GETCHAR = 0,
    HTIF_CONSOLE_PUTCHAR = 1,
    HTIF_YIELD_AUTOMATIC = 0,
    HTIF_YIELD_MANUAL = 1
};

/// \brief HTIF yield reasons
enum HTIF_yield_reason : uint64_t {
    HTIF_YIELD_REASON_PROGRESS = 0,
    HTIF_YIELD_REASON_RX_ACCEPTED = 1,
    HTIF_YIELD_REASON_RX_REJECTED = 2,
    HTIF_YIELD_REASON_TX_VOUCHER = 3,
    HTIF_YIELD_REASON_TX_NOTICE = 4,
    HTIF_YIELD_REASON_TX_REPORT = 5,
    HTIF_YIELD_REASON_TX_EXCEPTION = 6,
};

#endif /* end of include guard: HTIF_DEFINES_H */
