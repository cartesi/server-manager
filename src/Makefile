# Copyright Cartesi and individual authors (see AUTHORS)
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

UNAME:=$(shell uname)

BUILDDIR ?= $(abspath ../build/$(UNAME)_$(shell uname -m))

LUA_BIN?=lua5.4
GRPC_DIR:=../lib/grpc-interfaces
HEALTHCHECK_DIR=../third-party

MANAGER_ADDRESS?=127.0.0.1:5001
FAST_TEST?=false

ifeq ($(FAST_TEST),true)
FAST_TEST_FLAG=--fast
endif

# So we can use gcc to check if there are unused symbols
ifeq ($(gc),yes)
GCFLAGS= -fno-inline-small-functions -fno-inline -fdata-sections -ffunction-sections
GCLDFLAGS=-Wl,--gc-sections,--print-gc-sections
endif

PROTOC:=$(shell which protoc)
GRPC_CPP_PLUGIN:=$(shell which grpc_cpp_plugin)

# Mac OS X specific setup
SOLDFLAGS_Darwin:=-bundle -undefined dynamic_lookup
CC_Darwin=clang
CXX_Darwin=clang++
INCS_Darwin=

ifeq ($(UNAME),Darwin)
ifeq ($(MACOSX_DEPLOYMENT_TARGET),)
export MACOSX_DEPLOYMENT_TARGET := $(shell sw_vers -productVersion | sed -r  "s/([[:digit:]]+)\.([[:digit:]]+)\..+/\1.\2.0/")
endif
SOLDFLAGS_Darwin+=-Wl,-rpath,$(BUILDDIR)/lib -Wl,-rpath,$(CURDIR)
# Homebrew installation
ifneq (,$(wildcard /usr/local/opt/boost/lib))
BOOST_LIB_DIR_Darwin=-L/usr/local/opt/boost/lib
BOOST_INC_Darwin=-I/usr/local/opt/boost/include
CRYPTOPP_LIB_Darwin:=-L/usr/local/opt/cryptopp/lib -lcryptopp
CRYPTOPP_INC_Darwin:=-I/usr/local/opt/cryptopp/include
else # Macports installation
BOOST_LIB_DIR_Darwin=-L/opt/local/libexec/boost/1.81/lib
BOOST_INC_Darwin=-I/opt/local/libexec/boost/1.81/include
CRYPTOPP_LIB_Darwin:=-L/opt/local/lib -lcryptopp
CRYPTOPP_INC_Darwin:=-I/opt/local/include
endif
endif

BOOST_CORO_LIB_Darwin:=$(BOOST_LIB_DIR_Darwin) -lboost_coroutine-mt -lboost_context-mt
BOOST_FILESYSTEM_LIB_Darwin:=$(BOOST_LIB_DIR_Darwin) -lboost_system-mt -lboost_filesystem-mt
BOOST_LOG_LIB_Darwin:=$(BOOST_LIB_DIR_Darwin) -lboost_log-mt -lboost_log_setup-mt -lboost_thread-mt
BOOST_PROCESS_LIB_Darwin:=-lpthread
GRPC_INC_Darwin=-I$(dir $(GRPC_CPP_PLUGIN))../include
GRPC_LIB_Darwin=-L$(dir $(GRPC_CPP_PLUGIN))../lib -lgrpc++ -lgrpc -lgpr -lprotobuf -lpthread -labsl_synchronization
PROTOBUF_LIB_Darwin:=-L/opt/local/lib -lprotobuf -lpthread
CARTESI_EXECUTABLE_LDFLAGS_Darwin=-Wl,-rpath,$(BUILDDIR)/lib -Wl,-rpath,$(CURDIR)
PROFILE_DATA_Darwin=default.profdata

