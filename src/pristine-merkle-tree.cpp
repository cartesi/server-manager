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

#include "pristine-merkle-tree.h"
#include <cassert>
#include <stdexcept>

/// \file
/// \brief Pristine Merkle tree implementation.

namespace cartesi {

pristine_merkle_tree::pristine_merkle_tree(int log2_root_size, int log2_word_size) :
    m_log2_root_size{log2_root_size},
    m_log2_word_size{log2_word_size},
    m_hashes(std::max(0, log2_root_size - log2_word_size + 1)) {
    if (log2_root_size < 0) {
        throw std::out_of_range{"log2_root_size is negative"};
    }
    if (log2_word_size < 0) {
        throw std::out_of_range{"log2_word_size is negative"};
    }
    if (log2_word_size > log2_root_size) {
        throw std::out_of_range{"log2_word_size is greater than log2_root_size"};
    }
    std::vector<uint8_t> word(1 << log2_word_size, 0);
    assert(word.size() == (UINT64_C(1) << log2_word_size));
    hasher_type h;
    h.begin();
    h.add_data(word.data(), word.size());
    h.end(m_hashes[0]);
    for (unsigned i = 1; i < m_hashes.size(); ++i) {
        get_concat_hash(h, m_hashes[i - 1], m_hashes[i - 1], m_hashes[i]);
    }
}

const pristine_merkle_tree::hash_type &pristine_merkle_tree::get_hash(int log2_size) const {
    if (log2_size < m_log2_word_size || log2_size > m_log2_root_size) {
        throw std::out_of_range{"log2_size is out of range"};
    }
    return m_hashes[log2_size - m_log2_word_size];
}

} // namespace cartesi
