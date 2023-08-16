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

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

#include <cryptopp/filters.h>
#include <cryptopp/hex.h>
#include <unordered_map>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#pragma GCC diagnostic ignored "-Wdeprecated-copy"
#pragma GCC diagnostic ignored "-Wtype-limits"
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-builtins"
#endif
#include <boost/endian/conversion.hpp>
#include <google/protobuf/util/json_util.h>
#include <grpc++/grpc++.h>

#include "cartesi-machine-checkin.grpc.pb.h"
#include "health.grpc.pb.h"
#include "protobuf-util.h"
#include "server-manager.grpc.pb.h"
#pragma GCC diagnostic pop
#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include "back-merkle-tree.h"
#include "complete-merkle-tree.h"

using CartesiMachine::Void;
using grpc::ClientContext;
using grpc::Status;
using grpc::StatusCode;

// NOLINTNEXTLINE(misc-unused-using-decls)
using std::chrono_literals::operator""s;

using namespace std::filesystem;
using namespace CartesiServerManager;
using namespace cartesi;
using namespace grpc::health::v1;

constexpr static const int LOG2_ROOT_SIZE = 37;
constexpr static const int LOG2_KECCAK_SIZE = 5;
constexpr static const int LOG2_WORD_SIZE = 3;
constexpr static const uint64_t MEMORY_REGION_LENGTH = 2 << 20;
constexpr static const int WAITING_PENDING_INPUT_MAX_RETRIES = 20;
static const path MANAGER_ROOT_DIR = "/tmp/server-manager-root"; // NOLINT: ignore static initialization warning

class ServerManagerClient {

public:
    ServerManagerClient(const std::string &address) : m_test_id("not-defined") {
        m_stub = ServerManager::NewStub(grpc::CreateChannel(address, grpc::InsecureChannelCredentials()));
        m_health_stub = Health::NewStub(grpc::CreateChannel(address, grpc::InsecureChannelCredentials()));
    }

    Status get_version(Versioning::GetVersionResponse &response) {
        ClientContext context;
        Void request;
        init_client_context(context);
        return m_stub->GetVersion(&context, request, &response);
    }

    Status start_session(const StartSessionRequest &request, StartSessionResponse &response) {
        ClientContext context;
        init_client_context(context);
        return m_stub->StartSession(&context, request, &response);
    }

    Status advance_state(const AdvanceStateRequest &request) {
        ClientContext context;
        Void response;
        init_client_context(context);
        return m_stub->AdvanceState(&context, request, &response);
    }

    Status get_status(GetStatusResponse &response) {
        ClientContext context;
        Void request;
        init_client_context(context);
        return m_stub->GetStatus(&context, request, &response);
    }

    Status get_session_status(const GetSessionStatusRequest &request, GetSessionStatusResponse &response) {
        ClientContext context;
        init_client_context(context);
        return m_stub->GetSessionStatus(&context, request, &response);
    }

    Status get_epoch_status(const GetEpochStatusRequest &request, GetEpochStatusResponse &response) {
        ClientContext context;
        init_client_context(context);
        return m_stub->GetEpochStatus(&context, request, &response);
    }

    Status inspect_state(const InspectStateRequest &request, InspectStateResponse &response) {
        ClientContext context;
        init_client_context(context);
        return m_stub->InspectState(&context, request, &response);
    }

    Status finish_epoch(const FinishEpochRequest &request, FinishEpochResponse &response) {
        ClientContext context;
        init_client_context(context);
        return m_stub->FinishEpoch(&context, request, &response);
    }

    Status delete_epoch(const DeleteEpochRequest &request) {
        ClientContext context;
        Void response;
        init_client_context(context);
        return m_stub->DeleteEpoch(&context, request, &response);
    }

    Status end_session(const EndSessionRequest &request) {
        ClientContext context;
        Void response;
        init_client_context(context);
        return m_stub->EndSession(&context, request, &response);
    }

    Status health_check(const HealthCheckRequest &request, HealthCheckResponse &response) {
        ClientContext context;
        init_client_context(context);
        return m_health_stub->Check(&context, request, &response);
    }

    void set_test_id(std::string test_id) {
        m_test_id = std::move(test_id);
    }

    std::string test_id() {
        return m_test_id;
    }

private:
    std::unique_ptr<ServerManager::Stub> m_stub;
    std::unique_ptr<Health::Stub> m_health_stub;
    std::string m_test_id;

    void init_client_context(ClientContext &context) {
        context.set_wait_for_ready(true);
        context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(90));
        context.AddMetadata("test-id", test_id());
        context.AddMetadata("request-id", request_id());
    }

    static std::string request_id() {
        uint64_t request_id =
            static_cast<uint64_t>(std::time(nullptr)) << 32 | (std::rand() & 0xFFFFFFFF); // NOLINT: rand is ok for this
        return std::to_string(request_id);
    }
};

// using config_function = void (*)(machine_config &);
using test_function = void (*)(ServerManagerClient &);
using test_setup = void (*)(const std::function<void(const std::string &, test_function)> &);

class test_suite final {
public:
    test_suite(ServerManagerClient &manager) : m_manager{manager}, m_suite{}, m_total_tests{0} {}

    void add_test_set(const std::string &title, test_setup setup) {
        m_suite.emplace_back(title, std::vector<std::pair<std::string, test_function>>());
        auto &tests = m_suite.back().second;
        setup([&tests, this](const std::string &title, test_function f) {
            tests.emplace_back(title, f);
            ++m_total_tests;
        });
    }

    int run() {
        int total = 0;
        int total_failed = 0;
        std::cerr << "\nRunning tests:\n\n";
        for (const auto &[test, cases] : m_suite) {
            int failed = 0;
            std::cerr << test << ": ";
            for (const auto &[c, f] : cases) {
                try {
                    std::cerr << ".";
                    m_manager.set_test_id(std::to_string(total));
                    (*f)(m_manager);
                } catch (std::exception &e) {
                    if (failed == 0) {
                        std::cerr << " FAILED";
                    }
                    std::cerr << "\n  - [" << std::to_string(total) + "] '" << c << "' expected result failed:\n\t"
                              << e.what() << std::endl;
                    failed++;
                }
                total++;
            }
            if (failed == 0) {
                std::cerr << " OK";
            }
            std::cerr << std::endl;
            total_failed += failed;
        }
        std::cerr << m_total_tests - total_failed << " of " << m_total_tests << " tests passed" << std::endl;
        return total_failed;
    }

private:
    ServerManagerClient &m_manager;
    std::vector<std::pair<std::string, std::vector<std::pair<std::string, test_function>>>> m_suite;
    unsigned int m_total_tests;
};

static void get_word_hash(cryptopp_keccak_256_hasher &h, const unsigned char *word, int log2_word_size,
    cryptopp_keccak_256_hasher::hash_type &hash) {
    h.begin();
    h.add_data(word, 1 << log2_word_size);
    h.end(hash);
}

static cryptopp_keccak_256_hasher::hash_type get_leaf_hash(cryptopp_keccak_256_hasher &h,
    const unsigned char *leaf_data, int log2_leaf_size, int log2_word_size) {
    assert(log2_leaf_size >= log2_word_size);
    if (log2_leaf_size > log2_word_size) {
        cryptopp_keccak_256_hasher::hash_type left = get_leaf_hash(h, leaf_data, log2_leaf_size - 1, log2_word_size);
        cryptopp_keccak_256_hasher::hash_type right =
            get_leaf_hash(h, leaf_data + (1 << (log2_leaf_size - 1)), log2_leaf_size - 1, log2_word_size);
        get_concat_hash(h, left, right, left);
        return left;
    } else {
        cryptopp_keccak_256_hasher::hash_type leaf;
        get_word_hash(h, leaf_data, log2_word_size, leaf);
        return leaf;
    }
}

#if 0
static void get_hash(const unsigned char *data, size_t size,
    cryptopp_keccak_256_hasher::hash_type &hash) {
    cryptopp_keccak_256_hasher h;
    h.begin();
    h.add_data(data, size);
    h.end(hash);
}

static void print_hash(const cryptopp_keccak_256_hasher::hash_type &hash, FILE *f) {
    for (auto b: hash) {
        (void) fprintf(f, "%02x", static_cast<int>(b));
    }
    (void) fprintf(f, "\n");
}

static void print_json_message(const google::protobuf::Message &msg, bool pretty = false) {
    std::string json_msg;
    google::protobuf::util::JsonOptions json_opts;
    json_opts.add_whitespace = pretty;
    google::protobuf::util::Status s = MessageToJsonString(msg, &json_msg, json_opts);
    if (s.ok()) {
        std::cerr << std::endl << json_msg << std::endl ;
    }
}
#endif

static uint64_t new_session_id() {
    static uint64_t session_id = 1;
    return session_id++;
}

static path get_machine_directory(const std::string &storage_path, const std::string &machine) {
    return MANAGER_ROOT_DIR / storage_path / machine;
}

static bool delete_storage_directory(const std::string &storage_path) {
    if (storage_path.empty()) {
        return false;
    }
    return remove_all(MANAGER_ROOT_DIR / storage_path) > 0;
}

static bool create_storage_directory(const std::string &storage_path, bool rebuild = true) {
    if (storage_path.empty()) {
        return false;
    }
    path root_path = MANAGER_ROOT_DIR / storage_path;
    if (exists(root_path)) {
        if (!rebuild) {
            return true;
        }
        if (!delete_storage_directory(storage_path)) {
            return false;
        }
    }
    return create_directories(root_path) > 0;
}

static bool change_storage_directory_permissions(const std::string &storage_path, bool writable) {
    if (storage_path.empty()) {
        return false;
    }
    auto new_perms = writable ? (perms::owner_all) : (perms::owner_read | perms::owner_exec);
    std::error_code ec;
    permissions(MANAGER_ROOT_DIR / storage_path, new_perms, ec);
    return ec.value() == 0;
}

static StartSessionRequest create_valid_start_session_request(const std::string &name = "advance-state-machine") {
    // Convert to proto message
    StartSessionRequest session_request;
    std::string *machine_directory = session_request.mutable_machine_directory();
    *machine_directory = get_machine_directory("tests", name);

    session_request.set_session_id("test_session_request_id:" + std::to_string(new_session_id()));
    session_request.set_active_epoch_index(0);

    CyclesConfig *server_cycles = session_request.mutable_server_cycles();
    server_cycles->set_max_advance_state(UINT64_MAX >> 2);
    server_cycles->set_advance_state_increment(1 << 22);
    server_cycles->set_max_inspect_state(UINT64_MAX >> 2);
    server_cycles->set_inspect_state_increment(1 << 22);

    // Set server_deadline
    auto *server_deadline = session_request.mutable_server_deadline();
    server_deadline->set_checkin(1000ULL * 60);
    server_deadline->set_advance_state(1000ULL * 60 * 3);
    server_deadline->set_advance_state_increment(1000ULL * 10);
    server_deadline->set_inspect_state(1000ULL * 60 * 3);
    server_deadline->set_inspect_state_increment(1000ULL * 10);
    server_deadline->set_machine(1000ULL * 60);
    server_deadline->set_store(1000ULL * 60 * 3);
    server_deadline->set_fast(1000ULL * 5);

    return session_request;
}

// NOLINTNEXTLINE: ignore static initialization warning
static const std::string INPUT_ADDRESS_1 = "fafafafafafafafafafafafafafafafafafafafa";
// NOLINTNEXTLINE: ignore static initialization warning
static const std::string VOUCHER_ADDRESS_1 = "000000000000000000000000fafafafafafafafafafafafafafafafafafafafa";
// NOLINTNEXTLINE: ignore static initialization warning
static const std::string VOUCHER_OFFSET_1 = "0000000000000000000000000000000000000000000000000000000000000040";
// NOLINTNEXTLINE: ignore static initialization warning
static const std::string VOUCHER_LENGTH_1 = "0000000000000000000000000000000000000000000000000000000000000080";
// NOLINTNEXTLINE: ignore static initialization warning
static const std::string VOUCHER_PAYLOAD_1 = "6361727465736920726f6c6c7570206d616368696e65206d616e616765722020"
                                             "6361727465736920726f6c6c7570206d616368696e65206d616e616765722020"
                                             "6361727465736920726f6c6c7570206d616368696e65206d616e616765722020"
                                             "6361727465736920726f6c6c7570206d616368696e65206d616e616765720000";
// NOLINTNEXTLINE: ignore static initialization warning
static const std::string VOUCHER_KECCAK_1 = "028c8a06ce878fcd02522f0ca3174f9e6fe7c9267750a0c45844e597e7cbab03";

// NOLINTNEXTLINE: ignore static initialization warning
static const std::string INPUT_ADDRESS_2 = "babababababababababababababababababababa";
// NOLINTNEXTLINE: ignore static initialization warning
static const std::string VOUCHER_ADDRESS_2 = "000000000000000000000000babababababababababababababababababababa";
// NOLINTNEXTLINE: ignore static initialization warning
static const std::string VOUCHER_OFFSET_2 = "0000000000000000000000000000000000000000000000000000000000000040";
// NOLINTNEXTLINE: ignore static initialization warning
static const std::string VOUCHER_LENGTH_2 = "0000000000000000000000000000000000000000000000000000000000000020";
// NOLINTNEXTLINE: ignore static initialization warning
static const std::string VOUCHER_PAYLOAD_2 = "4c6f72656d20697073756d20646f6c6f722073697420616d657420637261732e";
// NOLINTNEXTLINE: ignore static initialization warning
static const std::string VOUCHER_KECCAK_2 = "4af9ac1565a66632741c1cf848847920ae4ef6e7e96ef9fd5bae9fa316f5cb33";

// NOLINTNEXTLINE: ignore static initialization warning
static const std::string NOTICE_OFFSET_1 = "0000000000000000000000000000000000000000000000000000000000000020";
// NOLINTNEXTLINE: ignore static initialization warning
static const std::string NOTICE_LENGTH_1 = VOUCHER_LENGTH_1;
// NOLINTNEXTLINE: ignore static initialization warning
static const std::string NOTICE_PAYLOAD_1 = VOUCHER_PAYLOAD_1;
// NOLINTNEXTLINE: ignore static initialization warning
static const std::string NOTICE_KECCAK_1 = "253f38cb583d6aba613e7f75bde205c74280bd321be826b93eb41a5404c1f508";

// NOLINTNEXTLINE: ignore static initialization warning
static const std::string NOTICE_OFFSET_2 = "0000000000000000000000000000000000000000000000000000000000000020";
// NOLINTNEXTLINE: ignore static initialization warning
static const std::string NOTICE_LENGTH_2 = VOUCHER_LENGTH_2;
// NOLINTNEXTLINE: ignore static initialization warning
static const std::string NOTICE_PAYLOAD_2 = VOUCHER_PAYLOAD_2;
// NOLINTNEXTLINE: ignore static initialization warning
static const std::string NOTICE_KECCAK_2 = "8c35a8e6f7e96bf5b0f9200e6cf35db282e9de960e9e958c5d52b14a66af6c47";

static void hex_string_to_binary(const std::string &input, std::string &dest) {
#ifndef __clang_analyzer__
    CryptoPP::StringSource ss(input, true,
        new CryptoPP::HexDecoder(new CryptoPP::StringSink(dest))); // NOLINT: suppress cryptopp warnings
#else
    (void) input;
    (void) dest;
#endif
}

static std::string get_voucher_keccak(uint64_t index) {
    std::string value;
    hex_string_to_binary((index & 0x1) ? VOUCHER_KECCAK_2 : VOUCHER_KECCAK_1, value);
    return value;
}

static std::string get_voucher_address(uint64_t index) {
    std::string value;
    hex_string_to_binary((index & 0x1) ? INPUT_ADDRESS_2 : INPUT_ADDRESS_1, value);
    return value;
}

static std::string get_voucher_payload(uint64_t index) {
    std::string value;
    hex_string_to_binary((index & 0x1) ? VOUCHER_PAYLOAD_2 : VOUCHER_PAYLOAD_1, value);
    return value;
}

static std::string get_notice_keccak(uint64_t index) {
    std::string value;
    hex_string_to_binary((index & 0x1) ? NOTICE_KECCAK_2 : NOTICE_KECCAK_1, value);
    return value;
}

static std::string get_notice_payload(uint64_t index) {
    std::string value;
    hex_string_to_binary((index & 0x1) ? VOUCHER_PAYLOAD_2 : VOUCHER_PAYLOAD_1, value);
    return value;
}

static std::string get_report_payload(uint64_t index) {
    std::string value;
    hex_string_to_binary((index & 0x1) ? VOUCHER_PAYLOAD_2 : VOUCHER_PAYLOAD_1, value);
    return value;
}

static inline int ilog2(uint64_t v) {
    return 63 - __builtin_clzll(v);
}

static cryptopp_keccak_256_hasher::hash_type get_data_hash(cryptopp_keccak_256_hasher &h, int log2_root_size,
    const std::string &data) {
    cartesi::complete_merkle_tree tree{log2_root_size, LOG2_WORD_SIZE, LOG2_WORD_SIZE};
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    const auto *data_c_str = reinterpret_cast<const unsigned char *>(data.c_str());
    uint64_t leaf_size = UINT64_C(1) << LOG2_WORD_SIZE;
    for (uint64_t i = 0; i < data.size(); i += leaf_size) {
        // Compute leaf hash
        auto leaf_hash = get_leaf_hash(h, data_c_str + i, 3, 3);
        // Add leaf to the tree
        tree.push_back(leaf_hash);
    }
    return tree.get_root_hash();
}

static cryptopp_keccak_256_hasher::hash_type get_voucher_keccak_hash(cryptopp_keccak_256_hasher &h, uint64_t index) {
    std::string keccak = get_voucher_keccak(index);
    return get_data_hash(h, LOG2_KECCAK_SIZE, keccak);
}

static cryptopp_keccak_256_hasher::hash_type get_notice_keccak_hash(cryptopp_keccak_256_hasher &h, uint64_t index) {
    std::string keccak = get_notice_keccak(index);
    return get_data_hash(h, LOG2_KECCAK_SIZE, keccak);
}

static cryptopp_keccak_256_hasher::hash_type get_voucher_root_hash(cryptopp_keccak_256_hasher &h, uint64_t index,
    uint64_t count) {
    std::string metadata_content;
    for (uint64_t i = 0; i < count; i++) {
        metadata_content += get_voucher_keccak(index);
    }
    return get_data_hash(h, ilog2(MEMORY_REGION_LENGTH), metadata_content);
}

static cryptopp_keccak_256_hasher::hash_type get_notice_root_hash(cryptopp_keccak_256_hasher &h, uint64_t index,
    uint64_t count) {
    std::string metadata_content;
    for (uint64_t i = 0; i < count; i++) {
        metadata_content += get_notice_keccak(index);
    }
    return get_data_hash(h, ilog2(MEMORY_REGION_LENGTH), metadata_content);
}

static void init_valid_advance_state_request(AdvanceStateRequest &request, const std::string &session_id,
    uint64_t epoch, uint64_t input_index) {
    request.set_session_id(session_id);
    request.set_active_epoch_index(epoch);
    request.set_current_input_index(input_index);

    static uint64_t block_number = 1;
    auto *input_metadata = request.mutable_input_metadata();
    auto *address = input_metadata->mutable_msg_sender()->mutable_data();
    *address = get_voucher_address(input_index);
    input_metadata->set_block_number(block_number);
    input_metadata->set_timestamp(static_cast<uint64_t>(std::time(nullptr)));
    input_metadata->set_epoch_index(0);
    input_metadata->set_input_index(input_index);

    auto *input_payload = request.mutable_input_payload();
    *input_payload = get_voucher_payload(input_index); // NOLINT: suppres crytopp warnings
}

static void init_valid_inspect_state_request(InspectStateRequest &request, const std::string &session_id,
    uint64_t input) {
    request.set_session_id(session_id);

    auto *query_payload = request.mutable_query_payload();
    *query_payload = get_report_payload(input); // NOLINT: suppres crytopp warnings
}

static void init_valid_finish_epoch_request(FinishEpochRequest &epoch_request, const std::string &session_id,
    uint64_t epoch, uint64_t processed_input_count_within_epoch, const std::string &dir = std::string{}) {
    epoch_request.set_session_id(session_id);
    epoch_request.set_active_epoch_index(epoch);
    epoch_request.set_processed_input_count_within_epoch(processed_input_count_within_epoch);
    if (!dir.empty()) {
        auto *storage_directory = epoch_request.mutable_storage_directory();
        (*storage_directory) = dir;
    }
}