# Linux specific setup
SOLDFLAGS_Linux:=-shared -fPIC -pthread
CC_Linux=gcc
CXX_Linux=g++
INCS_Linux=
BOOST_CORO_LIB_Linux:=-lboost_coroutine -lboost_context
BOOST_FILESYSTEM_LIB_Linux:=-lboost_system -lboost_filesystem
BOOST_LOG_LIB_Linux:=-lboost_log -lboost_log_setup -lboost_thread
BOOST_PROCESS_LIB_Linux:=-lpthread
BOOST_INC_Linux=
CRYPTOPP_LIB_Linux:=-lcryptopp
CRYPTOPP_INC_Linux:=
GRPC_INC_Linux:=-I$(BUILDDIR)/include
GRPC_LIB_Linux:=-L$(BUILDDIR)/lib -lgrpc++ -lgrpc -lgpr -lprotobuf -lpthread -labsl_synchronization
PROTOBUF_LIB_Linux:=-L$(BUILDDIR)/lib -lprotobuf -lpthread
CARTESI_EXECUTABLE_LDFLAGS_Linux=-Wl,-rpath,'$$ORIGIN/' -Wl,--copy-dt-needed-entries
PROFILE_DATA_Linux=

CC=$(CC_$(UNAME))
CXX=$(CXX_$(UNAME))
CC_MARCH=
SOLDFLAGS:=$(SOLDFLAGS_$(UNAME)) $(GCLDFLAGS)
BOOST_CORO_LIB=$(BOOST_CORO_LIB_$(UNAME))
BOOST_FILESYSTEM_LIB=$(BOOST_FILESYSTEM_LIB_$(UNAME))
BOOST_LOG_LIB=$(BOOST_LOG_LIB_$(UNAME))
BOOST_PROCESS_LIB=$(BOOST_PROCESS_LIB_$(UNAME))
BOOST_INC=$(BOOST_INC_$(UNAME))
CRYPTOPP_LIB=$(CRYPTOPP_LIB_$(UNAME))
CRYPTOPP_INC=$(CRYPTOPP_INC_$(UNAME))
GRPC_LIB=$(GRPC_LIB_$(UNAME))
GRPC_INC=$(GRPC_INC_$(UNAME))
PROTOBUF_LIB=$(PROTOBUF_LIB_$(UNAME))
CARTESI_EXECUTABLE_LDFLAGS=$(CARTESI_EXECUTABLE_LDFLAGS_$(UNAME))

SERVER_MANAGER_LIBS:=$(CRYPTOPP_LIB) $(GRPC_LIB) $(BOOST_CORO_LIB) $(BOOST_LOG_LIB) -ldl
TEST_SERVER_MANAGER_LIBS:=$(CRYPTOPP_LIB) $(GRPC_LIB) -ldl

WARNS=-W -Wall -pedantic

# Place our include directories before the system's
INCS= -I../lib/machine-emulator-defines $(CRYPTOPP_INC) $(BOOST_INC) $(GRPC_INC) $(INCS_$(UNAME))

ifeq ($(relwithdebinfo),yes)
OPTFLAGS+=-O2 -g
else ifeq ($(release),yes)
OPTFLAGS+=-O2
else ifeq ($(debug),yes)
OPTFLAGS+=-Og -g
else ifeq ($(sanitize),yes)
OPTFLAGS+=-Og -g
else
OPTFLAGS+=-O2 -g
endif

# Optimization flags
ifneq (,$(filter yes,$(relwithdebinfo) $(release)))
DEFS+=-DNDEBUG
# Disable jump tables, because it degrades the instruction decoding performance in the interpret loop,
# since it generates a memory indirection that has a high cost in opcode switches.
OPTFLAGS+=-fno-jump-tables
ifneq (,$(filter gcc,$(CC)))
# The following flag helps GCC to eliminate more redundant computations in the interpret loop,
# saving some host instructions and improving performance.
# This flag is usually enabled by default at -O3,
# but we don't use -O3 because it enables some other flags that are not worth for the interpreter.
OPTFLAGS+=-fgcse-after-reload -fpredictive-commoning -fsplit-paths -ftree-partial-pre
endif

