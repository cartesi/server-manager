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

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#pragma GCC diagnostic ignored "-Wdeprecated-copy"
#pragma GCC diagnostic ignored "-Wtype-limits"
#include "cartesi-machine.pb.h"
#pragma GCC diagnostic pop

#include "machine-merkle-tree.h"

namespace cartesi {
/// \brief Converts proto Hash to C++ hash
/// \param proto_hash Proto Hash to convert
/// \returns Converted C++ hash
machine_merkle_tree::hash_type get_proto_hash(const CartesiMachine::Hash &proto_hash);

/// \brief Converts C++ hash to proto Hash
/// \param h C++ hash to convert
/// \param proto_h Pointer to proto Hash receiving result of conversion
void set_proto_hash(const machine_merkle_tree::hash_type &h, CartesiMachine::Hash *proto_h);

/// \brief Converts C++ proof to proto Proof
/// \param p C++ proof to convert
/// \param proto_p Pointer to proto Proof receiving result of conversion
void set_proto_merkle_tree_proof(const machine_merkle_tree::proof_type &p, CartesiMachine::MerkleTreeProof *proto_p);

/// \brief Converts proto Proof to C++ proof
/// \param proto_proof Proto proof to convert
/// \returns Converted C++ proof
machine_merkle_tree::proof_type get_proto_merkle_tree_proof(const CartesiMachine::MerkleTreeProof &proto_proof);

} // namespace cartesi
