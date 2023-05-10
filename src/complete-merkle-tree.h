// Copyright 2021-2023 Cartesi Pte. Ltd.
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

#ifndef COMPLETE_MERKLE_TREE_H
#define COMPLETE_MERKLE_TREE_H

#include "keccak-256-hasher.h"
#include "merkle-tree-proof.h"
#include "meta.h"
#include "pristine-merkle-tree.h"

/// \file
/// \brief Complete Merkle tree interface.

namespace cartesi {

/// \brief Complete Merkle tree
/// \details This class implements complete Merkle tree, i.e., a tree in which
/// the leaf level has a number of non-pristine leaves followed by a number
/// of pristine leaves.
/// The tree is optimized to store only the hashes that are not pristine.
class complete_merkle_tree {
public:
    /// \brief Hasher class.
    using hasher_type = keccak_256_hasher;

    /// \brief Storage for a hash.
    using hash_type = hasher_type::hash_type;

    /// \brief Storage for an address.
    using address_type = uint64_t;

    /// \brief Storage for a proof.
    using proof_type = merkle_tree_proof<hash_type, address_type>;

    /// \brief Storage for a level in the tree.
    using level_type = std::vector<hash_type>;

    /// \brief Constructor for pristine tree
    /// \param log2_root_size Log<sub>2</sub> of tree size
    /// \param log2_leaf_size Log<sub>2</sub> of leaf node
    /// \param log2_word_size Log<sub>2</sub> of word
    complete_merkle_tree(int log2_root_size, int log2_leaf_size, int log2_word_size);

    /// \brief Constructor from non-pristine leaves (assumed flushed left)
    /// \param log2_root_size Log<sub>2</sub> of tree size
    /// \param log2_leaf_size Log<sub>2</sub> of leaf node
    /// \param log2_word_size Log<sub>2</sub> of word
    template <typename L>
    complete_merkle_tree(int log2_root_size, int log2_leaf_size, int log2_word_size, L &&leaves) :
        complete_merkle_tree{log2_root_size, log2_leaf_size, log2_word_size} {
        static_assert(std::is_same<level_type, typename remove_cvref<L>::type>::value, "not a leaves vector");
        get_level(get_log2_leaf_size()) = std::forward<L>(leaves);
        bubble_up();
    }

    /// \brief Returns the tree's root hash
    /// \returns Root hash
    hash_type get_root_hash(void) const {
        return get_node_hash(0, get_log2_root_size());
    }

    /// \brief Returns the hash of a node at a given address of a given size
    /// \param address Node address
    /// \param log2_size Log<sub>2</sub> size subintended by node
    const hash_type &get_node_hash(address_type address, int log2_size) const;

    /// \brief Returns proof for a given node
    /// \param address Node address
    /// \param log2_size Log<sub>2</sub> size subintended by node
    /// \returns Proof, or throws exception
    proof_type get_proof(address_type address, int log2_size) const;

    /// \brief Appends a new leaf hash to the tree
    /// \param hash Hash to append
    void push_back(const hash_type &hash);

    /// \brief Returns number of leaves in tree
    address_type size(void) const {
        return get_level(get_log2_leaf_size()).size();
    };

private:
    /// \brief Throws exception if log<sub>2</sub> sizes are inconsistent
    ///  with one another
    /// \param log2_root_size Log<sub>2</sub> of root node
    /// \param log2_leaf_size Log<sub>2</sub> of leaf node
    /// \param log2_word_size Log<sub>2</sub> of word
    static void check_log2_sizes(int log2_root_size, int log2_leaf_size, int log2_word_size);

    /// \brief Returns log<sub>2</sub> of size of tree
    int get_log2_root_size(void) const {
        return m_log2_root_size;
    }

    /// \brief Returns log<sub>2</sub> of size of leaf
    int get_log2_leaf_size(void) const {
        return m_log2_leaf_size;
    }

    /// \brief Update node hashes when a new set of non-pristine nodes is added
    /// to the leaf level
    void bubble_up(void);

    ///< \brief Returns hashes at a given level
    ///< \param log2_size Log<sub>2</sub> of size subintended by each
    /// hash at level
    const level_type &get_level(int log2_size) const;

    ///< \brief Returns hashes at a given level
    ///< \param log2_size Log<sub>2</sub> of size subintended by each
    /// hash at level
    level_type &get_level(int log2_size);

    int m_log2_root_size;            ///< Log<sub>2</sub> of tree size
    int m_log2_leaf_size;            ///< Log<sub>2</sub> of page size
    pristine_merkle_tree m_pristine; ///< Pristine hashes for all levels
    std::vector<level_type> m_tree;  ///< Merkle tree
};

} // namespace cartesi

#endif