endif
# Link time optimizations
ifeq ($(lto),yes)
OPTFLAGS+=-flto=auto
endif

ifeq ($(sanitize),yes)
# Enable address and undefined sanitizers
UBFLAGS+=-fsanitize=address,undefined -fno-sanitize-recover=all
else
# Flags to minimize undefined behavior
UBFLAGS+=-fno-strict-aliasing
UBFLAGS+=-fno-strict-overflow
UBFLAGS+=-fno-delete-null-pointer-checks
endif

# Compile for current system processor
ifeq ($(native),yes)
CC_MARCH=-march=native
else
CC_MARCH=
endif

# We ignore xkcp-keccak-256-hasher.cpp because it is missing a header file.
# The file is not being compiled but we want to keep it for reference.
LINTER_IGNORE_SOURCES=xkcp-keccak-256-hasher.cpp
LINTER_IGNORE_HEADERS=%.pb.h xkcp-keccak-256-hasher.h
LINTER_SOURCES=$(filter-out $(LINTER_IGNORE_SOURCES),$(strip $(wildcard *.cpp) $(wildcard *.c)))
LINTER_HEADERS=$(filter-out $(LINTER_IGNORE_HEADERS),$(strip $(wildcard *.hpp) $(wildcard *.h)))

CLANG_TIDY=clang-tidy
CLANG_TIDY_TARGETS=$(patsubst %.cpp,%.clang-tidy,$(patsubst %.c,%.clang-tidy,$(LINTER_SOURCES)))

CLANG_FORMAT=clang-format
CLANG_FORMAT_FILES:=$(wildcard *.cpp) $(wildcard *.c) $(wildcard *.h) $(wildcard *.hpp)
CLANG_FORMAT_FILES:=$(filter-out %.pb.h,$(strip $(CLANG_FORMAT_FILES)))

EMPTY:=
SPACE:=$(EMPTY) $(EMPTY)
CLANG_TIDY_HEADER_FILTER=$(PWD)/($(subst $(SPACE),|,$(LINTER_HEADERS)))

CXXFLAGS+=$(OPTFLAGS) -std=c++17 -fvisibility=hidden -fPIC -MMD $(CC_MARCH) $(INCS) $(GCFLAGS) $(UBFLAGS) $(DEFS) $(WARNS) $(MYCFLAGS)
CFLAGS+=$(OPTFLAGS) -std=c99 -fvisibility=hidden -fPIC -MMD $(CC_MARCH) $(INCS) $(GCFLAGS) $(UBFLAGS) $(DEFS) $(WARNS) $(MYCFLAGS)
LDFLAGS+=$(UBFLAGS) $(MYLDFLAGS)

ifeq ($(coverage-toolchain),gcc)
CC=gcc
CXX=g++
CXXFLAGS += -g -O0 --coverage
SOLDFLAGS += --coverage
SERVER_MANAGER_LIBS += --coverage
TEST_SERVER_MANAGER_LIBS += --coverage
else ifeq ($(coverage-toolchain),clang)
CC=clang
CXX=clang++
CXXFLAGS += -g -O0 -fprofile-instr-generate -fcoverage-mapping
SOLDFLAGS += -fprofile-instr-generate -fcoverage-mapping
SERVER_MANAGER_LIBS += -fprofile-instr-generate -fcoverage-mapping
TEST_SERVER_MANAGER_LIBS += -fprofile-instr-generate -fcoverage-mapping
COVERAGE_SOURCES = $(filter-out %.pb.h, $(wildcard *.h) $(wildcard *.cpp))
export LLVM_PROFILE_FILE=coverage-%p.profraw
else ifneq ($(coverage-toolchain),)
$(error invalid value for coverage-toolchain: $(coverage-toolchain))
endif

