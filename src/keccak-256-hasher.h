// Copyright 2019-2023 Cartesi Pte. Ltd.
//
// SPDX-License-Identifier: Apache-2.0
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use
// this file except in compliance with the License. You may obtain a copy of the
// License at http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed
// under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
// CONDITIONS OF ANY KIND, either express or implied. See the License for the
// specific language governing permissions and limitations under the License.

#ifndef KECCAK_256_HASHER_H
#define KECCAK_256_HASHER_H

#include "cryptopp-keccak-256-hasher.h"

namespace cartesi {

/// \brief Class used to compute Keccak 256 hashes
using keccak_256_hasher = cryptopp_keccak_256_hasher;

} // namespace cartesi

#endif