static void assert_status(Status &status, const std::string &rpcname, bool expected, const std::string &file,
    int line) {
    if (status.ok() != expected) {
        if (expected) {
            throw std::runtime_error("Call to " + rpcname + " failed. Code: " + std::to_string(status.error_code()) +
                " Message: " + status.error_message() + ". Assert at " + file + ":" + std::to_string(line));
        }
        throw std::runtime_error("Call to " + rpcname + " succeded when was expected to fail. Assert at " + file + ":" +
            std::to_string(line));
    }
}

static void assert_status_code(const Status &status, const std::string &rpcname, grpc::StatusCode expected,
    const std::string &file, int line) {
    if (status.error_code() != expected) {
        throw std::runtime_error(rpcname + " was expected to fail with Code: " + std::to_string(expected) +
            " but received " + std::to_string(status.error_code()) + " Message: " + status.error_message() +
            ". Assert at " + file + ":" + std::to_string(line));
    }
}

void assert_bool(bool value, const std::string &msg, const std::string &file, int line) {
    if (!value) {
        throw std::runtime_error(msg + ". Assert at " + file + ":" + std::to_string(line));
    }
}

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define ASSERT(v, msg) assert_bool(v, msg, __FILE__, __LINE__)
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define ASSERT_STATUS(s, f, v) assert_status(s, f, v, __FILE__, __LINE__)
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define ASSERT_STATUS_CODE(s, f, v) assert_status_code(s, f, v, __FILE__, __LINE__)

static void test_get_version(const std::function<void(const std::string &title, test_function f)> &test) {
    test("The server-manager server version should be 0.7.x", [](ServerManagerClient &manager) {
        Versioning::GetVersionResponse response;
        Status status = manager.get_version(response);
        ASSERT_STATUS(status, "GetVersion", true);
        ASSERT((response.version().major() == 0), "Version Major should be 0");
        ASSERT((response.version().minor() == 7), "Version Minor should be 6");
    });
}

static void test_start_session(const std::function<void(const std::string &title, test_function f)> &test) {
    test("Should complete a valid request with success", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request();
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        // EndSession
        EndSessionRequest end_session_request;
        end_session_request.set_session_id(session_request.session_id());
        status = manager.end_session(end_session_request);
        ASSERT_STATUS(status, "EndSession", true);
    });

    test("Should fail to complete a request with a invalid session id", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request();
        StartSessionResponse session_response;
        session_request.clear_session_id();
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", false);
        ASSERT_STATUS_CODE(status, "StartSession", StatusCode::INVALID_ARGUMENT);
    });

    test("Should fail to complete a 2nd request with same session id", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request();
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        // repeat request
        status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", false);
        ASSERT_STATUS_CODE(status, "StartSession", StatusCode::ALREADY_EXISTS);

        // EndSession
        EndSessionRequest end_session_request;
        end_session_request.set_session_id(session_request.session_id());
        status = manager.end_session(end_session_request);
        ASSERT_STATUS(status, "EndSession", true);
    });

    test("Should be able to reutilise an session id", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request();
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        // EndSession
        EndSessionRequest end_session_request;
        end_session_request.set_session_id(session_request.session_id());
        status = manager.end_session(end_session_request);
        ASSERT_STATUS(status, "EndSession", true);

        // repeat request
        status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        // EndSession
        status = manager.end_session(end_session_request);
        ASSERT_STATUS(status, "EndSession", true);
    });

    test("Should fail to complete a request with a invalid machine request", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request();
        StartSessionResponse session_response;
        // clear machine request
        session_request.clear_machine_directory();
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", false);
        ASSERT_STATUS_CODE(status, "StartSession", StatusCode::INVALID_ARGUMENT);
    });

    test("Should fail to complete when first yield reason is not accepted or rejected",
        [](ServerManagerClient &manager) {
            StartSessionRequest session_request = create_valid_start_session_request("init-exception-machine");
            StartSessionResponse session_response;
            Status status = manager.start_session(session_request, session_response);
            ASSERT_STATUS(status, "StartSession", false);
            ASSERT_STATUS_CODE(status, "StartSession", StatusCode::INVALID_ARGUMENT);
        });

    test("Should fail to complete when config.htif.yield_manual = false", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request("no-manual-yield-machine");
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", false);
        ASSERT_STATUS_CODE(status, "StartSession", StatusCode::INVALID_ARGUMENT);
    });

    test("Should fail to complete when config.htif.yield_automatic = false", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request("no-automatic-yield-machine");
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", false);
        ASSERT_STATUS_CODE(status, "StartSession", StatusCode::INVALID_ARGUMENT);
    });

    test("Should fail to complete when config.htif.console_getchar = true", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request("console-getchar-machine");
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", false);
        ASSERT_STATUS_CODE(status, "StartSession", StatusCode::INVALID_ARGUMENT);
    });

    test("Should fail to complete when machine config rollup is undefined", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request("no-rollup-machine");
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", false);
        ASSERT_STATUS_CODE(status, "StartSession", StatusCode::INVALID_ARGUMENT);
    });

    test("Should fail to complete if any of the rollup memory regions are shared", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request("shared-rx-buffer-machine");
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", false);
        ASSERT_STATUS_CODE(status, "StartSession", StatusCode::INVALID_ARGUMENT);

        session_request = create_valid_start_session_request("shared-tx-buffer-machine");
        status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", false);
        ASSERT_STATUS_CODE(status, "StartSession", StatusCode::INVALID_ARGUMENT);

        session_request = create_valid_start_session_request("shared-input-metadata-machine");
        status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", false);
        ASSERT_STATUS_CODE(status, "StartSession", StatusCode::INVALID_ARGUMENT);

        session_request = create_valid_start_session_request("shared-voucher-hashes-machine");
        status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", false);
        ASSERT_STATUS_CODE(status, "StartSession", StatusCode::INVALID_ARGUMENT);

        session_request = create_valid_start_session_request("shared-notice-hashes-machine");
        status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", false);
        ASSERT_STATUS_CODE(status, "StartSession", StatusCode::INVALID_ARGUMENT);
    });

    test("Should fail to complete if active epoch is on the limit", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request();
        StartSessionResponse session_response;
        session_request.set_active_epoch_index(UINT64_MAX);
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", false);
        ASSERT_STATUS_CODE(status, "StartSession", StatusCode::OUT_OF_RANGE);

        session_request.set_active_epoch_index(UINT64_MAX - 1);
        status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        // end session
        EndSessionRequest end_session_request;
        end_session_request.set_session_id(session_request.session_id());
        status = manager.end_session(end_session_request);
        ASSERT_STATUS(status, "EndSession", true);
    });

    test("Should fail to complete a request with an undefined server_cycles", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request();
        StartSessionResponse session_response;
        // clear server_cycles
        session_request.clear_server_cycles();
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", false);
        ASSERT_STATUS_CODE(status, "StartSession", StatusCode::INVALID_ARGUMENT);
    });

    test("Should fail to complete a request if server_cycles.max_advance_state == 0", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request();
        StartSessionResponse session_response;
        auto *server_cycles = session_request.mutable_server_cycles();
        server_cycles->set_max_advance_state(0);
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", false);
        ASSERT_STATUS_CODE(status, "StartSession", StatusCode::INVALID_ARGUMENT);
    });

    test("Should fail to complete a request if server_cycles.advance_state_increment == 0",
        [](ServerManagerClient &manager) {
            StartSessionRequest session_request = create_valid_start_session_request();
            StartSessionResponse session_response;
            auto *server_cycles = session_request.mutable_server_cycles();
            server_cycles->set_advance_state_increment(0);
            Status status = manager.start_session(session_request, session_response);
            ASSERT_STATUS(status, "StartSession", false);
            ASSERT_STATUS_CODE(status, "StartSession", StatusCode::INVALID_ARGUMENT);
        });

    test("Should fail to complete a request if server_cycles.max_advance_state < server_cycles.advance_state_increment",
        [](ServerManagerClient &manager) {
            StartSessionRequest session_request = create_valid_start_session_request();
            StartSessionResponse session_response;
            auto *server_cycles = session_request.mutable_server_cycles();
            server_cycles->set_max_advance_state(server_cycles->advance_state_increment() - 1);
            Status status = manager.start_session(session_request, session_response);
            ASSERT_STATUS(status, "StartSession", false);
            ASSERT_STATUS_CODE(status, "StartSession", StatusCode::INVALID_ARGUMENT);
        });

    test("Should fail to complete a request if server_cycles.max_inspect_state == 0", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request();
        StartSessionResponse session_response;
        auto *server_cycles = session_request.mutable_server_cycles();
        server_cycles->set_max_inspect_state(0);
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", false);
        ASSERT_STATUS_CODE(status, "StartSession", StatusCode::INVALID_ARGUMENT);
    });

    test("Should fail to complete a request if server_cycles.inspect_state_increment == 0",
        [](ServerManagerClient &manager) {
            StartSessionRequest session_request = create_valid_start_session_request();
            StartSessionResponse session_response;
            auto *server_cycles = session_request.mutable_server_cycles();
            server_cycles->set_inspect_state_increment(0);
            Status status = manager.start_session(session_request, session_response);
            ASSERT_STATUS(status, "StartSession", false);
            ASSERT_STATUS_CODE(status, "StartSession", StatusCode::INVALID_ARGUMENT);
        });

    test("Should fail to complete a request if server_cycles.max_inspect_state < server_cycles.inspect_state_increment",
        [](ServerManagerClient &manager) {
            StartSessionRequest session_request = create_valid_start_session_request();
            StartSessionResponse session_response;
            auto *server_cycles = session_request.mutable_server_cycles();
            server_cycles->set_max_inspect_state(server_cycles->inspect_state_increment() - 1);
            Status status = manager.start_session(session_request, session_response);
            ASSERT_STATUS(status, "StartSession", false);
            ASSERT_STATUS_CODE(status, "StartSession", StatusCode::INVALID_ARGUMENT);
        });

    test("Should fail to complete a request with an undefined server_deadline", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request();
        StartSessionResponse session_response;
        // clear server_deadline
        session_request.clear_server_deadline();
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", false);
        ASSERT_STATUS_CODE(status, "StartSession", StatusCode::INVALID_ARGUMENT);
    });

    test("Should fail to complete a request if server_deadline.advance_state < server_deadline.advance_state_increment",
        [](ServerManagerClient &manager) {
            StartSessionRequest session_request = create_valid_start_session_request();
            StartSessionResponse session_response;
            auto *server_deadline = session_request.mutable_server_deadline();
            server_deadline->set_advance_state(server_deadline->advance_state_increment() - 1);
            Status status = manager.start_session(session_request, session_response);
            ASSERT_STATUS(status, "StartSession", false);
            ASSERT_STATUS_CODE(status, "StartSession", StatusCode::INVALID_ARGUMENT);
        });

    test("Should fail to complete a request if server_deadline.inspect_state < server_deadline.inspect_state_increment",
        [](ServerManagerClient &manager) {
            StartSessionRequest session_request = create_valid_start_session_request();
            StartSessionResponse session_response;
            auto *server_deadline = session_request.mutable_server_deadline();
            server_deadline->set_inspect_state(server_deadline->inspect_state_increment() - 1);
            Status status = manager.start_session(session_request, session_response);
            ASSERT_STATUS(status, "StartSession", false);
            ASSERT_STATUS_CODE(status, "StartSession", StatusCode::INVALID_ARGUMENT);
        });

    test("Should fail to complete a request when the check-in deadline is reached", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request();
        StartSessionResponse session_response;
        auto *server_deadline = session_request.mutable_server_deadline();
        server_deadline->set_checkin(1);
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", false);
        ASSERT_STATUS_CODE(status, "StartSession", StatusCode::INTERNAL);
    });
}

static void wait_pending_inputs_to_be_processed(ServerManagerClient &manager, GetEpochStatusRequest &status_request,
    GetEpochStatusResponse &status_response, bool accept_tainted, int retries) {
    for (;;) {
        Status status = manager.get_epoch_status(status_request, status_response);
        ASSERT_STATUS(status, "GetEpochStatus", true);

        ASSERT(accept_tainted || !status_response.has_taint_status(),
            "tainted session was not expected: " + status_response.taint_status().error_message());
        if (accept_tainted && status_response.has_taint_status()) {
            break;
        }

        if (status_response.pending_input_count() == 0) {
            break;
        }

        ASSERT((retries > 0), "wait_pending_inputs_to_be_processed max retries reached");
        std::this_thread::sleep_for(3s);
        retries--;
    }
}

static machine_merkle_tree::proof_type assemble_merkle_proof(int log2_root_size,
    const machine_merkle_tree::hash_type &target_hash, const machine_merkle_tree::hash_type &root_hash,
    const google::protobuf::RepeatedPtrField<::CartesiMachine::Hash> &siblings, uint64_t input_index) {
    if (log2_root_size - siblings.size() != LOG2_KECCAK_SIZE) {
        throw std::invalid_argument("wrong number of sibling hashes");
    }
    machine_merkle_tree::proof_type p{log2_root_size, LOG2_KECCAK_SIZE};
    for (int i = 0; i < siblings.size(); ++i) {
        p.set_sibling_hash(get_proto_hash(siblings[i]), LOG2_KECCAK_SIZE + i);
    }
    p.set_target_address(input_index << LOG2_KECCAK_SIZE);
    p.set_target_hash(target_hash);
    p.set_root_hash(root_hash);
    return p;
}

static void assemble_output_epoch_trees(FinishEpochResponse &response, cartesi::complete_merkle_tree &vouchers_tree,
    cartesi::complete_merkle_tree &notices_tree, uint64_t input_count, bool skipped = false) {
    cryptopp_keccak_256_hasher h;
    if (response.proofs_size() == 0) {
        auto hash =
            skipped ? cryptopp_keccak_256_hasher::hash_type{} : get_data_hash(h, ilog2(MEMORY_REGION_LENGTH), "");
        for (uint64_t i = 0; i < input_count; i++) {
            vouchers_tree.push_back(hash);
            notices_tree.push_back(hash);
        }
        return;
    }
    uint64_t input_index = 0;
    std::unordered_map<uint64_t, std::pair<uint64_t, uint64_t>> outputs_per_input;
    for (const auto &proof : response.proofs()) {
        if (outputs_per_input.count(proof.validity().input_index_within_epoch()) == 0) {
            input_index = proof.validity().input_index_within_epoch();
            outputs_per_input[input_index] = std::make_pair(0, 0);
        }
        auto &[voucher_count, notice_count] = outputs_per_input.at(input_index);
        if (proof.output_enum() == OutputEnum::VOUCHER) {
            voucher_count++;
        } else if (proof.output_enum() == OutputEnum::NOTICE) {
            notice_count++;
        } else {
            ASSERT(false, "Invalid proof output type");
        }
    }
    ASSERT(input_index == input_count - 1, "input_index should match input_count - 1");
    for (uint64_t i = 0; i <= input_index; i++) {
        const auto &[voucher_count, notice_count] = outputs_per_input.at(i);
        auto voucher_root_hash = get_voucher_root_hash(h, i, voucher_count);
        auto notice_root_hash = get_notice_root_hash(h, i, notice_count);
        vouchers_tree.push_back(voucher_root_hash);
        notices_tree.push_back(notice_root_hash);
    }
}

static uint64_t get_abi_encoded_context(const std::string &context) {
    using namespace boost::endian;
    const auto *end = context.data() + context.size();
    return endian_load<uint64_t, sizeof(uint64_t), order::big>(
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        reinterpret_cast<const unsigned char *>(end) - sizeof(uint64_t));
}

static void verify_proof(FinishEpochResponse &response, const Proof &proof, uint64_t epoch_index,
    const cartesi::complete_merkle_tree &vouchers_tree, const cartesi::complete_merkle_tree &notices_tree) {
    ASSERT(!proof.context().empty(), "Proof should have a valid context");
    ASSERT(get_abi_encoded_context(proof.context()) == epoch_index,
        "Proof context should match ABI encoded epoch index");

    ASSERT(proof.has_validity(), "Proof should have an OutputValidityProof instance");
    const auto &validity = proof.validity();
    // Check output indices
    ASSERT(proof.output_index() == validity.output_index_within_input(), "Proof should have a valid output index");
    // Check if validity proof has all the required fields
    ASSERT(validity.has_output_hashes_root_hash() && !validity.output_hashes_root_hash().data().empty(),
        "Validity proof should have a valid output hashes root hash");
    ASSERT(validity.has_vouchers_epoch_root_hash() && !validity.vouchers_epoch_root_hash().data().empty(),
        "Validity proof should have a valid vouchers epoch root hash");
    ASSERT(validity.has_notices_epoch_root_hash() && !validity.notices_epoch_root_hash().data().empty(),
        "Validity proof should have a valid notices epoch root hash");
    ASSERT(validity.has_machine_state_hash() && !validity.machine_state_hash().data().empty(),
        "Validity proof should have a valid machine state hash");
    ASSERT(validity.output_hash_in_output_hashes_siblings_size() > 0,
        "Proof should have output hash in output hashes siblings");
    ASSERT(validity.output_hashes_in_epoch_siblings_size() > 0, "Proof should have output in hashes epoch siblings");

    // Check if validity proof fields match the ones in finish epoch response
    ASSERT(validity.machine_state_hash().data() == response.machine_hash().data(),
        "Validity proof machine state hash should should match the one in finish epoch response");
    ASSERT(validity.vouchers_epoch_root_hash().data() == response.vouchers_epoch_root_hash().data(),
        "Validity proof vouchers epoch root hash should should match the one in finish epoch response");
    ASSERT(validity.notices_epoch_root_hash().data() == response.notices_epoch_root_hash().data(),
        "Validity proof notices epoch root hash should should match the one in finish epoch response");

    // verify proofs
    cryptopp_keccak_256_hasher h;
    auto metadata_log2_size = ilog2(MEMORY_REGION_LENGTH);
    const auto &output_hash_in_output_hashes_siblings = validity.output_hash_in_output_hashes_siblings();
    const auto &output_hashes_in_epoch_siblings = validity.output_hashes_in_epoch_siblings();
    const auto &output_hashes_root_hash = get_proto_hash(validity.output_hashes_root_hash());

    if (proof.output_enum() == OutputEnum::VOUCHER) {
        ASSERT(output_hashes_root_hash ==
                vouchers_tree.get_node_hash(validity.input_index_within_epoch() << LOG2_KECCAK_SIZE, LOG2_KECCAK_SIZE),
            "Received output hashes root hash should match the calculated one");

        const auto &output_target_hash = get_voucher_keccak_hash(h, validity.input_index_within_epoch());
        auto p1 = assemble_merkle_proof(metadata_log2_size, output_target_hash, output_hashes_root_hash,
            output_hash_in_output_hashes_siblings, validity.input_index_within_epoch());
        ASSERT(p1.verify(h),
            "OutputValidityProof output_hashes_root_hash and output_hash_in_output_hashes_siblings verification "
            "failed");

        const auto &output_epoch_root_hash = get_proto_hash(validity.vouchers_epoch_root_hash());
        ASSERT(output_epoch_root_hash == vouchers_tree.get_root_hash(),
            "Received vouchers epoch root hash should match the calculated one");

        auto p2 = assemble_merkle_proof(LOG2_ROOT_SIZE, output_hashes_root_hash, output_epoch_root_hash,
            output_hashes_in_epoch_siblings, validity.input_index_within_epoch());
        ASSERT(p2.verify(h),
            "OutputValidityProof vouchers_epoch_root_hash and output_hashes_in_epoch_siblings verification failed");

    } else {
        ASSERT(output_hashes_root_hash ==
                notices_tree.get_node_hash(validity.input_index_within_epoch() << LOG2_KECCAK_SIZE, LOG2_KECCAK_SIZE),
            "Received output hashes root hash should match the calculated one");

        const auto &output_target_hash = get_notice_keccak_hash(h, validity.input_index_within_epoch());
        auto p1 = assemble_merkle_proof(metadata_log2_size, output_target_hash, output_hashes_root_hash,
            output_hash_in_output_hashes_siblings, validity.input_index_within_epoch());
        ASSERT(p1.verify(h),
            "OutputValidityProof output_hashes_root_hash and output_hash_in_output_hashes_siblings verification "
            "failed");

        const auto &output_epoch_root_hash = get_proto_hash(validity.notices_epoch_root_hash());
        ASSERT(output_epoch_root_hash == notices_tree.get_root_hash(),
            "Received notices epoch root hash should match the calculated one");

        auto p2 = assemble_merkle_proof(LOG2_ROOT_SIZE, output_hashes_root_hash, output_epoch_root_hash,
            output_hashes_in_epoch_siblings, validity.input_index_within_epoch());
        ASSERT(p2.verify(h),
            "OutputValidityProof notices_epoch_root_hash and output_hashes_in_epoch_siblings verification failed");
    }
}

