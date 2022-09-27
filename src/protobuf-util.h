// Copyright 2020 Cartesi Pte. Ltd.
//
// This file is part of the machine-emulator. The machine-emulator is free
// software: you can redistribute it and/or modify it under the terms of the GNU
// Lesser General Public License as published by the Free Software Foundation,
// either version 3 of the License, or (at your option) any later version.
//
// The machine-emulator is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
// for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with the machine-emulator. If not, see http://www.gnu.org/licenses/.
//

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#pragma GCC diagnostic ignored "-Wdeprecated-copy"
#pragma GCC diagnostic ignored "-Wtype-limits"
#include "cartesi-machine.pb.h"
#include "versioning.pb.h"
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
