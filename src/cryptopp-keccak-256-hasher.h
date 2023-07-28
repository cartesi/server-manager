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

#ifndef CRYPTOPP_KECCAK_256_HASHER_H
#define CRYPTOPP_KECCAK_256_HASHER_H

#include "i-hasher.h"
#include <cryptopp/keccak.h>
#include <type_traits>

namespace cartesi {

class cryptopp_keccak_256_hasher final :
    public i_hasher<cryptopp_keccak_256_hasher, std::integral_constant<int, CryptoPP::Keccak_256::DIGESTSIZE>> {

    CryptoPP::Keccak_256 kc{};

    friend i_hasher<cryptopp_keccak_256_hasher, std::integral_constant<int, CryptoPP::Keccak_256::DIGESTSIZE>>;

    void do_begin(void) {
        return kc.Restart();
    }

    void do_add_data(const unsigned char *data, size_t length) {
        return kc.Update(data, length);
    }

    void do_end(hash_type &hash) {
        return kc.Final(hash.data());
    }

public:
    /// \brief Default constructor
    cryptopp_keccak_256_hasher(void) = default;

    /// \brief Default destructor
    ~cryptopp_keccak_256_hasher(void) = default;

    /// \brief No copy constructor
    cryptopp_keccak_256_hasher(const cryptopp_keccak_256_hasher &) = delete;
    /// \brief No move constructor
    cryptopp_keccak_256_hasher(cryptopp_keccak_256_hasher &&) = delete;
    /// \brief No copy assignment
    cryptopp_keccak_256_hasher &operator=(const cryptopp_keccak_256_hasher &) = delete;
    /// \brief No move assignment
    cryptopp_keccak_256_hasher &operator=(cryptopp_keccak_256_hasher &&) = delete;
};

} // namespace cartesi

#endif