static void validate_finish_epoch_response(FinishEpochResponse &response, uint64_t epoch_index, uint64_t input_count,
    bool skipped = false) {
    ASSERT(response.has_machine_hash() && !response.machine_hash().data().empty(),
        "Finish epoch response should have a valid machine hash");
    ASSERT(response.has_vouchers_epoch_root_hash() && !response.vouchers_epoch_root_hash().data().empty(),
        "Finish epoch response should have a valid root hash for Merkle tree of voucher hashes memory ranges");
    ASSERT(response.has_notices_epoch_root_hash() && !response.notices_epoch_root_hash().data().empty(),
        "Finish epoch response should have a valid root hash for Merkle tree of notices hashes memory ranges");

    cartesi::complete_merkle_tree vouchers_tree{LOG2_ROOT_SIZE, LOG2_KECCAK_SIZE, LOG2_KECCAK_SIZE};
    cartesi::complete_merkle_tree notices_tree{LOG2_ROOT_SIZE, LOG2_KECCAK_SIZE, LOG2_KECCAK_SIZE};
    assemble_output_epoch_trees(response, vouchers_tree, notices_tree, input_count, skipped);

    // Chech vouchers and notices epoch root hashes
    ASSERT(get_proto_hash(response.vouchers_epoch_root_hash()) == vouchers_tree.get_root_hash(),
        "Received vouchers epoch root hash should match the calculated one");
    ASSERT(get_proto_hash(response.notices_epoch_root_hash()) == notices_tree.get_root_hash(),
        "Received notices epoch root hash should match the calculated one");

    // Verify proofs
    for (const auto &proof : response.proofs()) {
        verify_proof(response, proof, epoch_index, vouchers_tree, notices_tree);
    }
}

static void end_session_after_processing_pending_inputs(ServerManagerClient &manager, const std::string &session_id,
    uint64_t epoch, bool accept_tainted = false, bool skipped = false) {
    GetEpochStatusRequest status_request;
    GetEpochStatusResponse status_response;

    status_request.set_session_id(session_id);
    status_request.set_epoch_index(epoch);
    wait_pending_inputs_to_be_processed(manager, status_request, status_response, accept_tainted,
        WAITING_PENDING_INPUT_MAX_RETRIES);

    // finish epoch
    if ((!accept_tainted && !status_response.has_taint_status()) && (status_response.state() != EpochState::FINISHED) &&
        (status_response.processed_inputs_size() != 0)) {
        FinishEpochRequest epoch_request;
        FinishEpochResponse epoch_response;
        init_valid_finish_epoch_request(epoch_request, status_request.session_id(), status_request.epoch_index(),
            status_response.processed_inputs_size());
        Status status = manager.finish_epoch(epoch_request, epoch_response);
        ASSERT_STATUS(status, "FinishEpoch", true);
        validate_finish_epoch_response(epoch_response, status_response.epoch_index(),
            status_response.processed_inputs_size(), skipped);
    }

    // EndSession
    EndSessionRequest end_session_request;
    end_session_request.set_session_id(session_id);
    Status status = manager.end_session(end_session_request);
    ASSERT_STATUS(status, "EndSession", true);
}

static void test_advance_state(const std::function<void(const std::string &title, test_function f)> &test) {
    test("Should complete a valid request with success", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request();
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        // enqueue
        AdvanceStateRequest advance_request;
        init_valid_advance_state_request(advance_request, session_request.session_id(),
            session_request.active_epoch_index(), 0);
        status = manager.advance_state(advance_request);
        ASSERT_STATUS(status, "AdvanceState", true);

        end_session_after_processing_pending_inputs(manager, session_request.session_id(),
            session_request.active_epoch_index());
    });

    test("Should complete two valid requests with success", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request();
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        // enqueue first
        AdvanceStateRequest advance_request1;
        init_valid_advance_state_request(advance_request1, session_request.session_id(),
            session_request.active_epoch_index(), 0);
        status = manager.advance_state(advance_request1);
        ASSERT_STATUS(status, "AdvanceState", true);

        // enqueue second
        AdvanceStateRequest advance_request2;
        init_valid_advance_state_request(advance_request2, session_request.session_id(),
            session_request.active_epoch_index(), 1);
        status = manager.advance_state(advance_request2);
        ASSERT_STATUS(status, "AdvanceState", true);

        end_session_after_processing_pending_inputs(manager, session_request.session_id(),
            session_request.active_epoch_index());
    });

    test("Should not be able to enqueue two identical requests", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request();
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        // enqueue first
        AdvanceStateRequest advance_request;
        init_valid_advance_state_request(advance_request, session_request.session_id(),
            session_request.active_epoch_index(), 0);
        status = manager.advance_state(advance_request);
        ASSERT_STATUS(status, "AdvanceState", true);

        // repeated
        status = manager.advance_state(advance_request);
        ASSERT_STATUS(status, "AdvanceState", false);
        ASSERT_STATUS_CODE(status, "AdvanceState", StatusCode::INVALID_ARGUMENT);

        end_session_after_processing_pending_inputs(manager, session_request.session_id(),
            session_request.active_epoch_index());
    });

    test("Should fail to complete if session id is not valid", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request();
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        AdvanceStateRequest advance_request;
        init_valid_advance_state_request(advance_request, session_request.session_id(),
            session_request.active_epoch_index(), 0);
        advance_request.set_session_id("NON-EXISTENT");
        status = manager.advance_state(advance_request);
        ASSERT_STATUS(status, "AdvanceState", false);
        ASSERT_STATUS_CODE(status, "AdvanceState", StatusCode::INVALID_ARGUMENT);

        // end session
        EndSessionRequest end_session_request;
        end_session_request.set_session_id(session_request.session_id());
        status = manager.end_session(end_session_request);
        ASSERT_STATUS(status, "EndSession", true);
    });

    test("Should fail to complete if session was ended", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request();
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        // end session
        EndSessionRequest end_session_request;
        end_session_request.set_session_id(session_request.session_id());
        status = manager.end_session(end_session_request);
        ASSERT_STATUS(status, "EndSession", true);

        // try to enqueue input on ended session
        AdvanceStateRequest advance_request;
        init_valid_advance_state_request(advance_request, session_request.session_id(),
            session_request.active_epoch_index(), 0);
        status = manager.advance_state(advance_request);
        ASSERT_STATUS(status, "AdvanceState", false);
        ASSERT_STATUS_CODE(status, "AdvanceState", StatusCode::INVALID_ARGUMENT);
    });

    test("Should fail to complete if epoch is not the same", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request();
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        AdvanceStateRequest advance_request;
        init_valid_advance_state_request(advance_request, session_request.session_id(),
            session_request.active_epoch_index(), 0);
        // change epoch index
        advance_request.set_active_epoch_index(advance_request.active_epoch_index() + 1);
        status = manager.advance_state(advance_request);
        ASSERT_STATUS(status, "AdvanceState", false);
        ASSERT_STATUS_CODE(status, "AdvanceState", StatusCode::INVALID_ARGUMENT);

        // end session
        EndSessionRequest end_session_request;
        end_session_request.set_session_id(session_request.session_id());
        status = manager.end_session(end_session_request);
        ASSERT_STATUS(status, "EndSession", true);
    });

    test("Should fail to complete if epoch is finished", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request();
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        // finish epoch
        FinishEpochRequest epoch_request;
        FinishEpochResponse epoch_response;
        init_valid_finish_epoch_request(epoch_request, session_request.session_id(),
            session_request.active_epoch_index(), 0);
        status = manager.finish_epoch(epoch_request, epoch_response);
        ASSERT_STATUS(status, "FinishEpoch", true);
        validate_finish_epoch_response(epoch_response, session_request.active_epoch_index(), 0);

        // try to enqueue input on ended session
        AdvanceStateRequest advance_request;
        init_valid_advance_state_request(advance_request, session_request.session_id(),
            session_request.active_epoch_index(), 0);
        status = manager.advance_state(advance_request);
        ASSERT_STATUS(status, "AdvanceState", false);
        ASSERT_STATUS_CODE(status, "AdvanceState", StatusCode::INVALID_ARGUMENT);

        // end session
        EndSessionRequest end_session_request;
        end_session_request.set_session_id(session_request.session_id());
        status = manager.end_session(end_session_request);
        ASSERT_STATUS(status, "EndSession", true);
    });

    test("Should complete with success enqueing on a new epoch", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request();
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        // finish epoch
        FinishEpochRequest epoch_request;
        FinishEpochResponse epoch_response;
        init_valid_finish_epoch_request(epoch_request, session_request.session_id(),
            session_request.active_epoch_index(), 0);
        status = manager.finish_epoch(epoch_request, epoch_response);
        ASSERT_STATUS(status, "FinishEpoch", true);
        validate_finish_epoch_response(epoch_response, session_request.active_epoch_index(), 0);

        AdvanceStateRequest advance_request;
        init_valid_advance_state_request(advance_request, session_request.session_id(),
            session_request.active_epoch_index() + 1, 0);
        status = manager.advance_state(advance_request);
        ASSERT_STATUS(status, "AdvanceState", true);

        end_session_after_processing_pending_inputs(manager, session_request.session_id(),
            session_request.active_epoch_index() + 1);
    });

    test("Should fail to complete if active epoch is on the limit", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request();
        StartSessionResponse session_response;
        session_request.set_active_epoch_index(UINT64_MAX - 1);
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        FinishEpochRequest epoch_request;
        FinishEpochResponse epoch_response;
        init_valid_finish_epoch_request(epoch_request, session_request.session_id(),
            session_request.active_epoch_index(), 0);
        status = manager.finish_epoch(epoch_request, epoch_response);
        ASSERT_STATUS(status, "FinishEpoch", true);
        validate_finish_epoch_response(epoch_response, session_request.active_epoch_index(), 0);

        AdvanceStateRequest advance_request;
        init_valid_advance_state_request(advance_request, session_request.session_id(),
            session_request.active_epoch_index() + 1, 0);
        status = manager.advance_state(advance_request);
        ASSERT_STATUS(status, "AdvanceState", false);
        ASSERT_STATUS_CODE(status, "AdvanceState", StatusCode::OUT_OF_RANGE);

        // end session
        EndSessionRequest end_session_request;
        end_session_request.set_session_id(session_request.session_id());
        status = manager.end_session(end_session_request);
        ASSERT_STATUS(status, "EndSession", true);
    });

    test("Should fail to complete if the input index are not sequential", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request();
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        AdvanceStateRequest advance_request;
        init_valid_advance_state_request(advance_request, session_request.session_id(),
            session_request.active_epoch_index(), 0);
        status = manager.advance_state(advance_request);
        ASSERT_STATUS(status, "AdvanceState", true);

        // enqueue wrong input index
        advance_request.set_current_input_index(advance_request.current_input_index() + 10);
        status = manager.advance_state(advance_request);
        ASSERT_STATUS(status, "AdvanceState", false);
        ASSERT_STATUS_CODE(status, "AdvanceState", StatusCode::INVALID_ARGUMENT);

        end_session_after_processing_pending_inputs(manager, session_request.session_id(),
            session_request.active_epoch_index());
    });

    test("Should fail to complete input metadata is missing", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request();
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        // enqueue
        AdvanceStateRequest advance_request;
        init_valid_advance_state_request(advance_request, session_request.session_id(),
            session_request.active_epoch_index(), 0);
        advance_request.clear_input_metadata();
        status = manager.advance_state(advance_request);
        ASSERT_STATUS(status, "AdvanceState", false);
        ASSERT_STATUS_CODE(status, "AdvanceState", StatusCode::INVALID_ARGUMENT);

        // end session
        EndSessionRequest end_session_request;
        end_session_request.set_session_id(session_request.session_id());
        status = manager.end_session(end_session_request);
        ASSERT_STATUS(status, "EndSession", true);
    });

    test("Should fail to complete input metadata msg_sender is missing", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request();
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        // enqueue
        AdvanceStateRequest advance_request;
        init_valid_advance_state_request(advance_request, session_request.session_id(),
            session_request.active_epoch_index(), 0);
        advance_request.mutable_input_metadata()->clear_msg_sender();
        status = manager.advance_state(advance_request);
        ASSERT_STATUS(status, "AdvanceState", false);
        ASSERT_STATUS_CODE(status, "AdvanceState", StatusCode::INVALID_ARGUMENT);

        // end session
        EndSessionRequest end_session_request;
        end_session_request.set_session_id(session_request.session_id());
        status = manager.end_session(end_session_request);
        ASSERT_STATUS(status, "EndSession", true);
    });

    test("Should fail to complete input metadata msg_sender greater then 20 bytes", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request();
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        // enqueue
        AdvanceStateRequest advance_request;
        init_valid_advance_state_request(advance_request, session_request.session_id(),
            session_request.active_epoch_index(), 0);
        auto *msg_sender = advance_request.mutable_input_metadata()->mutable_msg_sender();
        msg_sender->mutable_data()->append("fafafa");
        status = manager.advance_state(advance_request);
        ASSERT_STATUS(status, "AdvanceState", false);
        ASSERT_STATUS_CODE(status, "AdvanceState", StatusCode::INVALID_ARGUMENT);

        // end session
        EndSessionRequest end_session_request;
        end_session_request.set_session_id(session_request.session_id());
        status = manager.end_session(end_session_request);
        ASSERT_STATUS(status, "EndSession", true);
    });

    test("Should fail to complete input metadata epoch index does not match active epoch index",
        [](ServerManagerClient &manager) {
            StartSessionRequest session_request = create_valid_start_session_request();
            StartSessionResponse session_response;
            Status status = manager.start_session(session_request, session_response);
            ASSERT_STATUS(status, "StartSession", true);

            // enqueue
            AdvanceStateRequest advance_request;
            init_valid_advance_state_request(advance_request, session_request.session_id(),
                session_request.active_epoch_index(), 0);
            advance_request.mutable_input_metadata()->set_epoch_index(session_request.active_epoch_index() + 1);
            status = manager.advance_state(advance_request);
            ASSERT_STATUS(status, "AdvanceState", false);
            ASSERT_STATUS_CODE(status, "AdvanceState", StatusCode::INVALID_ARGUMENT);

            // finish epoch
            FinishEpochRequest epoch_request;
            FinishEpochResponse epoch_response;
            init_valid_finish_epoch_request(epoch_request, session_request.session_id(),
                session_request.active_epoch_index(), 0);
            status = manager.finish_epoch(epoch_request, epoch_response);
            ASSERT_STATUS(status, "FinishEpoch", true);
            validate_finish_epoch_response(epoch_response, session_request.active_epoch_index(), 0);

            advance_request.mutable_input_metadata()->set_epoch_index(session_request.active_epoch_index());
            status = manager.advance_state(advance_request);
            ASSERT_STATUS(status, "AdvanceState", false);

            // end session
            EndSessionRequest end_session_request;
            end_session_request.set_session_id(session_request.session_id());
            status = manager.end_session(end_session_request);
            ASSERT_STATUS(status, "EndSession", true);
        });

    test("Should fail to complete input metadata input index does not match active epoch index",
        [](ServerManagerClient &manager) {
            StartSessionRequest session_request = create_valid_start_session_request();
            StartSessionResponse session_response;
            Status status = manager.start_session(session_request, session_response);
            ASSERT_STATUS(status, "StartSession", true);

            // enqueue
            AdvanceStateRequest advance_request;
            init_valid_advance_state_request(advance_request, session_request.session_id(),
                session_request.active_epoch_index(), 1);
            status = manager.advance_state(advance_request);
            ASSERT_STATUS(status, "AdvanceState", false);
            ASSERT_STATUS_CODE(status, "AdvanceState", StatusCode::INVALID_ARGUMENT);

            // finish epoch
            FinishEpochRequest epoch_request;
            FinishEpochResponse epoch_response;
            init_valid_finish_epoch_request(epoch_request, session_request.session_id(),
                session_request.active_epoch_index(), 0);
            status = manager.finish_epoch(epoch_request, epoch_response);
            ASSERT_STATUS(status, "FinishEpoch", true);
            validate_finish_epoch_response(epoch_response, session_request.active_epoch_index(), 0);

            advance_request.mutable_input_metadata()->set_epoch_index(session_request.active_epoch_index() + 1);
            status = manager.advance_state(advance_request);
            ASSERT_STATUS(status, "AdvanceState", false);

            // end session
            EndSessionRequest end_session_request;
            end_session_request.set_session_id(session_request.session_id());
            status = manager.end_session(end_session_request);
            ASSERT_STATUS(status, "EndSession", true);
        });
}

static void test_get_status(const std::function<void(const std::string &title, test_function f)> &test) {
    test("Should complete a valid request with success", [](ServerManagerClient &manager) {
        GetStatusResponse status_response;
        Status status = manager.get_status(status_response);
        ASSERT_STATUS(status, "GetStatus", true);
        ASSERT(status_response.session_id_size() == 0, "status response should be empty");
    });

    test("Should complete with success when there is one session", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request();
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        GetStatusResponse status_response;
        status = manager.get_status(status_response);
        ASSERT_STATUS(status, "GetStatus", true);

        ASSERT(status_response.session_id_size() == 1, "status response should have only one session");
        ASSERT(status_response.session_id()[0] == session_request.session_id(),
            "status response  first session_id should be the same as the one created");

        EndSessionRequest end_session_request;
        end_session_request.set_session_id(session_request.session_id());
        status = manager.end_session(end_session_request);
        ASSERT_STATUS(status, "EndSession", true);

        status = manager.get_status(status_response);
        ASSERT_STATUS(status, "GetStatus", true);
        ASSERT(status_response.session_id_size() == 0, "status response should have no sessions");
    });

    test("Should complete with success when there is two sessions", [](ServerManagerClient &manager) {
        // Create 1st session
        StartSessionRequest session_request = create_valid_start_session_request();
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        // Get status
        GetStatusResponse status_response;
        status = manager.get_status(status_response);
        ASSERT_STATUS(status, "GetStatus", true);
        ASSERT(status_response.session_id_size() == 1, "status response should have only one session");
        ASSERT(status_response.session_id()[0] == session_request.session_id(),
            "status response  first session_id should be the same as the first created");

        // Create 2nd session
        StartSessionRequest session_request2 = create_valid_start_session_request();
        StartSessionResponse session_response2;
        status = manager.start_session(session_request2, session_response2);
        ASSERT_STATUS(status, "StartSession", true);

        // Get status
        status = manager.get_status(status_response);
        ASSERT_STATUS(status, "GetStatus", true);
        ASSERT(status_response.session_id_size() == 2, "status response should have 2 sessions");

        // End 1st session
        EndSessionRequest end_session_request;
        end_session_request.set_session_id(session_request.session_id());
        status = manager.end_session(end_session_request);
        ASSERT_STATUS(status, "EndSession", true);

        // Get status
        status = manager.get_status(status_response);
        ASSERT_STATUS(status, "GetStatus", true);
        ASSERT(status_response.session_id_size() == 1, "status response should have 2 sessions");
        ASSERT(status_response.session_id()[0] == session_request2.session_id(),
            "status response  first session_id should be the same as the second created");

        // End 2nd session
        end_session_request.set_session_id(session_request2.session_id());
        status = manager.end_session(end_session_request);
        ASSERT_STATUS(status, "EndSession", true);

        // Get status
        status = manager.get_status(status_response);
        ASSERT_STATUS(status, "GetStatus", true);
        ASSERT(status_response.session_id_size() == 0, "status response should have no sessions");
    });
}

