// Copyright 2020-2023 Cartesi Pte. Ltd.
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

#include "protobuf-util.h"

namespace cartesi {

void set_proto_hash(const machine_merkle_tree::hash_type &h, CartesiMachine::Hash *proto_h) {
    proto_h->set_data(h.data(), h.size());
}

machine_merkle_tree::hash_type get_proto_hash(const CartesiMachine::Hash &proto_hash) {
    machine_merkle_tree::hash_type hash;
    if (proto_hash.data().size() != hash.size()) {
        throw std::invalid_argument("invalid hash size");
    }
    memcpy(hash.data(), proto_hash.data().data(), proto_hash.data().size());
    return hash;
}

machine_merkle_tree::proof_type get_proto_merkle_tree_proof(const CartesiMachine::MerkleTreeProof &proto_proof) {
    const int log2_target_size = static_cast<int>(proto_proof.log2_target_size());
    const int log2_root_size = static_cast<int>(proto_proof.log2_root_size());
    machine_merkle_tree::proof_type p{log2_root_size, log2_target_size};
    p.set_target_address(proto_proof.target_address());
    p.set_target_hash(get_proto_hash(proto_proof.target_hash()));
    p.set_root_hash(get_proto_hash(proto_proof.root_hash()));
    const auto &proto_sibs = proto_proof.sibling_hashes();
    if (log2_root_size - proto_sibs.size() != log2_target_size) {
        throw std::invalid_argument("wrong number of sibling hashes");
    }
    for (int i = 0; i < proto_sibs.size(); i++) {
        p.set_sibling_hash(get_proto_hash(proto_sibs[i]), log2_root_size - 1 - i);
    }
    return p;
}

void set_proto_merkle_tree_proof(const machine_merkle_tree::proof_type &p, CartesiMachine::MerkleTreeProof *proto_p) {
    proto_p->set_target_address(p.get_target_address());
    proto_p->set_log2_target_size(p.get_log2_target_size());
    proto_p->set_log2_root_size(p.get_log2_root_size());
    set_proto_hash(p.get_target_hash(), proto_p->mutable_target_hash());
    set_proto_hash(p.get_root_hash(), proto_p->mutable_root_hash());
    for (int log2_size = p.get_log2_root_size() - 1; log2_size >= p.get_log2_target_size(); --log2_size) {
        set_proto_hash(p.get_sibling_hash(log2_size), proto_p->add_sibling_hashes());
    }
}

} // namespace cartesi