all: server-manager test-server-manager

.PHONY: all generate use clean test lint format check-format compile_flags.txt

ifeq ($(gperf),yes)
DEFS+=-DGPERF
endif

CREATE_MACHINES_OPTS ?=
ifeq ($(rollup_init),yes)
CREATE_MACHINES_OPTS += --rollup-init
endif

lint: $(CLANG_TIDY_TARGETS)

format:
	@$(CLANG_FORMAT) -i $(CLANG_FORMAT_FILES)

check-format:
	@$(CLANG_FORMAT) -Werror --dry-run $(CLANG_FORMAT_FILES)

generate: CXXFLAGS += -fprofile-generate
generate: SOLDFLAGS += -fprofile-generate
generate: server-manager

$(PROFILE_DATA_Darwin):
	llvm-profdata merge -output=default.profdata default*.profraw

use: CXXFLAGS += -fprofile-use
use: SOLDFLAGS += -fprofile-use
use: $(PROFILE_DATA_$(UNAME))

compile_flags.txt:
	@echo "$(CXXFLAGS)" "-xc++" | sed -e $$'s/ \{1,\}/\\\n/g' | grep -v "MMD" > $@

/tmp/server-manager-root/tests:
	$(info Test machines not found! Please run 'make create-machines' first.)
	@exit 1

create-machines:
	@./create-machines.lua $(CREATE_MACHINES_OPTS)

clean-test-processes:
	@pkill -f server-manager || true
	@pkill -f remote-cartesi-machine || true

test: /tmp/server-manager-root/tests
	@trap 'make clean-test-processes && echo "\nClean up test execution." && exit 130' INT; \
	(./server-manager --manager-address=127.0.0.1:5001 >server-manager.log 2>&1 &); \
	(bash -c 'count=0; while ! echo >/dev/tcp/127.0.0.1/5001 ; do sleep 1; count=$$((count+1)); if [[ $$count -eq 20 ]]; then exit 1; fi; done' > /dev/null 2>&1); \
	./test-server-manager $(FAST_TEST_FLAG) 127.0.0.1:5001
	@make clean-test-processes

create-and-test: create-machines
	$(MAKE) test

run-test-server-manager:
	./test-server-manager $(FAST_TEST_FLAG) $(MANAGER_ADDRESS)

CARTESI_PROTOBUF_GEN_OBJS:= \
	versioning.pb.o \
	cartesi-machine.pb.o \
	cartesi-machine-checkin.pb.o

CARTESI_GRPC_GEN_OBJS:= \
	cartesi-machine.grpc.pb.o \
	cartesi-machine-checkin.grpc.pb.o

SERVER_MANAGER_PROTO_OBJS:= \
	server-manager.pb.o \
	server-manager.grpc.pb.o

HEALTHCHECK_PROTO_OBJS:= \
	health.pb.o \
	health.grpc.pb.o

PROTO_OBJS:= \
	$(CARTESI_PROTOBUF_GEN_OBJS) \
	$(CARTESI_GRPC_GEN_OBJS) \
	$(SERVER_MANAGER_PROTO_OBJS) \
	$(HEALTHCHECK_PROTO_OBJS)

$(PROTO_OBJS): CXXFLAGS +=  -Wno-zero-length-array -Wno-unused-parameter -Wno-deprecated-declarations -Wno-deprecated-copy -Wno-type-limits

PROTO_SOURCES:=$(PROTO_OBJS:.o=.cc)

$(PROTO_OBJS): cartesi-machine.pb.h versioning.pb.h cartesi-machine-checkin.pb.h server-manager.pb.h health.pb.h

SERVER_MANAGER_OBJS:= \
	$(CARTESI_PROTOBUF_GEN_OBJS) \
	$(CARTESI_GRPC_GEN_OBJS) \
	$(SERVER_MANAGER_PROTO_OBJS) \
	$(HEALTHCHECK_PROTO_OBJS) \
	complete-merkle-tree.o \
	pristine-merkle-tree.o \
	protobuf-util.o \
	server-manager.o