static void test_get_session_status(const std::function<void(const std::string &title, test_function f)> &test) {
    test("Should complete a valid request with success", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request();
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        GetSessionStatusRequest status_request;
        status_request.set_session_id(session_request.session_id());
        GetSessionStatusResponse status_response;
        status = manager.get_session_status(status_request, status_response);
        ASSERT_STATUS(status, "GetSessionStatus", true);

        ASSERT(status_response.session_id() == session_request.session_id(),
            "status response session_id should be the same as the one created");
        ASSERT(status_response.active_epoch_index() == session_request.active_epoch_index(),
            "status response active_epoch_index should be the same as the one created");
        ASSERT(status_response.epoch_index_size() == 1, "status response should no old epochs");
        ASSERT(!status_response.has_taint_status(), "status response should not be tainted");

        // end session
        EndSessionRequest end_session_request;
        end_session_request.set_session_id(session_request.session_id());
        status = manager.end_session(end_session_request);
        ASSERT_STATUS(status, "EndSession", true);
    });

    test("Should fail to complete with a invalid session id", [](ServerManagerClient &manager) {
        GetSessionStatusRequest status_request;
        status_request.set_session_id("NON-EXISTENT");
        GetSessionStatusResponse status_response;
        Status status = manager.get_session_status(status_request, status_response);
        ASSERT_STATUS(status, "GetSessionStatus", false);
        ASSERT_STATUS_CODE(status, "GetSessionStatus", StatusCode::INVALID_ARGUMENT);
    });

    test("Should report epoch index correctly after FinishEpoch", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request();
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        // GetSessionStatus
        GetSessionStatusRequest status_request;
        status_request.set_session_id(session_request.session_id());
        GetSessionStatusResponse status_response;
        status = manager.get_session_status(status_request, status_response);
        ASSERT_STATUS(status, "GetSessionStatus", true);

        ASSERT(status_response.session_id() == session_request.session_id(),
            "status response session_id should be the same as the one created");
        ASSERT(status_response.active_epoch_index() == session_request.active_epoch_index(),
            "status response active_epoch_index should be the same as the one created");
        ASSERT(status_response.epoch_index_size() == 1, "status response epoch_indices size should be 1");
        ASSERT(!status_response.has_taint_status(), "status response should not be tainted");

        // finish epoch
        FinishEpochRequest epoch_request;
        FinishEpochResponse epoch_response;
        init_valid_finish_epoch_request(epoch_request, session_request.session_id(),
            session_request.active_epoch_index(), 0);
        status = manager.finish_epoch(epoch_request, epoch_response);
        ASSERT_STATUS(status, "FinishEpoch", true);
        validate_finish_epoch_response(epoch_response, session_request.active_epoch_index(), 0);

        // GetSessionStatus
        status = manager.get_session_status(status_request, status_response);
        ASSERT_STATUS(status, "GetSessionStatus", true);

        ASSERT(status_response.session_id() == session_request.session_id(),
            "status response session_id should be the same as the one created");
        ASSERT(status_response.active_epoch_index() == session_request.active_epoch_index() + 1,
            "status response active_epoch_index should be 1");
        ASSERT(status_response.epoch_index_size() == 2, "status response epoch_indices size should be 2");
        ASSERT(!status_response.has_taint_status(), "status response should not be tainted");

        // finish epoch
        init_valid_finish_epoch_request(epoch_request, session_request.session_id(),
            session_request.active_epoch_index() + 1, 0);
        status = manager.finish_epoch(epoch_request, epoch_response);
        ASSERT_STATUS(status, "FinishEpoch", true);
        validate_finish_epoch_response(epoch_response, session_request.active_epoch_index() + 1, 0);

        // GetSessionStatus
        status = manager.get_session_status(status_request, status_response);
        ASSERT_STATUS(status, "GetSessionStatus", true);

        ASSERT(status_response.session_id() == session_request.session_id(),
            "status response session_id should be the same as the one created");
        ASSERT(status_response.active_epoch_index() == session_request.active_epoch_index() + 2,
            "status response active_epoch_index should be 2");
        ASSERT(status_response.epoch_index_size() == 3, "status response epoch_indices size should be 3");
        ASSERT(!status_response.has_taint_status(), "status response should not be tainted");

        // end session
        EndSessionRequest end_session_request;
        end_session_request.set_session_id(session_request.session_id());
        status = manager.end_session(end_session_request);
        ASSERT_STATUS(status, "EndSession", true);
    });

    test("Should complete with session taint_status code DEADLINE_EXCEEDED", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request("infinite-loop-machine");
        StartSessionResponse session_response;
        auto *server_deadline = session_request.mutable_server_deadline();
        server_deadline->set_advance_state_increment(1);
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        // enqueue
        AdvanceStateRequest advance_request;
        init_valid_advance_state_request(advance_request, session_request.session_id(),
            session_request.active_epoch_index(), 0);
        status = manager.advance_state(advance_request);
        ASSERT_STATUS(status, "AdvanceState", true);

        std::this_thread::sleep_for(10s);

        // GetSessionStatus
        GetSessionStatusRequest status_request;
        status_request.set_session_id(session_request.session_id());
        GetSessionStatusResponse status_response;
        status = manager.get_session_status(status_request, status_response);
        ASSERT_STATUS(status, "GetSessionStatus", true);

        ASSERT(status_response.session_id() == session_request.session_id(),
            "status response session_id should be the same as the one created");
        ASSERT(status_response.active_epoch_index() == session_request.active_epoch_index(),
            "status response active_epoch_index should be the same as the one created");
        ASSERT(status_response.epoch_index_size() == 1, "status response epoch_indices size should be 1");
        ASSERT(status_response.has_taint_status(), "status response should have a taint_status");
        ASSERT(status_response.taint_status().error_code() == StatusCode::DEADLINE_EXCEEDED,
            "taint_status code should be DEADLINE_EXCEEDED");

        end_session_after_processing_pending_inputs(manager, session_request.session_id(),
            session_request.active_epoch_index(), true);
    });
}

static void check_processed_input(ProcessedInput &processed_input, uint64_t index, int voucher_count, int notice_count,
    int report_count) {
    // processed_input
    ASSERT(processed_input.input_index() == index, "processed input index should sequential");
    ASSERT(processed_input.reports_size() == report_count,
        "processed input reports size should be equal to report_count");
    ASSERT(processed_input.status() == CompletionStatus::ACCEPTED, "processed input status should be ACCEPTED");
    ASSERT(processed_input.has_accepted_data(), "processed input should contain accepted data");

    const auto &result = processed_input.accepted_data();
    ASSERT(result.vouchers_size() == voucher_count, "result outputs size should be equal to output_count");
    ASSERT(result.notices_size() == notice_count, "result messages size should be equal to message_count");

    // reports
    for (const auto &report : processed_input.reports()) {
        ASSERT(!report.payload().empty(), "report payload should not be empty");
        ASSERT(report.payload() == get_report_payload(index), "report payload should match");
    }

    // vouchers
    for (const auto &voucher : result.vouchers()) {
        ASSERT(voucher.has_destination(), "voucher should have an address");
        ASSERT(voucher.destination().data() == get_voucher_address(index), "voucher address should match");
        ASSERT(!voucher.payload().empty(), "voucher payload should not be empty");
        ASSERT(voucher.payload() == get_voucher_payload(index), "voucher payload should match");
    }

    // notices
    for (const auto &notice : result.notices()) {
        ASSERT(!notice.payload().empty(), "notice payload should not be empty");
        ASSERT(notice.payload() == get_notice_payload(index), "notice payload should match");
    }
}

static void check_empty_epoch_status(const GetEpochStatusResponse &status_response, const std::string &session_id,
    uint64_t epoch_index, EpochState epoch_state, uint64_t pending_inputs) {
    ASSERT(status_response.session_id() == session_id,
        "status response session_id should be the same as the one created");
    ASSERT(status_response.epoch_index() == epoch_index,
        "status response epoch_index should be the same as the one created");
    ASSERT(status_response.state() == epoch_state, "status response state should be " + EpochState_Name(epoch_state));
    ASSERT(status_response.processed_inputs_size() == 0, "status response processed_inputs size should be 0");
    ASSERT(status_response.pending_input_count() == pending_inputs,
        "status response pending_input_count should be " + std::to_string(pending_inputs));
    ASSERT(!status_response.has_taint_status(), "status response should not be tainted");
}

