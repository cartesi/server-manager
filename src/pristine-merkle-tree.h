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

#ifndef PRISTINE_MERKLE_TREE_H
#define PRISTINE_MERKLE_TREE_H

#include <cassert>
#include <cstdint>
#include <vector>

#include "keccak-256-hasher.h"
#include "meta.h"

/// \file
/// \brief Pristine Merkle tree interface.

namespace cartesi {

/// \brief Hashes of pristine subtrees for a range of sizes
class pristine_merkle_tree {
public:
    /// \brief Hasher class.
    using hasher_type = keccak_256_hasher;

    /// \brief Storage for a hash.
    using hash_type = hasher_type::hash_type;

    using address_type = uint64_t;

    /// \brief Constructor
    /// \param log2_root_size Log<sub>2</sub> of root node
    /// \param log2_word_size Log<sub>2</sub> of word
    pristine_merkle_tree(int log2_root_size, int log2_word_size);

    /// \brief Returns hash of pristine subtree
    /// \param log2_size Log<sub>2</sub> of subtree size. Must be between
    /// log2_word_size (inclusive) and log2_root_size (inclusive) passed
    /// to constructor.
    const hash_type &get_hash(int log2_size) const;

private:
    int m_log2_root_size;            ///< Log<sub>2</sub> of tree size
    int m_log2_word_size;            ///< Log<sub>2</sub> of word size
    std::vector<hash_type> m_hashes; ///< Vector with hashes
};

} // namespace cartesi

#endif