TEST_SERVER_MANAGER_OBJS:= \
	$(CARTESI_PROTOBUF_GEN_OBJS) \
	$(CARTESI_GRPC_GEN_OBJS) \
	$(SERVER_MANAGER_PROTO_OBJS) \
	$(HEALTHCHECK_PROTO_OBJS) \
	complete-merkle-tree.o \
	pristine-merkle-tree.o \
	protobuf-util.o \
	test-server-manager.o

protobuf-util.o: $(CARTESI_PROTOBUF_GEN_OBJS)

test-server-manager.o: $(PROTO_OBJS)

grpc-interfaces: $(PROTO_SOURCES)

server-manager: $(SERVER_MANAGER_OBJS)
	$(CXX) $(LDFLAGS) $(CARTESI_EXECUTABLE_LDFLAGS) -o $@ $(SERVER_MANAGER_OBJS) $(SERVER_MANAGER_LIBS)

test-server-manager: $(TEST_SERVER_MANAGER_OBJS)
	$(CXX) $(LDFLAGS) $(CARTESI_EXECUTABLE_LDFLAGS) -o $@ $(TEST_SERVER_MANAGER_OBJS) $(TEST_SERVER_MANAGER_LIBS)

.PRECIOUS: %.grpc.pb.cc %.grpc.pb.h %.pb.cc %.pb.h

%.grpc.pb.cc: $(GRPC_DIR)/%.proto
	$(PROTOC) -I$(<D) --grpc_out=. --plugin=protoc-gen-grpc=$(GRPC_CPP_PLUGIN) $<

%.pb.cc %.pb.h: $(GRPC_DIR)/%.proto
	$(PROTOC) -I$(GRPC_DIR) --cpp_out=. $<

%.grpc.pb.cc: $(HEALTHCHECK_DIR)/%.proto
	$(PROTOC) -I$(HEALTHCHECK_DIR) --grpc_out=. --plugin=protoc-gen-grpc=$(GRPC_CPP_PLUGIN) $<

%.pb.cc %.pb.h: $(HEALTHCHECK_DIR)/%.proto
	$(PROTOC) -I$(HEALTHCHECK_DIR) --cpp_out=. $<

%.clang-tidy: %.cpp $(PROTO_SOURCES)
	@$(CLANG_TIDY) --header-filter='$(CLANG_TIDY_HEADER_FILTER)' $< -- $(CXXFLAGS) 2>/dev/null
	@$(CXX) $(CXXFLAGS) $< -MM -MT $@ -MF $@.d > /dev/null 2>&1
	@touch $@

%.clang-tidy: %.c $(PROTO_SOURCES)
	@$(CLANG_TIDY) --header-filter='$(CLANG_TIDY_HEADER_FILTER)' $< -- $(CFLAGS) 2>/dev/null
	@$(CC) $(CFLAGS) $< -MM -MT $@ -MF $@.d > /dev/null 2>&1
	@touch $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

%.o: %.cc
	$(CXX) $(CXXFLAGS) -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean: clean-objs clean-coverage clean-profile clean-proto-sources clean-tidy clean-executables clean-test

clean-proto-sources:
	@rm -f *.pb.cc *.pb.h

clean-tidy:
	@rm -f *.clang-tidy

clean-objs:
	@rm -f *.o *.d

clean-executables:
	@rm -f server-manager

clean-test:
	@rm -f test-server-manager

clean-machines:
	@rm -rf /tmp/server-manager-root

clean-coverage:
	@rm -f *.profdata *.profraw *.gcda *.gcov coverage.info coverage.txt
	@rm -rf coverage-gcc coverage-clang

clean-profile:
	@rm -f *.profdata *.profraw *.gcda *.gcno

-include $(wildcard *.d)