static void test_get_epoch_status(const std::function<void(const std::string &title, test_function f)> &test) {
    test("Should complete a valid request with success", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request();
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        GetEpochStatusRequest status_request;
        status_request.set_session_id(session_request.session_id());
        status_request.set_epoch_index(session_request.active_epoch_index());
        GetEpochStatusResponse status_response;
        status = manager.get_epoch_status(status_request, status_response);
        ASSERT_STATUS(status, "GetEpochStatus", true);

        // assert status_resonse content
        check_empty_epoch_status(status_response, session_request.session_id(), session_request.active_epoch_index(),
            EpochState::ACTIVE, 0);

        // end session
        EndSessionRequest end_session_request;
        end_session_request.set_session_id(session_request.session_id());
        status = manager.end_session(end_session_request);
        ASSERT_STATUS(status, "EndSession", true);
    });

    test("Should fail to complete with a invalid session id", [](ServerManagerClient &manager) {
        GetEpochStatusRequest status_request;
        status_request.set_session_id("NON-EXISTENT");
        status_request.set_epoch_index(0);
        GetEpochStatusResponse status_response;
        Status status = manager.get_epoch_status(status_request, status_response);
        ASSERT_STATUS(status, "GetEpochStatus", false);
        ASSERT_STATUS_CODE(status, "GetEpochStatus", StatusCode::INVALID_ARGUMENT);
    });

    test("Should fail to complete with a ended session id", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request();
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        // end session
        EndSessionRequest end_session_request;
        end_session_request.set_session_id(session_request.session_id());
        status = manager.end_session(end_session_request);
        ASSERT_STATUS(status, "EndSession", true);

        // try to enqueue input on ended session
        GetEpochStatusRequest status_request;
        status_request.set_session_id(session_request.session_id());
        status_request.set_epoch_index(session_request.active_epoch_index());
        GetEpochStatusResponse status_response;
        status = manager.get_epoch_status(status_request, status_response);
        ASSERT_STATUS(status, "GetEpochStatus", false);
        ASSERT_STATUS_CODE(status, "GetEpochStatus", StatusCode::INVALID_ARGUMENT);
    });

    test("Should fail to complete if epoch index is not valid", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request();
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        GetEpochStatusRequest status_request;
        status_request.set_session_id(session_request.session_id());
        status_request.set_epoch_index(session_request.active_epoch_index() + 10);
        GetEpochStatusResponse status_response;
        status = manager.get_epoch_status(status_request, status_response);
        ASSERT_STATUS(status, "GetEpochStatus", false);
        ASSERT_STATUS_CODE(status, "GetEpochStatus", StatusCode::INVALID_ARGUMENT);

        // end session
        EndSessionRequest end_session_request;
        end_session_request.set_session_id(session_request.session_id());
        status = manager.end_session(end_session_request);
        ASSERT_STATUS(status, "EndSession", true);
    });

    test("Should complete with success with a valid session id and valid old epoch", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request();
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        // finish epoch
        FinishEpochRequest epoch_request;
        FinishEpochResponse epoch_response;
        init_valid_finish_epoch_request(epoch_request, session_request.session_id(),
            session_request.active_epoch_index(), 0);
        status = manager.finish_epoch(epoch_request, epoch_response);
        ASSERT_STATUS(status, "FinishEpoch", true);
        validate_finish_epoch_response(epoch_response, session_request.active_epoch_index(), 0);

        // status on old epoch
        GetEpochStatusRequest status_request;
        status_request.set_session_id(session_request.session_id());
        status_request.set_epoch_index(session_request.active_epoch_index());
        GetEpochStatusResponse status_response;
        status = manager.get_epoch_status(status_request, status_response);
        ASSERT_STATUS(status, "GetEpochStatus", true);

        // assert status_resonse content
        check_empty_epoch_status(status_response, session_request.session_id(), session_request.active_epoch_index(),
            EpochState::FINISHED, 0);

        // status on current epoch
        status_request.set_epoch_index(session_request.active_epoch_index() + 1);
        status = manager.get_epoch_status(status_request, status_response);
        ASSERT_STATUS(status, "GetEpochStatus", true);

        // assert status_resonse content
        check_empty_epoch_status(status_response, session_request.session_id(),
            session_request.active_epoch_index() + 1, EpochState::ACTIVE, 0);

        // end session
        EndSessionRequest end_session_request;
        end_session_request.set_session_id(session_request.session_id());
        status = manager.end_session(end_session_request);
        ASSERT_STATUS(status, "EndSession", true);
    });

    test("Should complete with pending input count equal 1 after AdvanceState", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request();
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        // enqueue
        AdvanceStateRequest advance_request;
        init_valid_advance_state_request(advance_request, session_request.session_id(),
            session_request.active_epoch_index(), 0);
        status = manager.advance_state(advance_request);
        ASSERT_STATUS(status, "AdvanceState", true);

        // get epoch status
        GetEpochStatusRequest status_request;
        status_request.set_session_id(session_request.session_id());
        status_request.set_epoch_index(session_request.active_epoch_index());
        GetEpochStatusResponse status_response;
        status = manager.get_epoch_status(status_request, status_response);
        ASSERT_STATUS(status, "GetEpochStatus", true);

        // assert status_resonse content
        check_empty_epoch_status(status_response, session_request.session_id(), session_request.active_epoch_index(),
            EpochState::ACTIVE, 1);

        end_session_after_processing_pending_inputs(manager, session_request.session_id(),
            session_request.active_epoch_index());
    });

    test("Should complete with processed input count equal 1 after processing enqueued input",
        [](ServerManagerClient &manager) {
            StartSessionRequest session_request = create_valid_start_session_request();
            StartSessionResponse session_response;
            Status status = manager.start_session(session_request, session_response);
            ASSERT_STATUS(status, "StartSession", true);

            // enqueue
            AdvanceStateRequest advance_request;
            init_valid_advance_state_request(advance_request, session_request.session_id(),
                session_request.active_epoch_index(), 0);
            status = manager.advance_state(advance_request);
            ASSERT_STATUS(status, "AdvanceState", true);

            // get epoch status after pending input is processed
            GetEpochStatusRequest status_request;
            status_request.set_session_id(session_request.session_id());
            status_request.set_epoch_index(session_request.active_epoch_index());
            GetEpochStatusResponse status_response;
            wait_pending_inputs_to_be_processed(manager, status_request, status_response, false,
                WAITING_PENDING_INPUT_MAX_RETRIES);

            // assert status_resonse content
            ASSERT(status_response.session_id() == session_request.session_id(),
                "status response session_id should be the same as the one created");
            ASSERT(status_response.epoch_index() == session_request.active_epoch_index(),
                "status response epoch_index should be 0");
            ASSERT(status_response.state() == EpochState::ACTIVE, "status response state should be ACTIVE");
            ASSERT(status_response.processed_inputs_size() == 1, "status response processed_inputs size should be 1");
            ASSERT(status_response.pending_input_count() == 0, "status response pending_input_count should 0");
            ASSERT(!status_response.has_taint_status(), "status response should not be tainted");

            auto processed_input = (status_response.processed_inputs())[0];
            check_processed_input(processed_input, 0, 2, 2, 2);

            // Finish epoch
            FinishEpochRequest epoch_request;
            FinishEpochResponse epoch_response;
            init_valid_finish_epoch_request(epoch_request, session_request.session_id(),
                session_request.active_epoch_index(), status_response.processed_inputs_size());
            status = manager.finish_epoch(epoch_request, epoch_response);
            ASSERT_STATUS(status, "FinishEpoch", true);
            validate_finish_epoch_response(epoch_response, session_request.active_epoch_index(), 1);

            // EndSession
            EndSessionRequest end_session_request;
            end_session_request.set_session_id(session_request.session_id());
            status = manager.end_session(end_session_request);
            ASSERT_STATUS(status, "EndSession", true);
        });

    test("Should complete with processed input count equal 1 after processing enqueued input on new epoch",
        [](ServerManagerClient &manager) {
            StartSessionRequest session_request = create_valid_start_session_request();
            StartSessionResponse session_response;
            Status status = manager.start_session(session_request, session_response);
            ASSERT_STATUS(status, "StartSession", true);

            // finish epoch
            FinishEpochRequest epoch_request;
            FinishEpochResponse epoch_response;
            init_valid_finish_epoch_request(epoch_request, session_request.session_id(),
                session_request.active_epoch_index(), 0);
            status = manager.finish_epoch(epoch_request, epoch_response);
            ASSERT_STATUS(status, "FinishEpoch", true);
            validate_finish_epoch_response(epoch_response, session_request.active_epoch_index(), 0);

            // enqueue
            AdvanceStateRequest advance_request;
            init_valid_advance_state_request(advance_request, session_request.session_id(),
                session_request.active_epoch_index() + 1, 0);
            status = manager.advance_state(advance_request);
            ASSERT_STATUS(status, "AdvanceState", true);

            // get epoch status after pending input is processed
            GetEpochStatusRequest status_request;
            status_request.set_session_id(session_request.session_id());
            status_request.set_epoch_index(session_request.active_epoch_index() + 1);
            GetEpochStatusResponse status_response;
            wait_pending_inputs_to_be_processed(manager, status_request, status_response, false,
                WAITING_PENDING_INPUT_MAX_RETRIES);

            // assert status_resonse content
            ASSERT(status_response.session_id() == session_request.session_id(),
                "status response session_id should be the same as the one created");
            ASSERT(status_response.epoch_index() == session_request.active_epoch_index() + 1,
                "status response epoch_index should be 0");
            ASSERT(status_response.state() == EpochState::ACTIVE, "status response state should be ACTIVE");
            ASSERT(status_response.processed_inputs_size() == 1, "status response processed_inputs size should be 1");
            ASSERT(status_response.pending_input_count() == 0, "status response pending_input_count should 0");
            ASSERT(!status_response.has_taint_status(), "status response should not be tainted");

            auto processed_input = (status_response.processed_inputs())[0];
            check_processed_input(processed_input, 0, 2, 2, 2);

            // Finish epoch
            init_valid_finish_epoch_request(epoch_request, session_request.session_id(),
                session_request.active_epoch_index() + 1, status_response.processed_inputs_size());
            status = manager.finish_epoch(epoch_request, epoch_response);
            ASSERT_STATUS(status, "FinishEpoch", true);

            // EndSession
            EndSessionRequest end_session_request;
            end_session_request.set_session_id(session_request.session_id());
            status = manager.end_session(end_session_request);
            ASSERT_STATUS(status, "EndSession", true);
        });

    test("Should complete with processed input count equal 1 after processing enqueued input (empty payload)",
        [](ServerManagerClient &manager) {
            StartSessionRequest session_request = create_valid_start_session_request("no-output-machine");
            StartSessionResponse session_response;
            Status status = manager.start_session(session_request, session_response);
            ASSERT_STATUS(status, "StartSession", true);

            // enqueue
            AdvanceStateRequest advance_request;
            init_valid_advance_state_request(advance_request, session_request.session_id(),
                session_request.active_epoch_index(), 0);
            advance_request.clear_input_payload();
            status = manager.advance_state(advance_request);
            ASSERT_STATUS(status, "AdvanceState", true);

            // get epoch status after pending input is processed
            GetEpochStatusRequest status_request;
            status_request.set_session_id(session_request.session_id());
            status_request.set_epoch_index(session_request.active_epoch_index());
            GetEpochStatusResponse status_response;
            wait_pending_inputs_to_be_processed(manager, status_request, status_response, false,
                WAITING_PENDING_INPUT_MAX_RETRIES);

            // assert status_resonse content
            ASSERT(status_response.session_id() == session_request.session_id(),
                "status response session_id should be the same as the one created");
            ASSERT(status_response.epoch_index() == session_request.active_epoch_index(),
                "status response epoch_index should be 0");
            ASSERT(status_response.state() == EpochState::ACTIVE, "status response state should be ACTIVE");
            ASSERT(status_response.processed_inputs_size() == 1, "status response processed_inputs size should be 1");
            ASSERT(status_response.pending_input_count() == 0, "status response pending_input_count should 0");
            ASSERT(!status_response.has_taint_status(), "status response should not be tainted");

            auto processed_input = (status_response.processed_inputs())[0];
            check_processed_input(processed_input, 0, 0, 0, 0);

            // Finish epoch
            FinishEpochRequest epoch_request;
            FinishEpochResponse epoch_response;
            init_valid_finish_epoch_request(epoch_request, session_request.session_id(),
                session_request.active_epoch_index(), status_response.processed_inputs_size());
            status = manager.finish_epoch(epoch_request, epoch_response);
            ASSERT_STATUS(status, "FinishEpoch", true);
            validate_finish_epoch_response(epoch_response, session_request.active_epoch_index(), 1);

            // EndSession
            EndSessionRequest end_session_request;
            end_session_request.set_session_id(session_request.session_id());
            status = manager.end_session(end_session_request);
            ASSERT_STATUS(status, "EndSession", true);
        });

    test("Should fail to complete an taint the session when manual yield reason is TX-EXCEPTION",
        [](ServerManagerClient &manager) {
            StartSessionRequest session_request = create_valid_start_session_request("exception-machine");
            StartSessionResponse session_response;
            Status status = manager.start_session(session_request, session_response);
            ASSERT_STATUS(status, "StartSession", true);

            // enqueue
            AdvanceStateRequest advance_request;
            init_valid_advance_state_request(advance_request, session_request.session_id(),
                session_request.active_epoch_index(), 0);
            status = manager.advance_state(advance_request);
            ASSERT_STATUS(status, "AdvanceState", true);

            // get epoch status after pending input is processed
            GetEpochStatusRequest status_request;
            status_request.set_session_id(session_request.session_id());
            status_request.set_epoch_index(session_request.active_epoch_index());
            GetEpochStatusResponse status_response;
            wait_pending_inputs_to_be_processed(manager, status_request, status_response, true,
                WAITING_PENDING_INPUT_MAX_RETRIES);

            // assert status_resonse content
            ASSERT(status_response.session_id() == session_request.session_id(),
                "status response session_id should be the same as the one created");
            ASSERT(status_response.epoch_index() == session_request.active_epoch_index(),
                "status response epoch_index should be 0");
            ASSERT(status_response.state() == EpochState::ACTIVE, "status response state should be ACTIVE");
            ASSERT(status_response.processed_inputs_size() == 1, "status response processed_inputs size should be 1");
            ASSERT(status_response.pending_input_count() == 0, "status response pending_input_count should 0");
            ASSERT(!status_response.has_taint_status(), "status response should not be tainted");

            auto processed_input = (status_response.processed_inputs())[0];
            ASSERT(processed_input.input_index() == 0, "processed input index should sequential");
            ASSERT(processed_input.reports_size() == 0, "processed input reports size should be equal to report_count");
            ASSERT(processed_input.status() == CompletionStatus::EXCEPTION,
                "processed input status should be EXCEPTION");
            ASSERT(processed_input.has_exception_data(), "processed input should contain exception data");
            ASSERT(processed_input.exception_data() == "test payload",
                "exception data should contain the expected payload");

            end_session_after_processing_pending_inputs(manager, session_request.session_id(),
                session_request.active_epoch_index(), false, true);
        });

    test("Should complete with CompletionStatus EXCEPTION after fatal error", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request("fatal-error-machine");
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        // enqueue
        AdvanceStateRequest advance_request;
        init_valid_advance_state_request(advance_request, session_request.session_id(),
            session_request.active_epoch_index(), 0);
        status = manager.advance_state(advance_request);
        ASSERT_STATUS(status, "AdvanceState", true);

        // get epoch status
        GetEpochStatusRequest status_request;
        status_request.set_session_id(session_request.session_id());
        status_request.set_epoch_index(session_request.active_epoch_index());
        GetEpochStatusResponse status_response;
        wait_pending_inputs_to_be_processed(manager, status_request, status_response, false,
            WAITING_PENDING_INPUT_MAX_RETRIES);

        // assert status_resonse content
        ASSERT(status_response.session_id() == session_request.session_id(),
            "status response session_id should be the same as the one created");
        ASSERT(status_response.epoch_index() == session_request.active_epoch_index(),
            "status response epoch_index should be 0");
        ASSERT(status_response.state() == EpochState::ACTIVE, "status response state should be ACTIVE");
        ASSERT(status_response.processed_inputs_size() == 1, "status response processed_inputs size should be 1");
        ASSERT(status_response.pending_input_count() == 0, "status response pending_input_count should 0");
        ASSERT(!status_response.has_taint_status(), "status response should not be tainted");

        auto processed_input = (status_response.processed_inputs())[0];
        ASSERT(processed_input.input_index() == 0, "processed_input input index should be 0");
        ASSERT(processed_input.reports_size() == 0, "processed input reports size should be equal to report_count");
        ASSERT(processed_input.status() == CompletionStatus::EXCEPTION, "processed input status should be EXCEPTION");
        ASSERT(processed_input.has_exception_data(), "processed input should contain exception data");
        ASSERT(processed_input.exception_data() == "dapp exited with exit status: 2",
            "exception data should contain the expected payload");

        end_session_after_processing_pending_inputs(manager, session_request.session_id(),
            session_request.active_epoch_index(), false, true);
    });

    test("Should complete with CompletionStatus EXCEPTION after rollup-http-server error",
        [](ServerManagerClient &manager) {
            StartSessionRequest session_request = create_valid_start_session_request("http-server-error-machine");
            StartSessionResponse session_response;
            Status status = manager.start_session(session_request, session_response);
            ASSERT_STATUS(status, "StartSession", true);

            // enqueue
            AdvanceStateRequest advance_request;
            init_valid_advance_state_request(advance_request, session_request.session_id(),
                session_request.active_epoch_index(), 0);
            status = manager.advance_state(advance_request);
            ASSERT_STATUS(status, "AdvanceState", true);

            // get epoch status
            GetEpochStatusRequest status_request;
            status_request.set_session_id(session_request.session_id());
            status_request.set_epoch_index(session_request.active_epoch_index());
            GetEpochStatusResponse status_response;
            wait_pending_inputs_to_be_processed(manager, status_request, status_response, false,
                WAITING_PENDING_INPUT_MAX_RETRIES);

            // assert status_resonse content
            ASSERT(status_response.session_id() == session_request.session_id(),
                "status response session_id should be the same as the one created");
            ASSERT(status_response.epoch_index() == session_request.active_epoch_index(),
                "status response epoch_index should be 0");
            ASSERT(status_response.state() == EpochState::ACTIVE, "status response state should be ACTIVE");
            ASSERT(status_response.processed_inputs_size() == 1, "status response processed_inputs size should be 1");
            ASSERT(status_response.pending_input_count() == 0, "status response pending_input_count should 0");
            ASSERT(!status_response.has_taint_status(), "status response should not be tainted");

            auto processed_input = (status_response.processed_inputs())[0];
            ASSERT(processed_input.input_index() == 0, "processed_input input index should be 0");
            ASSERT(processed_input.reports_size() == 0, "processed input reports size should be equal to report_count");
            ASSERT(processed_input.status() == CompletionStatus::EXCEPTION,
                "processed input status should be EXCEPTION");
            ASSERT(processed_input.has_exception_data(), "processed input should contain exception data");
            ASSERT(processed_input.exception_data() == "rollup-http-server exited with 0 status",
                "exception data should contain the expected payload");

            end_session_after_processing_pending_inputs(manager, session_request.session_id(),
                session_request.active_epoch_index(), false, true);
        });

    test("Should complete with first processed input as CompletionStatus PAYLOAD_LENGTH_LIMIT_EXCEEDED",
        [](ServerManagerClient &manager) {
            StartSessionRequest session_request = create_valid_start_session_request();
            StartSessionResponse session_response;
            CyclesConfig *server_cycles = session_request.mutable_server_cycles();
            server_cycles->set_max_advance_state(2);
            server_cycles->set_advance_state_increment(2);
            Status status = manager.start_session(session_request, session_response);
            ASSERT_STATUS(status, "StartSession", true);

            // enqueue
            AdvanceStateRequest advance_request;
            init_valid_advance_state_request(advance_request, session_request.session_id(),
                session_request.active_epoch_index(), 0);
            auto *input_payload = advance_request.mutable_input_payload();
            input_payload->resize(session_response.config().rollup().rx_buffer().length() + 1, 'x');
            status = manager.advance_state(advance_request);
            ASSERT_STATUS(status, "AdvanceState", true);

            std::this_thread::sleep_for(5s);

            // get epoch status
            GetEpochStatusRequest status_request;
            status_request.set_session_id(session_request.session_id());
            status_request.set_epoch_index(session_request.active_epoch_index());
            GetEpochStatusResponse status_response;
            status = manager.get_epoch_status(status_request, status_response);
            ASSERT_STATUS(status, "GetEpochStatus", true);

            // assert status_resonse content
            ASSERT(status_response.session_id() == session_request.session_id(),
                "status response session_id should be the same as the one created");
            ASSERT(status_response.epoch_index() == session_request.active_epoch_index(),
                "status response epoch_index should be 0");
            ASSERT(status_response.state() == EpochState::ACTIVE, "status response state should be ACTIVE");
            ASSERT(status_response.processed_inputs_size() == 1, "status response processed_inputs size should be 1");
            ASSERT(status_response.pending_input_count() == 0, "status response pending_input_count should 0");
            ASSERT(!status_response.has_taint_status(), "status response should not be tainted");

            auto processed_input = (status_response.processed_inputs())[0];
            ASSERT(processed_input.input_index() == 0, "processed_input input index should be 0");
            ASSERT(processed_input.status() == CompletionStatus::PAYLOAD_LENGTH_LIMIT_EXCEEDED,
                "CompletionStatus should be PAYLOAD_LENGTH_LIMIT_EXCEEDED");
            ASSERT(processed_input.ProcessedInputOneOf_case() == ProcessedInput::PROCESSEDINPUTONEOF_NOT_SET,
                "ProcessedInputOneOf should not be set");

            end_session_after_processing_pending_inputs(manager, session_request.session_id(),
                session_request.active_epoch_index(), false, true);
        });

    test("Should complete with first processed input as CompletionStatus CYCLE_LIMIT_EXCEEDED",
        [](ServerManagerClient &manager) {
            StartSessionRequest session_request = create_valid_start_session_request();
            StartSessionResponse session_response;
            CyclesConfig *server_cycles = session_request.mutable_server_cycles();
            server_cycles->set_max_advance_state(2);
            server_cycles->set_advance_state_increment(2);
            Status status = manager.start_session(session_request, session_response);
            ASSERT_STATUS(status, "StartSession", true);

            // enqueue
            AdvanceStateRequest advance_request;
            init_valid_advance_state_request(advance_request, session_request.session_id(),
                session_request.active_epoch_index(), 0);
            status = manager.advance_state(advance_request);
            ASSERT_STATUS(status, "AdvanceState", true);

            std::this_thread::sleep_for(5s);

            // get epoch status
            GetEpochStatusRequest status_request;
            status_request.set_session_id(session_request.session_id());
            status_request.set_epoch_index(session_request.active_epoch_index());
            GetEpochStatusResponse status_response;
            status = manager.get_epoch_status(status_request, status_response);
            ASSERT_STATUS(status, "GetEpochStatus", true);

            // assert status_resonse content
            ASSERT(status_response.session_id() == session_request.session_id(),
                "status response session_id should be the same as the one created");
            ASSERT(status_response.epoch_index() == session_request.active_epoch_index(),
                "status response epoch_index should be 0");
            ASSERT(status_response.state() == EpochState::ACTIVE, "status response state should be ACTIVE");
            ASSERT(status_response.processed_inputs_size() == 1, "status response processed_inputs size should be 1");
            ASSERT(status_response.pending_input_count() == 0, "status response pending_input_count should 0");
            ASSERT(!status_response.has_taint_status(), "status response should not be tainted");

            auto processed_input = (status_response.processed_inputs())[0];
            ASSERT(processed_input.input_index() == 0, "processed_input input index should be 0");
            ASSERT(processed_input.status() == CompletionStatus::CYCLE_LIMIT_EXCEEDED,
                "CompletionStatus should be CYCLE_LIMIT_EXCEEDED");

            end_session_after_processing_pending_inputs(manager, session_request.session_id(),
                session_request.active_epoch_index(), false, true);
        });

    test("Should complete with first processed input as CompletionStatus TIME_LIMIT_EXCEEDED",
        [](ServerManagerClient &manager) {
            StartSessionRequest session_request = create_valid_start_session_request();
            StartSessionResponse session_response;
            CyclesConfig *server_cycles = session_request.mutable_server_cycles();
            server_cycles->set_advance_state_increment(10);
            auto *server_deadline = session_request.mutable_server_deadline();
            server_deadline->set_advance_state(1000);
            server_deadline->set_advance_state_increment(1000);
            Status status = manager.start_session(session_request, session_response);
            ASSERT_STATUS(status, "StartSession", true);

            // enqueue
            AdvanceStateRequest advance_request;
            init_valid_advance_state_request(advance_request, session_request.session_id(),
                session_request.active_epoch_index(), 0);
            status = manager.advance_state(advance_request);
            ASSERT_STATUS(status, "AdvanceState", true);

            std::this_thread::sleep_for(10s);

            // get epoch status
            GetEpochStatusRequest status_request;
            status_request.set_session_id(session_request.session_id());
            status_request.set_epoch_index(session_request.active_epoch_index());
            GetEpochStatusResponse status_response;
            status = manager.get_epoch_status(status_request, status_response);
            ASSERT_STATUS(status, "GetEpochStatus", true);

            // assert status_resonse content
            ASSERT(status_response.session_id() == session_request.session_id(),
                "status response session_id should be the same as the one created");
            ASSERT(status_response.epoch_index() == session_request.active_epoch_index(),
                "status response epoch_index should be 0");
            ASSERT(status_response.state() == EpochState::ACTIVE, "status response state should be ACTIVE");
            ASSERT(status_response.processed_inputs_size() == 1, "status response processed_inputs size should be 1");
            ASSERT(status_response.pending_input_count() == 0, "status response pending_input_count should 0");
            ASSERT(!status_response.has_taint_status(), "status response should not be tainted");

            auto processed_input = (status_response.processed_inputs())[0];
            ASSERT(processed_input.input_index() == 0, "processed_input input index should be 0");
            ASSERT(processed_input.status() == CompletionStatus::TIME_LIMIT_EXCEEDED,
                "CompletionStatus should be TIME_LIMIT_EXCEEDED");

            end_session_after_processing_pending_inputs(manager, session_request.session_id(),
                session_request.active_epoch_index(), false, true);
        });

    test("Should complete with session taint_status code DEADLINE_EXCEEDED", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request("infinite-loop-machine");
        StartSessionResponse session_response;
        auto *server_deadline = session_request.mutable_server_deadline();
        server_deadline->set_advance_state_increment(1);
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        // enqueue
        AdvanceStateRequest advance_request;
        init_valid_advance_state_request(advance_request, session_request.session_id(),
            session_request.active_epoch_index(), 0);
        status = manager.advance_state(advance_request);
        ASSERT_STATUS(status, "AdvanceState", true);

        std::this_thread::sleep_for(10s);

        // get epoch status
        GetEpochStatusRequest status_request;
        status_request.set_session_id(session_request.session_id());
        status_request.set_epoch_index(session_request.active_epoch_index());
        GetEpochStatusResponse status_response;
        status = manager.get_epoch_status(status_request, status_response);
        ASSERT_STATUS(status, "GetEpochStatus", true);

        // assert status_resonse content
        ASSERT(status_response.session_id() == session_request.session_id(),
            "status response session_id should be the same as the one created");
        ASSERT(status_response.epoch_index() == session_request.active_epoch_index(),
            "status response epoch_index should be 0");
        ASSERT(status_response.state() == EpochState::ACTIVE, "status response state should be ACTIVE");
        ASSERT(status_response.processed_inputs_size() == 0, "status response processed_inputs size should be 0");
        ASSERT(status_response.pending_input_count() == 1, "status response pending_input_count should 1");
        ASSERT(status_response.has_taint_status(), "status response should have a taint_status");
        ASSERT(status_response.taint_status().error_code() == StatusCode::DEADLINE_EXCEEDED,
            "taint_status code should be DEADLINE_EXCEEDED");

        end_session_after_processing_pending_inputs(manager, session_request.session_id(),
            session_request.active_epoch_index(), true);
    });

    test("Should complete with first processed input as CompletionStatus REJECTED_BY_MACHINE",
        [](ServerManagerClient &manager) {
            StartSessionRequest session_request = create_valid_start_session_request("advance-rejecting-machine");
            StartSessionResponse session_response;
            Status status = manager.start_session(session_request, session_response);
            ASSERT_STATUS(status, "StartSession", true);

            // enqueue
            AdvanceStateRequest advance_request;
            init_valid_advance_state_request(advance_request, session_request.session_id(),
                session_request.active_epoch_index(), 0);
            status = manager.advance_state(advance_request);
            ASSERT_STATUS(status, "AdvanceState", true);

            std::this_thread::sleep_for(10s);

            // get epoch status
            GetEpochStatusRequest status_request;
            status_request.set_session_id(session_request.session_id());
            status_request.set_epoch_index(session_request.active_epoch_index());
            GetEpochStatusResponse status_response;
            status = manager.get_epoch_status(status_request, status_response);
            ASSERT_STATUS(status, "GetEpochStatus", true);

            // assert status_resonse content
            ASSERT(status_response.session_id() == session_request.session_id(),
                "status response session_id should be the same as the one created");
            ASSERT(status_response.epoch_index() == session_request.active_epoch_index(),
                "status response epoch_index should be 0");
            ASSERT(status_response.state() == EpochState::ACTIVE, "status response state should be ACTIVE");
            ASSERT(status_response.processed_inputs_size() == 1, "status response processed_inputs size should be 1");
            ASSERT(status_response.pending_input_count() == 0, "status response pending_input_count should 0");
            ASSERT(!status_response.has_taint_status(), "status response should not be tainted");

            auto processed_input = (status_response.processed_inputs())[0];
            ASSERT(processed_input.input_index() == 0, "processed_input input index should be 0");
            ASSERT(processed_input.status() == CompletionStatus::REJECTED, "CompletionStatus should be REJECTED");

            end_session_after_processing_pending_inputs(manager, session_request.session_id(),
                session_request.active_epoch_index(), false, true);
        });

    test("Should complete with first processed input as CompletionStatus MACHINE_HALTED",
        [](ServerManagerClient &manager) {
            StartSessionRequest session_request = create_valid_start_session_request("halting-machine");
            StartSessionResponse session_response;
            Status status = manager.start_session(session_request, session_response);
            ASSERT_STATUS(status, "StartSession", true);

            // enqueue
            AdvanceStateRequest advance_request;
            init_valid_advance_state_request(advance_request, session_request.session_id(),
                session_request.active_epoch_index(), 0);
            status = manager.advance_state(advance_request);
            ASSERT_STATUS(status, "AdvanceState", true);

            std::this_thread::sleep_for(10s);

            // get epoch status
            GetEpochStatusRequest status_request;
            status_request.set_session_id(session_request.session_id());
            status_request.set_epoch_index(session_request.active_epoch_index());
            GetEpochStatusResponse status_response;
            status = manager.get_epoch_status(status_request, status_response);
            ASSERT_STATUS(status, "GetEpochStatus", true);

            // assert status_resonse content
            ASSERT(status_response.session_id() == session_request.session_id(),
                "status response session_id should be the same as the one created");
            ASSERT(status_response.epoch_index() == session_request.active_epoch_index(),
                "status response epoch_index should be 0");
            ASSERT(status_response.state() == EpochState::ACTIVE, "status response state should be ACTIVE");
            ASSERT(status_response.processed_inputs_size() == 1, "status response processed_inputs size should be 1");
            ASSERT(status_response.pending_input_count() == 0, "status response pending_input_count should 0");
            ASSERT(!status_response.has_taint_status(), "status response should not be tainted");

            auto processed_input = (status_response.processed_inputs())[0];
            ASSERT(processed_input.input_index() == 0, "processed_input input index should be 0");
            ASSERT(processed_input.status() == CompletionStatus::MACHINE_HALTED,
                "CompletionStatus should be MACHINE_HALTED");

            end_session_after_processing_pending_inputs(manager, session_request.session_id(),
                session_request.active_epoch_index(), false, true);
        });

    test("Should return valid InputResults after request completed with success", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request();
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        // enqueue
        AdvanceStateRequest advance_request;
        init_valid_advance_state_request(advance_request, session_request.session_id(),
            session_request.active_epoch_index(), 0);
        status = manager.advance_state(advance_request);
        ASSERT_STATUS(status, "AdvanceState", true);

        GetEpochStatusRequest status_request;
        status_request.set_session_id(session_request.session_id());
        status_request.set_epoch_index(session_request.active_epoch_index());
        GetEpochStatusResponse status_response;
        wait_pending_inputs_to_be_processed(manager, status_request, status_response, false,
            WAITING_PENDING_INPUT_MAX_RETRIES);

        // assert status_response content
        ASSERT(status_response.session_id() == session_request.session_id(),
            "status response session_id should be the same as the one created");
        ASSERT(status_response.epoch_index() == session_request.active_epoch_index(),
            "status response epoch_index should be the same as the one created");
        ASSERT(status_response.state() == EpochState::ACTIVE, "status response state should be ACTIVE");
        ASSERT(status_response.processed_inputs_size() == 1, "status response processed_inputs size should be 1");
        ASSERT(status_response.pending_input_count() == 0, "status response pending_input_count should 0");
        ASSERT(!status_response.has_taint_status(), "status response should not be tainted");

        // processed_input
        auto processed_input = (status_response.processed_inputs())[0];
        check_processed_input(processed_input, 0, 2, 2, 2);

        // end session
        end_session_after_processing_pending_inputs(manager, session_request.session_id(),
            session_request.active_epoch_index());
    });

    test("Should return valid InputResults even when there is no outputs or messages",
        [](ServerManagerClient &manager) {
            StartSessionRequest session_request = create_valid_start_session_request("no-output-machine");
            StartSessionResponse session_response;
            Status status = manager.start_session(session_request, session_response);
            ASSERT_STATUS(status, "StartSession", true);

            // enqueue
            AdvanceStateRequest advance_request;
            init_valid_advance_state_request(advance_request, session_request.session_id(),
                session_request.active_epoch_index(), 0);
            status = manager.advance_state(advance_request);
            ASSERT_STATUS(status, "AdvanceState", true);

            GetEpochStatusRequest status_request;
            status_request.set_session_id(session_request.session_id());
            status_request.set_epoch_index(session_request.active_epoch_index());
            GetEpochStatusResponse status_response;
            wait_pending_inputs_to_be_processed(manager, status_request, status_response, false,
                WAITING_PENDING_INPUT_MAX_RETRIES);

            // assert status_response content
            ASSERT(status_response.session_id() == session_request.session_id(),
                "status response session_id should be the same as the one created");
            ASSERT(status_response.epoch_index() == session_request.active_epoch_index(),
                "status response epoch_index should be the same as the one created");
            ASSERT(status_response.state() == EpochState::ACTIVE, "status response state should be ACTIVE");
            ASSERT(status_response.processed_inputs_size() == 1, "status response processed_inputs size should be 1");
            ASSERT(status_response.pending_input_count() == 0, "status response pending_input_count should 0");
            ASSERT(!status_response.has_taint_status(), "status response should not be tainted");

            // processed_input
            auto processed_input = (status_response.processed_inputs())[0];
            check_processed_input(processed_input, 0, 0, 0, 0);

            // end session
            end_session_after_processing_pending_inputs(manager, session_request.session_id(),
                session_request.active_epoch_index());
        });

    test("Should complete with success returning one voucher and no notices or reports",
        [](ServerManagerClient &manager) {
            StartSessionRequest session_request = create_valid_start_session_request("one-voucher-machine");
            StartSessionResponse session_response;
            Status status = manager.start_session(session_request, session_response);
            ASSERT_STATUS(status, "StartSession", true);

            // enqueue
            AdvanceStateRequest advance_request;
            init_valid_advance_state_request(advance_request, session_request.session_id(),
                session_request.active_epoch_index(), 0);
            status = manager.advance_state(advance_request);
            ASSERT_STATUS(status, "AdvanceState", true);

            GetEpochStatusRequest status_request;
            status_request.set_session_id(session_request.session_id());
            status_request.set_epoch_index(session_request.active_epoch_index());
            GetEpochStatusResponse status_response;
            wait_pending_inputs_to_be_processed(manager, status_request, status_response, false,
                WAITING_PENDING_INPUT_MAX_RETRIES);

            // assert status_response content
            ASSERT(status_response.session_id() == session_request.session_id(),
                "status response session_id should be the same as the one created");
            ASSERT(status_response.epoch_index() == session_request.active_epoch_index(),
                "status response epoch_index should be the same as the one created");
            ASSERT(status_response.state() == EpochState::ACTIVE, "status response state should be ACTIVE");
            ASSERT(status_response.processed_inputs_size() == 1, "status response processed_inputs size should be 1");
            ASSERT(status_response.pending_input_count() == 0, "status response pending_input_count should 0");
            ASSERT(!status_response.has_taint_status(), "status response should not be tainted");

            // processed_input
            auto processed_input = (status_response.processed_inputs())[0];
            check_processed_input(processed_input, 0, 1, 0, 0);

            // end session
            end_session_after_processing_pending_inputs(manager, session_request.session_id(),
                session_request.active_epoch_index());
        });

    test("Should complete with success returning one notice and no vouchers or reports",
        [](ServerManagerClient &manager) {
            StartSessionRequest session_request = create_valid_start_session_request("one-notice-machine");
            StartSessionResponse session_response;
            Status status = manager.start_session(session_request, session_response);
            ASSERT_STATUS(status, "StartSession", true);

            // enqueue
            AdvanceStateRequest advance_request;
            init_valid_advance_state_request(advance_request, session_request.session_id(),
                session_request.active_epoch_index(), 0);
            status = manager.advance_state(advance_request);
            ASSERT_STATUS(status, "AdvanceState", true);

            GetEpochStatusRequest status_request;
            status_request.set_session_id(session_request.session_id());
            status_request.set_epoch_index(session_request.active_epoch_index());
            GetEpochStatusResponse status_response;
            wait_pending_inputs_to_be_processed(manager, status_request, status_response, false,
                WAITING_PENDING_INPUT_MAX_RETRIES);

            // assert status_response content
            ASSERT(status_response.session_id() == session_request.session_id(),
                "status response session_id should be the same as the one created");
            ASSERT(status_response.epoch_index() == session_request.active_epoch_index(),
                "status response epoch_index should be the same as the one created");
            ASSERT(status_response.state() == EpochState::ACTIVE, "status response state should be ACTIVE");
            ASSERT(status_response.processed_inputs_size() == 1, "status response processed_inputs size should be 1");
            ASSERT(status_response.pending_input_count() == 0, "status response pending_input_count should 0");
            ASSERT(!status_response.has_taint_status(), "status response should not be tainted");

            // processed_input
            auto processed_input = (status_response.processed_inputs())[0];
            check_processed_input(processed_input, 0, 0, 1, 0);

            // end session
            end_session_after_processing_pending_inputs(manager, session_request.session_id(),
                session_request.active_epoch_index());
        });

    test("Should complete with success returning one report and no notices or vouchers",
        [](ServerManagerClient &manager) {
            StartSessionRequest session_request = create_valid_start_session_request("one-report-machine");
            StartSessionResponse session_response;
            Status status = manager.start_session(session_request, session_response);
            ASSERT_STATUS(status, "StartSession", true);

            // enqueue
            AdvanceStateRequest advance_request;
            init_valid_advance_state_request(advance_request, session_request.session_id(),
                session_request.active_epoch_index(), 0);
            status = manager.advance_state(advance_request);
            ASSERT_STATUS(status, "AdvanceState", true);

            GetEpochStatusRequest status_request;
            status_request.set_session_id(session_request.session_id());
            status_request.set_epoch_index(session_request.active_epoch_index());
            GetEpochStatusResponse status_response;
            wait_pending_inputs_to_be_processed(manager, status_request, status_response, false,
                WAITING_PENDING_INPUT_MAX_RETRIES);

            // assert status_response content
            ASSERT(status_response.session_id() == session_request.session_id(),
                "status response session_id should be the same as the one created");
            ASSERT(status_response.epoch_index() == session_request.active_epoch_index(),
                "status response epoch_index should be the same as the one created");
            ASSERT(status_response.state() == EpochState::ACTIVE, "status response state should be ACTIVE");
            ASSERT(status_response.processed_inputs_size() == 1, "status response processed_inputs size should be 1");
            ASSERT(status_response.pending_input_count() == 0, "status response pending_input_count should 0");
            ASSERT(!status_response.has_taint_status(), "status response should not be tainted");

            // processed_input
            auto processed_input = (status_response.processed_inputs())[0];
            check_processed_input(processed_input, 0, 0, 0, 1);

            // end session
            end_session_after_processing_pending_inputs(manager, session_request.session_id(),
                session_request.active_epoch_index());
        });
}

