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

# Install settings
PREFIX= /usr
BIN_INSTALL_PATH= $(PREFIX)/bin
LIB_INSTALL_PATH= $(PREFIX)/lib
INSTALL_PLAT = install-$(UNAME)

INSTALL= cp -RP
CHMOD_EXEC= chmod 0755
STRIP_EXEC= strip -x

SERVER_MANAGER_TO_BIN= server-manager

# Build settings
DEPDIR := third-party
SRCDIR := $(abspath src)
SUBCLEAN := $(addsuffix .clean,$(SRCDIR))
SERVER_MANAGER_PROTO := lib/grpc-interfaces/server-manager.proto
HEALTHCHECK_PROTO := third-party/health.proto

# Docker image settings
TAG ?= lambadex
EMULATOR_TAG ?= lambadex
EMULATOR_REPOSITORY ?= cartesi/machine-emulator

# Mac OS X specific settings
ifeq ($(UNAME),Darwin)
LUA_PLAT ?= macosx
export CC = clang
export CXX = clang++
LIB_EXTENSION = dylib

# Linux specific settings
else ifeq ($(UNAME),Linux)
LIB_EXTENSION := so
# Unknown platform
else
LIB_EXTENSION := dll
endif

# Test settings
MANAGER_ADDRESS?=127.0.0.1:5001
FAST_TEST?=false
export MANAGER_ADDRESS
export FAST_TEST

all: source-default

env:
	@echo $(LIBRARY_PATH)
	@echo "export PATH='$(SRCDIR):$(PREFIX)/bin:${PATH}'"
	@echo "export LUA_PATH_5_4='$(SRCDIR)/?.lua;$(PREFIX)/share/lua/5.4/?.lua;$${LUA_PATH_5_4:-;}'"
	@echo "export LUA_CPATH_5_4='$(SRCDIR)/?.so;$(PREFIX)/lib/lua/5.4/?.so;$${LUA_CPATH_5_4:-;}'"

doc:
	cd doc && doxygen Doxyfile

help:
	@echo 'Common targets:'
	@echo '* all                        - build the src/ code. To build from a clean clone, run: make submodules all'
	@echo '  submodules                 - initialize and update submodules'
	@echo '  create-machines            - create machines for the server-manager tests'
	@echo '  test                       - run server-manager tests'
	@echo '  create-and-test            - create machines for the server-manager tests'
	@echo '  doc                        - build the doxygen documentation (requires doxygen to be installed)'
	@echo 'Docker targets:'
	@echo '  image                      - Build the server-manager docker image'
	@echo 'Cleaning targets:'
	@echo '  clean                      - clean the src/ artifacts'
	@echo '  distclean                  - clean + profile information'
	@echo '  clean-machines             - clean machines created for the server-manager tests'

$(BIN_INSTALL_PATH) $(LIB_INSTALL_PATH) $(IMAGES_INSTALL_PATH) $(DEPDIR):
	mkdir -m 0755 -p $@

$(HEALTHCHECK_PROTO):
	@wget -nc -i dependencies -P $(DEPDIR)

dep: $(HEALTHCHECK_PROTO)

submodules:
	git submodule update --init --recursive

shasumfile: $(HEALTHCHECK_PROTO)
	@shasum -a 256 $^ > $@

checksum: $(HEALTHCHECK_PROTO)
	@shasum -ca 256 shasumfile

$(SERVER_MANAGER_PROTO):
	$(info gprc-interfaces submodule not initialized!)
	@exit 1

test server-manager: | $(SERVER_MANAGER_PROTO) $(HEALTHCHECK_PROTO)
test lint coverage-report check-format format server-manager create-machines create-and-test clean-machines run-test-server-manager:
	@eval $$($(MAKE) -s --no-print-directory env); $(MAKE) -C $(SRCDIR) $@

source-default: | $(SERVER_MANAGER_PROTO) checksum
	@eval $$($(MAKE) -s --no-print-directory env); $(MAKE) -C $(SRCDIR)

image:
	docker build -t cartesi/server-manager:$(TAG) -f Dockerfile --build-arg EMULATOR_REPOSITORY=$(EMULATOR_REPOSITORY) --build-arg EMULATOR_TAG=$(EMULATOR_TAG) .

installer-stage-image:
	docker build --target installer -t cartesi/server-manager:installer -f Dockerfile --build-arg EMULATOR_REPOSITORY=$(EMULATOR_REPOSITORY) --build-arg EMULATOR_TAG=$(EMULATOR_TAG) .

$(SUBCLEAN): %.clean:
	$(MAKE) -C $* clean

clean: $(SUBCLEAN)

distclean: clean clean-profile
	rm -rf $(DEPDIR)

clean-profile:
	$(MAKE) -C $(SRCDIR) $@

install-Darwin: install-strip
	cd $(BIN_INSTALL_PATH) && \
		for x in $(SERVER_MANAGER_TO_BIN); do \
			install_name_tool -delete_rpath $(SRCDIR) -add_rpath $(LIB_INSTALL_PATH) $$x ;\
		done

install-Linux: install-strip
	cd $(BIN_INSTALL_PATH) && for x in $(SERVER_MANAGER_TO_BIN); do patchelf --set-rpath $(LIB_INSTALL_PATH) $$x ; done

install-binary: $(BIN_INSTALL_PATH)
	cd src && $(INSTALL) $(SERVER_MANAGER_TO_BIN) $(BIN_INSTALL_PATH)
	cd $(BIN_INSTALL_PATH) && $(CHMOD_EXEC) $(SERVER_MANAGER_TO_BIN)

install-strip: install-binary
	cd $(BIN_INSTALL_PATH) && $(STRIP_EXEC) $(SERVER_MANAGER_TO_BIN)

install: $(INSTALL_PLAT)

.SECONDARY: $(SERVER_MANAGER_PROTO)

.PHONY: help all submodules doc clean distclean clean-profile src test shasumfile checksum \
	$(SUBDIRS) $(SUBCLEAN)
