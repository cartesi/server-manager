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

#ifndef I_HASHER_H
#define I_HASHER_H

/// \file
/// \brief Hasher interface

#include <array>
#include <cstddef>

#include "meta.h"

namespace cartesi {

/// \brief Hasher interface.
/// \tparam DERIVED Derived class implementing the interface. (An example of CRTP.)
/// \tparam HASH_SIZE Size of hash.
template <typename DERIVED, typename HASH_SIZE>
class i_hasher { // CRTP

    /// \brief Returns object cast as the derived class
    DERIVED &derived(void) {
        return *static_cast<DERIVED *>(this);
    }

    /// \brief Returns object cast as the derived class
    const DERIVED &derived(void) const {
        return *static_cast<const DERIVED *>(this);
    }

public:
    constexpr static size_t hash_size = HASH_SIZE::value;

    using hash_type = std::array<unsigned char, hash_size>;

    void begin(void) {
        return derived().do_begin();
    }

    void add_data(const unsigned char *data, size_t length) {
        return derived().do_add_data(data, length);
    }

    void end(hash_type &hash) {
        return derived().do_end(hash);
    }
};

template <typename DERIVED>
using is_an_i_hasher =
    std::integral_constant<bool, is_template_base_of<i_hasher, typename remove_cvref<DERIVED>::type>::value>;

/// \brief Computes the hash of concatenated hashes
/// \tparam H Hasher class
/// \param h Hasher object
/// \param left Left hash to concatenate
/// \param right Right hash to concatenate
/// \param result Receives the hash of the concatenation
template <typename H>
inline static void get_concat_hash(H &h, const typename H::hash_type &left, const typename H::hash_type &right,
    typename H::hash_type &result) {
    static_assert(is_an_i_hasher<H>::value, "not an i_hasher");
    h.begin();
    h.add_data(left.data(), static_cast<int>(left.size()));
    h.add_data(right.data(), static_cast<int>(right.size()));
    h.end(result);
}

/// \brief Computes the hash of concatenated hashes
/// \tparam H Hasher class
/// \param h Hasher object
/// \param left Left hash to concatenate
/// \param right Right hash to concatenate
/// \return The hash of the concatenation
template <typename H>
inline static typename H::hash_type get_concat_hash(H &h, const typename H::hash_type &left,
    const typename H::hash_type &right) {
    static_assert(is_an_i_hasher<H>::value, "not an i_hasher");
    h.begin();
    h.add_data(left.data(), left.size());
    h.add_data(right.data(), right.size());
    typename H::hash_type result;
    h.end(result);
    return result;
}

} // namespace cartesi

#endif