static void check_inspect_state_response(InspectStateResponse &response, const std::string &session_id, uint64_t epoch,
    uint64_t input, int report_count, CompletionStatus status = CompletionStatus::ACCEPTED) {
    ASSERT(response.session_id() == session_id, "response session id should match");
    ASSERT(response.active_epoch_index() == epoch, "response epoch should match");
    ASSERT(response.status() == status, "response status should match");
    ASSERT(response.reports_size() == report_count, "response reports size should be equal to report_count");
    for (const auto &report : response.reports()) {
        ASSERT(!report.payload().empty(), "report payload should not be empty");
        ASSERT(report.payload() == get_report_payload(input), "report payload should match");
    }
}

static void test_inspect_state(const std::function<void(const std::string &title, test_function f)> &test) {
    test("Should complete a valid request with success", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request("inspect-state-machine");
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        InspectStateRequest inspect_request;
        init_valid_inspect_state_request(inspect_request, session_request.session_id(), 0);
        InspectStateResponse inspect_response;
        status = manager.inspect_state(inspect_request, inspect_response);
        ASSERT_STATUS(status, "InspectState", true);

        check_inspect_state_response(inspect_response, inspect_request.session_id(),
            session_request.active_epoch_index(), 0, 2);

        // end session
        EndSessionRequest end_session_request;
        end_session_request.set_session_id(session_request.session_id());
        status = manager.end_session(end_session_request);
        ASSERT_STATUS(status, "EndSession", true);
    });

    test("Should complete two valid requests with success", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request("inspect-state-machine");
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        // enqueue first
        InspectStateRequest inspect_request;
        init_valid_inspect_state_request(inspect_request, session_request.session_id(), 0);
        InspectStateResponse inspect_response;
        status = manager.inspect_state(inspect_request, inspect_response);
        ASSERT_STATUS(status, "InspectState", true);

        check_inspect_state_response(inspect_response, inspect_request.session_id(),
            session_request.active_epoch_index(), 0, 2);

        // enqueue second
        status = manager.inspect_state(inspect_request, inspect_response);
        ASSERT_STATUS(status, "InspectState", true);

        check_inspect_state_response(inspect_response, inspect_request.session_id(),
            session_request.active_epoch_index(), 0, 2);

        // end session
        EndSessionRequest end_session_request;
        end_session_request.set_session_id(session_request.session_id());
        status = manager.end_session(end_session_request);
        ASSERT_STATUS(status, "EndSession", true);
    });

    test("Should complete a valid request with success (empty payload)", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request("no-output-machine");
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        InspectStateRequest inspect_request;
        init_valid_inspect_state_request(inspect_request, session_request.session_id(), 0);
        inspect_request.clear_query_payload();
        InspectStateResponse inspect_response;
        status = manager.inspect_state(inspect_request, inspect_response);
        ASSERT_STATUS(status, "InspectState", true);

        check_inspect_state_response(inspect_response, inspect_request.session_id(),
            session_request.active_epoch_index(), 0, 0);

        // end session
        EndSessionRequest end_session_request;
        end_session_request.set_session_id(session_request.session_id());
        status = manager.end_session(end_session_request);
        ASSERT_STATUS(status, "EndSession", true);
    });

    test("Should complete with CompletionStatus EXCEPTION when receiving a manual yield with reason TX-EXCEPTION",
        [](ServerManagerClient &manager) {
            StartSessionRequest session_request = create_valid_start_session_request("exception-machine");
            StartSessionResponse session_response;
            Status status = manager.start_session(session_request, session_response);
            ASSERT_STATUS(status, "StartSession", true);

            InspectStateRequest inspect_request;
            init_valid_inspect_state_request(inspect_request, session_request.session_id(), 0);
            InspectStateResponse inspect_response;
            status = manager.inspect_state(inspect_request, inspect_response);
            ASSERT_STATUS(status, "InspectState", true);

            check_inspect_state_response(inspect_response, inspect_request.session_id(),
                session_request.active_epoch_index(), 0, 0, CompletionStatus::EXCEPTION);

            ASSERT(inspect_response.has_exception_data(), "InspectResponse should containd exception data");
            ASSERT(inspect_response.exception_data() == "test payload",
                "exception_data should contain expected exception payload");
            // end session
            EndSessionRequest end_session_request;
            end_session_request.set_session_id(session_request.session_id());
            status = manager.end_session(end_session_request);
            ASSERT_STATUS(status, "EndSession", true);
        });

    test("Should complete with CompletionStatus EXCEPTION after fatal error", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request("fatal-error-machine");
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        // enqueue first
        InspectStateRequest inspect_request;
        init_valid_inspect_state_request(inspect_request, session_request.session_id(), 0);
        InspectStateResponse inspect_response;
        status = manager.inspect_state(inspect_request, inspect_response);
        ASSERT_STATUS(status, "InspectState", true);

        check_inspect_state_response(inspect_response, inspect_request.session_id(),
            session_request.active_epoch_index(), 0, 0, CompletionStatus::EXCEPTION);

        ASSERT(inspect_response.has_exception_data(), "InspectResponse should containd exception data");
        ASSERT(inspect_response.exception_data() == "dapp exited with exit status: 2",
            "exception_data should contain expected exception payload");

        // end session
        EndSessionRequest end_session_request;
        end_session_request.set_session_id(session_request.session_id());
        status = manager.end_session(end_session_request);
        ASSERT_STATUS(status, "EndSession", true);
    });

    test("Should complete with CompletionStatus EXCEPTION after rollup-http-server error",
        [](ServerManagerClient &manager) {
            StartSessionRequest session_request = create_valid_start_session_request("http-server-error-machine");
            StartSessionResponse session_response;
            Status status = manager.start_session(session_request, session_response);
            ASSERT_STATUS(status, "StartSession", true);

            // enqueue first
            InspectStateRequest inspect_request;
            init_valid_inspect_state_request(inspect_request, session_request.session_id(), 0);
            InspectStateResponse inspect_response;
            status = manager.inspect_state(inspect_request, inspect_response);
            ASSERT_STATUS(status, "InspectState", true);

            check_inspect_state_response(inspect_response, inspect_request.session_id(),
                session_request.active_epoch_index(), 0, 0, CompletionStatus::EXCEPTION);

            ASSERT(inspect_response.has_exception_data(), "InspectResponse should containd exception data");
            ASSERT(inspect_response.exception_data() == "rollup-http-server exited with 0 status",
                "exception_data should contain expected exception payload");

            // end session
            EndSessionRequest end_session_request;
            end_session_request.set_session_id(session_request.session_id());
            status = manager.end_session(end_session_request);
            ASSERT_STATUS(status, "EndSession", true);
        });

    test("Should complete a valid request with accept (voucher on inspect)", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request("voucher-on-inspect-machine");
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        InspectStateRequest inspect_request;
        init_valid_inspect_state_request(inspect_request, session_request.session_id(), 0);
        InspectStateResponse inspect_response;
        status = manager.inspect_state(inspect_request, inspect_response);
        ASSERT_STATUS(status, "InspectState", true);

        check_inspect_state_response(inspect_response, inspect_request.session_id(),
            session_request.active_epoch_index(), 0, 0);

        // end session
        EndSessionRequest end_session_request;
        end_session_request.set_session_id(session_request.session_id());
        status = manager.end_session(end_session_request);
        ASSERT_STATUS(status, "EndSession", true);
    });

    test("Should complete a valid request with accept (notice on inspect)", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request("notice-on-inspect-machine");
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        InspectStateRequest inspect_request;
        init_valid_inspect_state_request(inspect_request, session_request.session_id(), 0);
        InspectStateResponse inspect_response;
        status = manager.inspect_state(inspect_request, inspect_response);
        ASSERT_STATUS(status, "InspectState", true);

        check_inspect_state_response(inspect_response, inspect_request.session_id(),
            session_request.active_epoch_index(), 0, 0);

        // end session
        EndSessionRequest end_session_request;
        end_session_request.set_session_id(session_request.session_id());
        status = manager.end_session(end_session_request);
        ASSERT_STATUS(status, "EndSession", true);
    });

    test("Should complete a inspect state request enqueued after a advance state with success",
        [](ServerManagerClient &manager) {
            StartSessionRequest session_request = create_valid_start_session_request("inspect-state-machine");
            StartSessionResponse session_response;
            Status status = manager.start_session(session_request, session_response);
            ASSERT_STATUS(status, "StartSession", true);

            AdvanceStateRequest advance_request;
            init_valid_advance_state_request(advance_request, session_request.session_id(),
                session_request.active_epoch_index(), 0);
            status = manager.advance_state(advance_request);
            ASSERT_STATUS(status, "AdvanceState", true);

            // get epoch status after pending input is processed
            GetEpochStatusRequest status_request;
            status_request.set_session_id(session_request.session_id());
            status_request.set_epoch_index(session_request.active_epoch_index());
            GetEpochStatusResponse status_response;
            wait_pending_inputs_to_be_processed(manager, status_request, status_response, false, 10);

            InspectStateRequest inspect_request;
            init_valid_inspect_state_request(inspect_request, session_request.session_id(), 1);
            InspectStateResponse inspect_response;
            status = manager.inspect_state(inspect_request, inspect_response);
            ASSERT_STATUS(status, "InspectState", true);

            check_inspect_state_response(inspect_response, inspect_request.session_id(),
                session_request.active_epoch_index(), 1, 2);

            // enqueue second
            end_session_after_processing_pending_inputs(manager, session_request.session_id(),
                session_request.active_epoch_index());
        });

    test("Should complete a inspect state request enqueued during a advance state with success",
        [](ServerManagerClient &manager) {
            StartSessionRequest session_request = create_valid_start_session_request("inspect-state-machine");
            StartSessionResponse session_response;
            Status status = manager.start_session(session_request, session_response);
            ASSERT_STATUS(status, "StartSession", true);

            AdvanceStateRequest advance_request;
            init_valid_advance_state_request(advance_request, session_request.session_id(),
                session_request.active_epoch_index(), 0);
            status = manager.advance_state(advance_request);
            ASSERT_STATUS(status, "AdvanceState", true);

            InspectStateRequest inspect_request;
            init_valid_inspect_state_request(inspect_request, session_request.session_id(), 1);
            InspectStateResponse inspect_response;
            status = manager.inspect_state(inspect_request, inspect_response);
            ASSERT_STATUS(status, "InspectState", true);

            check_inspect_state_response(inspect_response, inspect_request.session_id(),
                session_request.active_epoch_index(), 1, 2);

            // enqueue second
            end_session_after_processing_pending_inputs(manager, session_request.session_id(),
                session_request.active_epoch_index());
        });

    test("Should fail to complete if session id is not valid", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request("inspect-state-machine");
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        InspectStateRequest inspect_request;
        init_valid_inspect_state_request(inspect_request, session_request.session_id(), 0);
        inspect_request.set_session_id("NON-EXISTENT");
        InspectStateResponse inspect_response;
        status = manager.inspect_state(inspect_request, inspect_response);
        ASSERT_STATUS(status, "InspectState", false);
        ASSERT_STATUS_CODE(status, "InspectState", StatusCode::INVALID_ARGUMENT);

        // end session
        EndSessionRequest end_session_request;
        end_session_request.set_session_id(session_request.session_id());
        status = manager.end_session(end_session_request);
        ASSERT_STATUS(status, "EndSession", true);
    });

    test("Should fail to complete if session was ended", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request("inspect-state-machine");
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        // end session
        EndSessionRequest end_session_request;
        end_session_request.set_session_id(session_request.session_id());
        status = manager.end_session(end_session_request);
        ASSERT_STATUS(status, "EndSession", true);

        // try to enqueue input on ended session
        InspectStateRequest inspect_request;
        init_valid_inspect_state_request(inspect_request, session_request.session_id(), 0);
        InspectStateResponse inspect_response;
        status = manager.inspect_state(inspect_request, inspect_response);
        ASSERT_STATUS(status, "InspectState", false);
        ASSERT_STATUS_CODE(status, "InspectState", StatusCode::INVALID_ARGUMENT);
    });

    test("Should complete with success enqueing on a new epoch", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request("inspect-state-machine");
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        // finish epoch
        FinishEpochRequest epoch_request;
        FinishEpochResponse epoch_response;
        init_valid_finish_epoch_request(epoch_request, session_request.session_id(),
            session_request.active_epoch_index(), 0);
        status = manager.finish_epoch(epoch_request, epoch_response);
        ASSERT_STATUS(status, "FinishEpoch", true);
        validate_finish_epoch_response(epoch_response, session_request.active_epoch_index(), 0);

        InspectStateRequest inspect_request;
        init_valid_inspect_state_request(inspect_request, session_request.session_id(), 1);
        InspectStateResponse inspect_response;
        status = manager.inspect_state(inspect_request, inspect_response);
        ASSERT_STATUS(status, "InspectState", true);

        check_inspect_state_response(inspect_response, inspect_request.session_id(),
            session_request.active_epoch_index() + 1, 1, 2);

        // end session
        EndSessionRequest end_session_request;
        end_session_request.set_session_id(session_request.session_id());
        status = manager.end_session(end_session_request);
        ASSERT_STATUS(status, "EndSession", true);
    });

    test("Should complete with CompletionStatus PAYLOAD_LENGTH_LIMIT_EXCEEDED if query_payload is greater then rx "
         "buffer",
        [](ServerManagerClient &manager) {
            StartSessionRequest session_request = create_valid_start_session_request("inspect-state-machine");
            StartSessionResponse session_response;
            Status status = manager.start_session(session_request, session_response);
            ASSERT_STATUS(status, "StartSession", true);

            InspectStateRequest inspect_request;
            init_valid_inspect_state_request(inspect_request, session_request.session_id(), 0);
            auto *query_payload = inspect_request.mutable_query_payload();
            query_payload->resize(session_response.config().rollup().rx_buffer().length(), 'f');
            InspectStateResponse inspect_response;
            status = manager.inspect_state(inspect_request, inspect_response);
            ASSERT_STATUS(status, "InspectState", true);

            check_inspect_state_response(inspect_response, inspect_request.session_id(),
                session_request.active_epoch_index(), 0, 0, CompletionStatus::PAYLOAD_LENGTH_LIMIT_EXCEEDED);

            // end session
            EndSessionRequest end_session_request;
            end_session_request.set_session_id(session_request.session_id());
            status = manager.end_session(end_session_request);
            ASSERT_STATUS(status, "EndSession", true);
        });

    test("Should complete with CompletionStatus CYCLE_LIMIT_EXCEEDED", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request("inspect-state-machine");
        StartSessionResponse session_response;
        CyclesConfig *server_cycles = session_request.mutable_server_cycles();
        server_cycles->set_max_inspect_state(2);
        server_cycles->set_inspect_state_increment(2);
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        // enqueue first
        InspectStateRequest inspect_request;
        init_valid_inspect_state_request(inspect_request, session_request.session_id(), 0);
        InspectStateResponse inspect_response;
        status = manager.inspect_state(inspect_request, inspect_response);
        ASSERT_STATUS(status, "InspectState", true);

        check_inspect_state_response(inspect_response, inspect_request.session_id(),
            session_request.active_epoch_index(), 0, 0, CompletionStatus::CYCLE_LIMIT_EXCEEDED);

        // end session
        EndSessionRequest end_session_request;
        end_session_request.set_session_id(session_request.session_id());
        status = manager.end_session(end_session_request);
        ASSERT_STATUS(status, "EndSession", true);
    });

    test("Should complete with CompletionStatus REJECTED_BY_MACHINE", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request("inspect-rejecting-machine");
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        // enqueue first
        InspectStateRequest inspect_request;
        init_valid_inspect_state_request(inspect_request, session_request.session_id(), 0);
        InspectStateResponse inspect_response;
        status = manager.inspect_state(inspect_request, inspect_response);
        ASSERT_STATUS(status, "InspectState", true);

        check_inspect_state_response(inspect_response, inspect_request.session_id(),
            session_request.active_epoch_index(), 0, 0, CompletionStatus::REJECTED);

        // end session
        EndSessionRequest end_session_request;
        end_session_request.set_session_id(session_request.session_id());
        status = manager.end_session(end_session_request);
        ASSERT_STATUS(status, "EndSession", true);
    });

    test("Should complete with CompletionStatus MACHINE_HALTED", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request("halting-machine");
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        // enqueue first
        InspectStateRequest inspect_request;
        init_valid_inspect_state_request(inspect_request, session_request.session_id(), 0);
        InspectStateResponse inspect_response;
        status = manager.inspect_state(inspect_request, inspect_response);
        ASSERT_STATUS(status, "InspectState", true);

        check_inspect_state_response(inspect_response, inspect_request.session_id(),
            session_request.active_epoch_index(), 0, 0, CompletionStatus::MACHINE_HALTED);

        // end session
        EndSessionRequest end_session_request;
        end_session_request.set_session_id(session_request.session_id());
        status = manager.end_session(end_session_request);
        ASSERT_STATUS(status, "EndSession", true);
    });

    test("Should complete with CompletionStatus TIME_LIMIT_EXCEEDED", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request("inspect-state-machine");
        StartSessionResponse session_response;
        CyclesConfig *server_cycles = session_request.mutable_server_cycles();
        server_cycles->set_inspect_state_increment(10);
        auto *server_deadline = session_request.mutable_server_deadline();
        server_deadline->set_inspect_state(1000);
        server_deadline->set_inspect_state_increment(1000);
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        // enqueue first
        InspectStateRequest inspect_request;
        init_valid_inspect_state_request(inspect_request, session_request.session_id(), 0);
        InspectStateResponse inspect_response;
        status = manager.inspect_state(inspect_request, inspect_response);
        ASSERT_STATUS(status, "InspectState", true);

        check_inspect_state_response(inspect_response, inspect_request.session_id(),
            session_request.active_epoch_index(), 0, 0, CompletionStatus::TIME_LIMIT_EXCEEDED);

        // end session
        EndSessionRequest end_session_request;
        end_session_request.set_session_id(session_request.session_id());
        status = manager.end_session(end_session_request);
        ASSERT_STATUS(status, "EndSession", true);
    });

    test("Should complete with Status DEADLINE_EXCEEDED", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request("infinite-loop-machine");
        StartSessionResponse session_response;
        auto *server_deadline = session_request.mutable_server_deadline();
        server_deadline->set_inspect_state_increment(1);
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        // enqueue first
        InspectStateRequest inspect_request;
        init_valid_inspect_state_request(inspect_request, session_request.session_id(), 0);
        InspectStateResponse inspect_response;
        status = manager.inspect_state(inspect_request, inspect_response);
        ASSERT_STATUS(status, "InspectState", false);
        ASSERT_STATUS_CODE(status, "InspectState", StatusCode::DEADLINE_EXCEEDED);

        // end session
        EndSessionRequest end_session_request;
        end_session_request.set_session_id(session_request.session_id());
        status = manager.end_session(end_session_request);
        ASSERT_STATUS(status, "EndSession", true);
    });
}

