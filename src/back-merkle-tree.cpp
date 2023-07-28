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

#include "back-merkle-tree.h"
#include <limits>

/// \file
/// \brief Back Merkle tree implementation.

namespace cartesi {

back_merkle_tree::back_merkle_tree(int log2_root_size, int log2_leaf_size, int log2_word_size) :
    m_log2_root_size{log2_root_size},
    m_log2_leaf_size{log2_leaf_size},
    m_leaf_count{0},
    m_max_leaves{address_type{1} << (log2_root_size - log2_leaf_size)},
    m_context(std::max(1, log2_root_size - log2_leaf_size + 1)),
    m_pristine_hashes{log2_root_size, log2_word_size} {
    if (log2_root_size < 0) {
        throw std::out_of_range{"log2_root_size is negative"};
    }
    if (log2_leaf_size < 0) {
        throw std::out_of_range{"log2_leaf_size is negative"};
    }
    if (log2_word_size < 0) {
        throw std::out_of_range{"log2_word_size is negative"};
    }
    if (log2_leaf_size > log2_root_size) {
        throw std::out_of_range{"log2_leaf_size is greater than log2_root_size"};
    }
    if (log2_word_size > log2_leaf_size) {
        throw std::out_of_range{"log2_word_size is greater than log2_word_size"};
    }
    if (log2_root_size >= std::numeric_limits<address_type>::digits) {
        throw std::out_of_range{"tree is too large for address type"};
    }
}

void back_merkle_tree::push_back(const hash_type &leaf_hash) {
    hasher_type h;
    hash_type right = leaf_hash;
    if (m_leaf_count >= m_max_leaves) {
        throw std::out_of_range{"too many leaves"};
    }
    const int depth = m_log2_root_size - m_log2_leaf_size;
    for (int i = 0; i <= depth; ++i) {
        if (m_leaf_count & (address_type{1} << i)) {
            const auto &left = m_context[i];
            get_concat_hash(h, left, right, right);
        } else {
            m_context[i] = right;
            break;
        }
    }
    ++m_leaf_count;
}

back_merkle_tree::hash_type back_merkle_tree::get_root_hash(void) const {
    hasher_type h;
    assert(m_leaf_count <= m_max_leaves);
    const int depth = m_log2_root_size - m_log2_leaf_size;
    if (m_leaf_count < m_max_leaves) {
        auto root = m_pristine_hashes.get_hash(m_log2_leaf_size);
        for (int i = 0; i < depth; ++i) {
            if (m_leaf_count & (address_type{1} << i)) {
                const auto &left = m_context[i];
                get_concat_hash(h, left, root, root);
            } else {
                const auto &right = m_pristine_hashes.get_hash(m_log2_leaf_size + i);
                get_concat_hash(h, root, right, root);
            }
        }
        return root;
    } else {
        return m_context[depth];
    }
}

back_merkle_tree::proof_type back_merkle_tree::get_next_leaf_proof(void) const {
    const int depth = m_log2_root_size - m_log2_leaf_size;
    if (m_leaf_count >= m_max_leaves) {
        throw std::out_of_range{"tree is full"};
    }
    hasher_type h;
    proof_type proof{m_log2_root_size, m_log2_leaf_size};
    proof.set_target_address(m_leaf_count << m_log2_leaf_size);
    proof.set_target_hash(m_pristine_hashes.get_hash(m_log2_leaf_size));
    hash_type hash = m_pristine_hashes.get_hash(m_log2_leaf_size);
    for (int i = 0; i < depth; ++i) {
        if (m_leaf_count & (address_type{1} << i)) {
            const auto &left = m_context[i];
            proof.set_sibling_hash(left, m_log2_leaf_size + i);
            get_concat_hash(h, left, hash, hash);
        } else {
            const auto &right = m_pristine_hashes.get_hash(m_log2_leaf_size + i);
            proof.set_sibling_hash(right, m_log2_leaf_size + i);
            get_concat_hash(h, hash, right, hash);
        }
    }
    proof.set_root_hash(hash);
#ifndef NDEBUG
    if (!proof.verify(h)) {
        throw std::runtime_error{"produced invalid proof"};
    }
#endif
    return proof;
}

} // namespace cartesi