static bool check_session_store(const std::string &machine_dir) {
    static const std::vector<std::string> files = {
        "hash", "config.protobuf",
        "0000000000001000-f000.bin",    // rom
        "0000000000020000-6000.bin",    // shadow tlb
        "0000000060000000-200000.bin",  // rollup rx buffer
        "0000000060200000-200000.bin",  // rollup tx buffer
        "0000000060400000-1000.bin",    // rollup input metadata
        "0000000060600000-200000.bin",  // rollup voucher hashes
        "0000000060800000-200000.bin",  // rollup notice hashes
        "0000000080000000-4000000.bin", // ram
        "0080000000000000-4400000.bin"  // root drive
    };
    if (machine_dir.empty()) {
        return false;
    }
    path full_path{machine_dir};
    return std::all_of(files.begin(), files.end(),
        [&full_path](const std::string &f) { return exists(full_path / f); });
}

static void test_finish_epoch(const std::function<void(const std::string &title, test_function f)> &test) {
    test("Should complete a valid request with success", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request();
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        FinishEpochRequest epoch_request;
        FinishEpochResponse epoch_response;
        init_valid_finish_epoch_request(epoch_request, session_request.session_id(),
            session_request.active_epoch_index(), 0);
        status = manager.finish_epoch(epoch_request, epoch_response);
        ASSERT_STATUS(status, "FinishEpoch", true);
        validate_finish_epoch_response(epoch_response, session_request.active_epoch_index(), 0);

        // end session
        EndSessionRequest end_session_request;
        end_session_request.set_session_id(session_request.session_id());
        status = manager.end_session(end_session_request);
        ASSERT_STATUS(status, "EndSession", true);
    });

    test("Should fail to complete if session id is not valid", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request();
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        FinishEpochRequest epoch_request;
        FinishEpochResponse epoch_response;
        init_valid_finish_epoch_request(epoch_request, session_request.session_id(),
            session_request.active_epoch_index(), 0);
        epoch_request.set_session_id("NON-EXISTENT");
        status = manager.finish_epoch(epoch_request, epoch_response);
        ASSERT_STATUS(status, "FinishEpoch", false);
        ASSERT_STATUS_CODE(status, "FinishEpoch", StatusCode::INVALID_ARGUMENT);

        // end session
        EndSessionRequest end_session_request;
        end_session_request.set_session_id(session_request.session_id());
        status = manager.end_session(end_session_request);
        ASSERT_STATUS(status, "EndSession", true);
    });

    test("Should fail to complete if session id was ended", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request();
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        // end session
        EndSessionRequest end_session_request;
        end_session_request.set_session_id(session_request.session_id());
        status = manager.end_session(end_session_request);
        ASSERT_STATUS(status, "EndSession", true);

        FinishEpochRequest epoch_request;
        FinishEpochResponse epoch_response;
        init_valid_finish_epoch_request(epoch_request, session_request.session_id(),
            session_request.active_epoch_index(), 0);
        status = manager.finish_epoch(epoch_request, epoch_response);
        ASSERT_STATUS(status, "FinishEpoch", false);
        ASSERT_STATUS_CODE(status, "FinishEpoch", StatusCode::INVALID_ARGUMENT);
    });

    test("Should fail to complete if epoch index is already finished", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request();
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        FinishEpochRequest epoch_request;
        FinishEpochResponse epoch_response;
        init_valid_finish_epoch_request(epoch_request, session_request.session_id(),
            session_request.active_epoch_index(), 0);
        status = manager.finish_epoch(epoch_request, epoch_response);
        ASSERT_STATUS(status, "FinishEpoch", true);
        validate_finish_epoch_response(epoch_response, session_request.active_epoch_index(), 0);

        status = manager.finish_epoch(epoch_request, epoch_response);
        ASSERT_STATUS(status, "FinishEpoch", false);
        ASSERT_STATUS_CODE(status, "FinishEpoch", StatusCode::INVALID_ARGUMENT);

        // end session
        EndSessionRequest end_session_request;
        end_session_request.set_session_id(session_request.session_id());
        status = manager.end_session(end_session_request);
        ASSERT_STATUS(status, "EndSession", true);
    });

    test("Should fail to complete if active epoch index is not match", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request();
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        FinishEpochRequest epoch_request;
        FinishEpochResponse epoch_response;
        init_valid_finish_epoch_request(epoch_request, session_request.session_id(),
            session_request.active_epoch_index(), 0);
        epoch_request.set_active_epoch_index(epoch_request.active_epoch_index() + 10);
        status = manager.finish_epoch(epoch_request, epoch_response);
        ASSERT_STATUS(status, "FinishEpoch", false);
        ASSERT_STATUS_CODE(status, "FinishEpoch", StatusCode::INVALID_ARGUMENT);

        // end session
        EndSessionRequest end_session_request;
        end_session_request.set_session_id(session_request.session_id());
        status = manager.end_session(end_session_request);
        ASSERT_STATUS(status, "EndSession", true);
    });

    test("Should fail to complete if active epoch is on the limit", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request();
        StartSessionResponse session_response;
        session_request.set_active_epoch_index(UINT64_MAX - 1);
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        // Go to active_epoch_index = UINT64_MAX
        FinishEpochRequest epoch_request;
        FinishEpochResponse epoch_response;
        init_valid_finish_epoch_request(epoch_request, session_request.session_id(),
            session_request.active_epoch_index(), 0);
        status = manager.finish_epoch(epoch_request, epoch_response);
        ASSERT_STATUS(status, "FinishEpoch", true);
        validate_finish_epoch_response(epoch_response, session_request.active_epoch_index(), 0);

        // status
        GetSessionStatusRequest status_request;
        status_request.set_session_id(session_request.session_id());
        GetSessionStatusResponse status_response;
        status = manager.get_session_status(status_request, status_response);
        ASSERT_STATUS(status, "GetSessionStatus", true);
        ASSERT(status_response.active_epoch_index() == UINT64_MAX, "active epoch index should be UINT64_MAX");

        status = manager.finish_epoch(epoch_request, epoch_response);
        ASSERT_STATUS(status, "FinishEpoch", false);
        ASSERT_STATUS_CODE(status, "FinishEpoch", StatusCode::OUT_OF_RANGE);

        status_request.set_session_id(session_request.session_id());
        status = manager.get_session_status(status_request, status_response);
        ASSERT_STATUS(status, "GetSessionStatus", true);
        ASSERT(status_response.active_epoch_index() == UINT64_MAX, "active epoch index should stop at UINT64_MAX");

        // end session
        EndSessionRequest end_session_request;
        end_session_request.set_session_id(session_request.session_id());
        status = manager.end_session(end_session_request);
        ASSERT_STATUS(status, "EndSession", true);
    });

    test("Should fail to complete if pending input count is not empty", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request();
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        // enqueue
        AdvanceStateRequest advance_request;
        init_valid_advance_state_request(advance_request, session_request.session_id(),
            session_request.active_epoch_index(), 0);
        status = manager.advance_state(advance_request);
        ASSERT_STATUS(status, "AdvanceState", true);

        FinishEpochRequest epoch_request;
        FinishEpochResponse epoch_response;
        init_valid_finish_epoch_request(epoch_request, session_request.session_id(),
            session_request.active_epoch_index(), 1);
        status = manager.finish_epoch(epoch_request, epoch_response);
        ASSERT_STATUS(status, "FinishEpoch", false);
        ASSERT_STATUS_CODE(status, "FinishEpoch", StatusCode::INVALID_ARGUMENT);

        // end session
        end_session_after_processing_pending_inputs(manager, session_request.session_id(),
            session_request.active_epoch_index());
    });

    test("Should fail to complete if processed input count does not match", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request();
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        FinishEpochRequest epoch_request;
        FinishEpochResponse epoch_response;
        init_valid_finish_epoch_request(epoch_request, session_request.session_id(),
            session_request.active_epoch_index(), 10);
        status = manager.finish_epoch(epoch_request, epoch_response);
        ASSERT_STATUS(status, "FinishEpoch", false);
        ASSERT_STATUS_CODE(status, "FinishEpoch", StatusCode::INVALID_ARGUMENT);

        // end session
        EndSessionRequest end_session_request;
        end_session_request.set_session_id(session_request.session_id());
        status = manager.end_session(end_session_request);
        ASSERT_STATUS(status, "EndSession", true);
    });

    test("Should complete with success storing the machine", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request();
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        std::string storage_dir{"sessions"};
        ASSERT(create_storage_directory(storage_dir), "test should be able to create directory");

        FinishEpochRequest epoch_request;
        FinishEpochResponse epoch_response;
        std::string machine_dir = get_machine_directory(storage_dir, "test_" + manager.test_id());
        init_valid_finish_epoch_request(epoch_request, session_request.session_id(),
            session_request.active_epoch_index(), 0, machine_dir);
        status = manager.finish_epoch(epoch_request, epoch_response);
        ASSERT_STATUS(status, "FinishEpoch", true);
        validate_finish_epoch_response(epoch_response, session_request.active_epoch_index(), 0);

        ASSERT(check_session_store(machine_dir),
            "FinishEpoch should store machine to disk if storage directory is defined");
        ASSERT(delete_storage_directory(storage_dir), "test should be able to remove dir");

        // end session
        EndSessionRequest end_session_request;
        end_session_request.set_session_id(session_request.session_id());
        status = manager.end_session(end_session_request);
        ASSERT_STATUS(status, "EndSession", true);
    });

    test("Should fail to complete if the server does not have permission to write", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request();
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        std::string storage_dir{"sessions"};
        ASSERT(create_storage_directory(storage_dir), "test should be able to create directory");
        ASSERT(change_storage_directory_permissions(storage_dir, false),
            "test should be able to change directory permissions");

        FinishEpochRequest epoch_request;
        FinishEpochResponse epoch_response;
        std::string machine_dir = get_machine_directory(storage_dir, "test_" + manager.test_id());
        init_valid_finish_epoch_request(epoch_request, session_request.session_id(),
            session_request.active_epoch_index(), 0, machine_dir);
        status = manager.finish_epoch(epoch_request, epoch_response);
        ASSERT_STATUS(status, "FinishEpoch", false);
        ASSERT_STATUS_CODE(status, "FinishEpoch", StatusCode::ABORTED);

        ASSERT(!check_session_store(machine_dir),
            "FinishEpoch should store machine to disk if storage directory is defined");
        ASSERT(delete_storage_directory(storage_dir), "test should be able to remove dir");

        // end session
        EndSessionRequest end_session_request;
        end_session_request.set_session_id(session_request.session_id());
        status = manager.end_session(end_session_request);
        ASSERT_STATUS(status, "EndSession", true);
    });

    test("Should fail to complete with the directory already exists", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request();
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        std::string storage_dir{"sessions"};
        ASSERT(create_storage_directory(storage_dir), "test should be able to create directory");

        FinishEpochRequest epoch_request;
        FinishEpochResponse epoch_response;
        std::string machine_dir = get_machine_directory(storage_dir, "test_" + manager.test_id());
        init_valid_finish_epoch_request(epoch_request, session_request.session_id(),
            session_request.active_epoch_index(), 0, machine_dir);
        status = manager.finish_epoch(epoch_request, epoch_response);
        ASSERT_STATUS(status, "FinishEpoch", true);
        validate_finish_epoch_response(epoch_response, session_request.active_epoch_index(), 0);

        ASSERT(check_session_store(machine_dir),
            "FinishEpoch should store machine to disk if storage directory is defined");

        init_valid_finish_epoch_request(epoch_request, session_request.session_id(),
            session_request.active_epoch_index() + 1, 0, machine_dir);
        status = manager.finish_epoch(epoch_request, epoch_response);
        ASSERT_STATUS(status, "FinishEpoch", false);
        ASSERT_STATUS_CODE(status, "FinishEpoch", StatusCode::ABORTED);

        ASSERT(delete_storage_directory(storage_dir), "test should be able to remove dir");

        // end session
        EndSessionRequest end_session_request;
        end_session_request.set_session_id(session_request.session_id());
        status = manager.end_session(end_session_request);
        ASSERT_STATUS(status, "EndSession", true);
    });

    test("StartSession should complete with success from a previous stored the machine",
        [](ServerManagerClient &manager) {
            StartSessionRequest session_request = create_valid_start_session_request();
            StartSessionResponse session_response;
            Status status = manager.start_session(session_request, session_response);
            ASSERT_STATUS(status, "StartSession", true);

            std::string storage_dir{"sessions"};
            ASSERT(create_storage_directory(storage_dir), "test should be able to create directory");

            FinishEpochRequest epoch_request;
            FinishEpochResponse epoch_response;
            std::string machine_dir = get_machine_directory(storage_dir, "test_" + manager.test_id());
            init_valid_finish_epoch_request(epoch_request, session_request.session_id(),
                session_request.active_epoch_index(), 0, machine_dir);
            status = manager.finish_epoch(epoch_request, epoch_response);
            ASSERT_STATUS(status, "FinishEpoch", true);
            validate_finish_epoch_response(epoch_response, session_request.active_epoch_index(), 0);

            ASSERT(check_session_store(machine_dir),
                "FinishEpoch should store machine to disk if storage directory is defined");

            // end session
            EndSessionRequest end_session_request;
            end_session_request.set_session_id(session_request.session_id());
            status = manager.end_session(end_session_request);
            ASSERT_STATUS(status, "EndSession", true);

            auto *stored_machine_dir = session_request.mutable_machine_directory();
            (*stored_machine_dir) = machine_dir;
            status = manager.start_session(session_request, session_response);
            ASSERT_STATUS(status, "StartSession", true);

            end_session_request.set_session_id(session_request.session_id());
            status = manager.end_session(end_session_request);
            ASSERT_STATUS(status, "EndSession", true);

            ASSERT(delete_storage_directory(storage_dir), "test should be able to remove dir");
        });

    test("Should complete with success when processed input count greater than 1", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request();
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        // enqueue first
        AdvanceStateRequest advance_request;
        init_valid_advance_state_request(advance_request, session_request.session_id(),
            session_request.active_epoch_index(), 0);
        status = manager.advance_state(advance_request);
        ASSERT_STATUS(status, "AdvanceState", true);
        // enqueue second
        init_valid_advance_state_request(advance_request, session_request.session_id(),
            session_request.active_epoch_index(), 1);
        status = manager.advance_state(advance_request);
        ASSERT_STATUS(status, "AdvanceState", true);

        // get epoch status after pending input is processed
        GetEpochStatusRequest status_request;
        status_request.set_session_id(session_request.session_id());
        status_request.set_epoch_index(session_request.active_epoch_index());
        GetEpochStatusResponse status_response;
        wait_pending_inputs_to_be_processed(manager, status_request, status_response, false,
            WAITING_PENDING_INPUT_MAX_RETRIES);

        // assert status_resonse content
        ASSERT(status_response.session_id() == session_request.session_id(),
            "status response session_id should be the same as the one created");
        ASSERT(status_response.epoch_index() == session_request.active_epoch_index(),
            "status response epoch_index should be 0");
        ASSERT(status_response.state() == EpochState::ACTIVE, "status response state should be ACTIVE");
        ASSERT(status_response.processed_inputs_size() == 2, "status response processed_inputs size should be 2");
        ASSERT(status_response.pending_input_count() == 0, "status response pending_input_count should 0");
        ASSERT(!status_response.has_taint_status(), "status response should not be tainted");

        // Finish epoch
        FinishEpochRequest epoch_request;
        FinishEpochResponse epoch_response;
        init_valid_finish_epoch_request(epoch_request, session_request.session_id(),
            session_request.active_epoch_index(), status_response.processed_inputs_size());
        status = manager.finish_epoch(epoch_request, epoch_response);
        ASSERT_STATUS(status, "FinishEpoch", true);
        validate_finish_epoch_response(epoch_response, session_request.active_epoch_index(), 2);

        // EndSession
        EndSessionRequest end_session_request;
        end_session_request.set_session_id(session_request.session_id());
        status = manager.end_session(end_session_request);
        ASSERT_STATUS(status, "EndSession", true);
    });
}

static void test_delete_epoch(const std::function<void(const std::string &title, test_function f)> &test) {
    test("Should complete a valid request with success", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request();
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        // finish epoch
        FinishEpochRequest epoch_request;
        FinishEpochResponse epoch_response;
        init_valid_finish_epoch_request(epoch_request, session_request.session_id(),
            session_request.active_epoch_index(), 0);
        status = manager.finish_epoch(epoch_request, epoch_response);
        ASSERT_STATUS(status, "FinishEpoch", true);
        validate_finish_epoch_response(epoch_response, session_request.active_epoch_index(), 0);

        GetEpochStatusRequest status_request;
        status_request.set_session_id(session_request.session_id());
        status_request.set_epoch_index(0);
        GetEpochStatusResponse status_response;
        status = manager.get_epoch_status(status_request, status_response);
        ASSERT_STATUS(status, "GetEpochStatus", true);

        // assert status_resonse content
        check_empty_epoch_status(status_response, session_request.session_id(), session_request.active_epoch_index(),
            EpochState::FINISHED, 0);

        DeleteEpochRequest delete_request;
        delete_request.set_session_id(session_request.session_id());
        delete_request.set_epoch_index(0);
        status = manager.delete_epoch(delete_request);
        ASSERT_STATUS(status, "DeleteEpoch", true);

        status = manager.get_epoch_status(status_request, status_response);
        ASSERT_STATUS(status, "GetEpochStatus", false);

        // end session
        EndSessionRequest end_session_request;
        end_session_request.set_session_id(session_request.session_id());
        status = manager.end_session(end_session_request);
        ASSERT_STATUS(status, "EndSession", true);
    });

    test("Should fail to complete with a invalid session id", [](ServerManagerClient &manager) {
        DeleteEpochRequest delete_request;
        delete_request.set_session_id("NON-EXISTENT");
        delete_request.set_epoch_index(0);
        Status status = manager.delete_epoch(delete_request);
        ASSERT_STATUS(status, "DeleteEpoch", false);
        ASSERT_STATUS_CODE(status, "DeleteEpoch", StatusCode::INVALID_ARGUMENT);
    });

    test("Should fail to complete with a ended session id", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request();
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        // end session
        EndSessionRequest end_session_request;
        end_session_request.set_session_id(session_request.session_id());
        status = manager.end_session(end_session_request);
        ASSERT_STATUS(status, "EndSession", true);

        // try to enqueue input on ended session
        DeleteEpochRequest delete_request;
        delete_request.set_session_id(session_request.session_id());
        delete_request.set_epoch_index(0);
        status = manager.delete_epoch(delete_request);
        ASSERT_STATUS(status, "DeleteEpoch", false);
        ASSERT_STATUS_CODE(status, "DeleteEpoch", StatusCode::INVALID_ARGUMENT);
    });

    test("Should fail to complete if epoch index is not valid", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request();
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        // try to enqueue input on ended session
        DeleteEpochRequest delete_request;
        delete_request.set_session_id(session_request.session_id());
        delete_request.set_epoch_index(session_request.active_epoch_index() + 10);
        status = manager.delete_epoch(delete_request);
        ASSERT_STATUS(status, "DeleteEpoch", false);
        ASSERT_STATUS_CODE(status, "DeleteEpoch", StatusCode::INVALID_ARGUMENT);

        // end session
        EndSessionRequest end_session_request;
        end_session_request.set_session_id(session_request.session_id());
        status = manager.end_session(end_session_request);
        ASSERT_STATUS(status, "EndSession", true);
    });

    test("Should fail to complete if epoch index is active", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request();
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        // try to enqueue input on ended session
        DeleteEpochRequest delete_request;
        delete_request.set_session_id(session_request.session_id());
        delete_request.set_epoch_index(session_request.active_epoch_index());
        status = manager.delete_epoch(delete_request);
        ASSERT_STATUS(status, "DeleteEpoch", false);
        ASSERT_STATUS_CODE(status, "DeleteEpoch", StatusCode::INVALID_ARGUMENT);

        // end session
        EndSessionRequest end_session_request;
        end_session_request.set_session_id(session_request.session_id());
        status = manager.end_session(end_session_request);
        ASSERT_STATUS(status, "EndSession", true);
    });

    test("Should fail to complete if epoch index was already erased", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request();
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        // finish epoch
        FinishEpochRequest epoch_request;
        FinishEpochResponse epoch_response;
        init_valid_finish_epoch_request(epoch_request, session_request.session_id(),
            session_request.active_epoch_index(), 0);
        status = manager.finish_epoch(epoch_request, epoch_response);
        ASSERT_STATUS(status, "FinishEpoch", true);
        validate_finish_epoch_response(epoch_response, session_request.active_epoch_index(), 0);

        GetEpochStatusRequest status_request;
        status_request.set_session_id(session_request.session_id());
        status_request.set_epoch_index(0);
        GetEpochStatusResponse status_response;
        status = manager.get_epoch_status(status_request, status_response);
        ASSERT_STATUS(status, "GetEpochStatus", true);

        // assert status_resonse content
        check_empty_epoch_status(status_response, session_request.session_id(), session_request.active_epoch_index(),
            EpochState::FINISHED, 0);

        DeleteEpochRequest delete_request;
        delete_request.set_session_id(session_request.session_id());
        delete_request.set_epoch_index(0);
        status = manager.delete_epoch(delete_request);
        ASSERT_STATUS(status, "DeleteEpoch", true);

        status = manager.get_epoch_status(status_request, status_response);
        ASSERT_STATUS(status, "GetEpochStatus", false);

        status = manager.delete_epoch(delete_request);
        ASSERT_STATUS(status, "DeleteEpoch", false);
        ASSERT_STATUS_CODE(status, "DeleteEpoch", StatusCode::INVALID_ARGUMENT);

        // end session
        EndSessionRequest end_session_request;
        end_session_request.set_session_id(session_request.session_id());
        status = manager.end_session(end_session_request);
        ASSERT_STATUS(status, "EndSession", true);
    });
}

static void test_end_session(const std::function<void(const std::string &title, test_function f)> &test) {
    test("Should complete a valid request with success", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request();
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        EndSessionRequest end_session_request;
        end_session_request.set_session_id(session_request.session_id());
        status = manager.end_session(end_session_request);
        ASSERT_STATUS(status, "EndSession", true);
    });

    test("Should fail to complete if session id is not valid", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request();
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        EndSessionRequest end_session_request;
        end_session_request.set_session_id("NON-EXISTENT");
        status = manager.end_session(end_session_request);
        ASSERT_STATUS(status, "EndSession", false);
        ASSERT_STATUS_CODE(status, "EndSession", StatusCode::INVALID_ARGUMENT);

        // end session
        end_session_request.set_session_id(session_request.session_id());
        status = manager.end_session(end_session_request);
        ASSERT_STATUS(status, "EndSession", true);
    });

    test("Should fail to complete if session id was already ended", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request();
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        EndSessionRequest end_session_request;
        end_session_request.set_session_id(session_request.session_id());
        status = manager.end_session(end_session_request);
        ASSERT_STATUS(status, "EndSession", true);

        // same request again
        status = manager.end_session(end_session_request);
        ASSERT_STATUS(status, "EndSession", false);
        ASSERT_STATUS_CODE(status, "EndSession", StatusCode::INVALID_ARGUMENT);
    });

    test("Should fail to complete if session active epoch has pending or processed inputs",
        [](ServerManagerClient &manager) {
            StartSessionRequest session_request = create_valid_start_session_request();
            StartSessionResponse session_response;
            Status status = manager.start_session(session_request, session_response);
            ASSERT_STATUS(status, "StartSession", true);

            // enqueue
            AdvanceStateRequest advance_request;
            init_valid_advance_state_request(advance_request, session_request.session_id(),
                session_request.active_epoch_index(), 0);
            status = manager.advance_state(advance_request);
            ASSERT_STATUS(status, "AdvanceState", true);

            EndSessionRequest end_session_request;
            end_session_request.set_session_id(session_request.session_id());
            status = manager.end_session(end_session_request);
            ASSERT_STATUS(status, "EndSession", false);
            ASSERT_STATUS_CODE(status, "EndSession", StatusCode::INVALID_ARGUMENT);

            // get epoch status after pending input is processed
            GetEpochStatusRequest status_request;
            status_request.set_session_id(session_request.session_id());
            status_request.set_epoch_index(session_request.active_epoch_index());
            GetEpochStatusResponse status_response;
            wait_pending_inputs_to_be_processed(manager, status_request, status_response, false,
                WAITING_PENDING_INPUT_MAX_RETRIES);

            status = manager.end_session(end_session_request);
            ASSERT_STATUS(status, "EndSession", false);
            ASSERT_STATUS_CODE(status, "EndSession", StatusCode::INVALID_ARGUMENT);

            end_session_after_processing_pending_inputs(manager, session_request.session_id(),
                session_request.active_epoch_index());
        });
}

static void test_session_simulations(const std::function<void(const std::string &title, test_function f)> &test) {
    test("Should EndSession with success after processing two inputs on one epoch", [](ServerManagerClient &manager) {
        StartSessionRequest session_request = create_valid_start_session_request();
        StartSessionResponse session_response;
        Status status = manager.start_session(session_request, session_response);
        ASSERT_STATUS(status, "StartSession", true);

        // enqueue 0 epoch 0
        AdvanceStateRequest advance_request;
        init_valid_advance_state_request(advance_request, session_request.session_id(),
            session_request.active_epoch_index(), 0);
        status = manager.advance_state(advance_request);
        ASSERT_STATUS(status, "AdvanceState", true);
        // enqueue 1 epoch 0
        init_valid_advance_state_request(advance_request, session_request.session_id(),
            session_request.active_epoch_index(), 1);
        status = manager.advance_state(advance_request);
        ASSERT_STATUS(status, "AdvanceState", true);

        // get epoch status after pending input is processed
        GetEpochStatusRequest status_request;
        status_request.set_session_id(session_request.session_id());
        status_request.set_epoch_index(session_request.active_epoch_index());
        GetEpochStatusResponse status_response;
        wait_pending_inputs_to_be_processed(manager, status_request, status_response, false,
            WAITING_PENDING_INPUT_MAX_RETRIES);

        // assert status_resonse content
        ASSERT(status_response.session_id() == session_request.session_id(),
            "status response session_id should be the same as the one created");
        ASSERT(status_response.epoch_index() == session_request.active_epoch_index(),
            "status response epoch_index should be 0");
        ASSERT(status_response.state() == EpochState::ACTIVE, "status response state should be ACTIVE");
        ASSERT(status_response.processed_inputs_size() == 2, "status response processed_inputs size should be 1");
        ASSERT(status_response.pending_input_count() == 0, "status response pending_input_count should 0");
        ASSERT(!status_response.has_taint_status(), "status response should not be tainted");

        uint64_t index = 0;
        for (auto processed_input : status_response.processed_inputs()) {
            check_processed_input(processed_input, index, 2, 2, 2);
            index++;
        }

        // Finish epoch
        FinishEpochRequest epoch_request;
        FinishEpochResponse epoch_response;
        init_valid_finish_epoch_request(epoch_request, session_request.session_id(),
            session_request.active_epoch_index(), status_response.processed_inputs_size());
        status = manager.finish_epoch(epoch_request, epoch_response);
        ASSERT_STATUS(status, "FinishEpoch", true);
        validate_finish_epoch_response(epoch_response, session_request.active_epoch_index(), 2);

        // EndSession
        EndSessionRequest end_session_request;
        end_session_request.set_session_id(session_request.session_id());
        status = manager.end_session(end_session_request);
        ASSERT_STATUS(status, "EndSession", true);
    });

    test("Should EndSession with success after processing multiple inputs on multiple epochs",
        [](ServerManagerClient &manager) {
            StartSessionRequest session_request = create_valid_start_session_request();
            StartSessionResponse session_response;
            Status status = manager.start_session(session_request, session_response);
            ASSERT_STATUS(status, "StartSession", true);

            // enqueue 0 epoch 0
            AdvanceStateRequest advance_request;
            init_valid_advance_state_request(advance_request, session_request.session_id(), 0, 0);
            status = manager.advance_state(advance_request);
            ASSERT_STATUS(status, "AdvanceState", true);
            // enqueue 1 epoch 0
            init_valid_advance_state_request(advance_request, session_request.session_id(), 0, 1);
            status = manager.advance_state(advance_request);
            ASSERT_STATUS(status, "AdvanceState", true);

            // get epoch status after pending input is processed
            GetEpochStatusRequest status_request;
            status_request.set_session_id(session_request.session_id());
            status_request.set_epoch_index(session_request.active_epoch_index());
            GetEpochStatusResponse status_response;
            wait_pending_inputs_to_be_processed(manager, status_request, status_response, false,
                WAITING_PENDING_INPUT_MAX_RETRIES);

            // assert status_resonse content
            ASSERT(status_response.session_id() == session_request.session_id(),
                "status response session_id should be the same as the one created");
            ASSERT(status_response.epoch_index() == session_request.active_epoch_index(),
                "status response epoch_index should be 0");
            ASSERT(status_response.state() == EpochState::ACTIVE, "status response state should be ACTIVE");
            ASSERT(status_response.processed_inputs_size() == 2, "status response processed_inputs size should be 1");
            ASSERT(status_response.pending_input_count() == 0, "status response pending_input_count should 0");
            ASSERT(!status_response.has_taint_status(), "status response should not be tainted");

            uint64_t index = 0;
            for (auto processed_input : status_response.processed_inputs()) {
                check_processed_input(processed_input, index, 2, 2, 2);
                index++;
            }

            // Finish epoch
            FinishEpochRequest epoch_request;
            FinishEpochResponse epoch_response;
            init_valid_finish_epoch_request(epoch_request, session_request.session_id(), 0, 2);
            status = manager.finish_epoch(epoch_request, epoch_response);
            ASSERT_STATUS(status, "FinishEpoch", true);
            validate_finish_epoch_response(epoch_response, 0, 2);

            // enqueue 0 epoch 1
            init_valid_advance_state_request(advance_request, session_request.session_id(), 1, 2);
            status = manager.advance_state(advance_request);
            ASSERT_STATUS(status, "AdvanceState", true);
            // enqueue 1 epoch 1
            init_valid_advance_state_request(advance_request, session_request.session_id(), 1, 3);
            status = manager.advance_state(advance_request);
            ASSERT_STATUS(status, "AdvanceState", true);

            status_request.set_epoch_index(1);
            wait_pending_inputs_to_be_processed(manager, status_request, status_response, false,
                WAITING_PENDING_INPUT_MAX_RETRIES);

            ASSERT(status_response.session_id() == session_request.session_id(),
                "status response session_id should be the same as the one created");
            ASSERT(status_response.epoch_index() == 1, "status response epoch_index should be 1");
            ASSERT(status_response.state() == EpochState::ACTIVE, "status response state should be ACTIVE");
            ASSERT(status_response.processed_inputs_size() == 2, "status response processed_inputs size should be 1");
            ASSERT(status_response.pending_input_count() == 0, "status response pending_input_count should 0");
            ASSERT(!status_response.has_taint_status(), "status response should not be tainted");

            for (auto processed_input : status_response.processed_inputs()) {
                check_processed_input(processed_input, index, 2, 2, 2);
                index++;
            }

            // Finish epoch
            init_valid_finish_epoch_request(epoch_request, session_request.session_id(), 1, 2);
            status = manager.finish_epoch(epoch_request, epoch_response);
            ASSERT_STATUS(status, "FinishEpoch", true);
            validate_finish_epoch_response(epoch_response, 1, 2);

            status_request.set_epoch_index(2);
            wait_pending_inputs_to_be_processed(manager, status_request, status_response, false,
                WAITING_PENDING_INPUT_MAX_RETRIES);

            ASSERT(status_response.session_id() == session_request.session_id(),
                "status response session_id should be the same as the one created");
            ASSERT(status_response.epoch_index() == 2, "status response epoch_index should be 2");
            ASSERT(status_response.state() == EpochState::ACTIVE, "status response state should be ACTIVE");
            ASSERT(status_response.processed_inputs_size() == 0, "status response processed_inputs size should be 0");
            ASSERT(status_response.pending_input_count() == 0, "status response pending_input_count should 0");
            ASSERT(!status_response.has_taint_status(), "status response should not be tainted");

            // EndSession
            EndSessionRequest end_session_request;
            end_session_request.set_session_id(session_request.session_id());
            status = manager.end_session(end_session_request);
            ASSERT_STATUS(status, "EndSession", true);
        });
}

static void test_health_check(const std::function<void(const std::string &title, test_function f)> &test) {
    test("The server-manager server health status should be SERVING", [](ServerManagerClient &manager) {
        HealthCheckRequest request;
        HealthCheckResponse response;
        auto *service = request.mutable_service();
        *service = "";
        Status status = manager.health_check(request, response);
        ASSERT_STATUS(status, "HealthCheck", true);
        ASSERT((response.status() == HealthCheckResponse_ServingStatus_SERVING), "Version Major should be 0");
    });

    test("The server-manager ServerManager service health status should be SERVING", [](ServerManagerClient &manager) {
        HealthCheckRequest request;
        HealthCheckResponse response;
        auto *service = request.mutable_service();
        *service = ServerManager::service_full_name();
        Status status = manager.health_check(request, response);
        ASSERT_STATUS(status, "HealthCheck", true);
        ASSERT((response.status() == HealthCheckResponse_ServingStatus_SERVING), "Version Major should be 0");
    });

    test("The server-manager ManagerCheckIn service health status should be SERVING", [](ServerManagerClient &manager) {
        HealthCheckRequest request;
        HealthCheckResponse response;
        auto *service = request.mutable_service();
        *service = CartesiMachine::MachineCheckIn::service_full_name();
        Status status = manager.health_check(request, response);
        ASSERT_STATUS(status, "HealthCheck", true);
        ASSERT((response.status() == HealthCheckResponse_ServingStatus_SERVING), "Version Major should be 0");
    });

    test("The server-manager Health service health status should be SERVING", [](ServerManagerClient &manager) {
        HealthCheckRequest request;
        HealthCheckResponse response;
        auto *service = request.mutable_service();
        *service = Health::service_full_name();
        Status status = manager.health_check(request, response);
        ASSERT_STATUS(status, "HealthCheck", true);
        ASSERT((response.status() == HealthCheckResponse_ServingStatus_SERVING), "Version Major should be 0");
    });

    test("The server-manager Unknown service status should be NOT FOUND", [](ServerManagerClient &manager) {
        HealthCheckRequest request;
        HealthCheckResponse response;
        auto *service = request.mutable_service();
        *service = "UnknownService";
        Status status = manager.health_check(request, response);
        ASSERT_STATUS(status, "HealthCheck", false);
        ASSERT_STATUS_CODE(status, "HealthCheck", StatusCode::NOT_FOUND);
    });
}

static int run_tests(const char *address) {
    ServerManagerClient manager(address);
    test_suite suite(manager);
    suite.add_test_set("GetVersion", test_get_version);
    suite.add_test_set("HealthCheck", test_health_check);
    suite.add_test_set("StartSession", test_start_session);
    suite.add_test_set("AdvanceState", test_advance_state);
    suite.add_test_set("GetStatus", test_get_status);
    suite.add_test_set("GetSessionStatus", test_get_session_status);
    suite.add_test_set("GetEpochStatus", test_get_epoch_status);
    suite.add_test_set("InspectState", test_inspect_state);
    suite.add_test_set("FinishEpoch", test_finish_epoch);
    suite.add_test_set("DeleteEpoch", test_delete_epoch);
    suite.add_test_set("EndSession", test_end_session);
    suite.add_test_set("Session Simulations", test_session_simulations);
    return suite.run();
}

/// \brief Prints help
/// \param name Program name vrom argv[0]
static void help(const char *name) {
    (void) fprintf(stderr,
        R"(Usage:

    %s [-r] [--help] [--http] <manager-address>

where

    <manager-address>
      server manager address, where <manager-address> can be
        <ipv4-hostname/address>:<port>
        <ipv6-hostname/address>:<port>
        unix:<path>

    --help
      prints this message and exits


)",
        name);
}

int main(int argc, char *argv[]) try {
    const char *manager_address = nullptr;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            help(argv[0]);
            exit(0);
        } else {
            manager_address = argv[i];
        }
    }

    if (!manager_address) {
        std::cerr << "missing manager-address\n";
        exit(1);
    }
    return run_tests(manager_address);
} catch (std::exception &e) {
    std::cerr << "Caught exception: " << e.what() << '\n';
    return 1;
} catch (...) {
    std::cerr << "Caught unknown exception\n";
    return 1;
}
