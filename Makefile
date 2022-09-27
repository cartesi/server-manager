UNAME:=$(shell uname)

# Install settings
PREFIX= /opt/cartesi
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
BUILDBASE := $(abspath build)
BUILDDIR = $(BUILDBASE)/$(UNAME)_$(shell uname -m)
SUBCLEAN := $(addsuffix .clean,$(SRCDIR))
DEPDIRS := $(addprefix $(DEPDIR)/,grpc)
DEPCLEAN := $(addsuffix .clean,$(DEPDIRS))
SERVER_MANAGER_PROTO := lib/grpc-interfaces/server-manager.proto
GRPC_VERSION ?= v1.50.0

# Docker image settings
TAG ?= devel
EMULATOR_TAG ?= 0.14.0
EMULATOR_REPOSITORY ?= cartesi/machine-emulator

# Mac OS X specific settings
ifeq ($(UNAME),Darwin)
LUA_PLAT ?= macosx
export CC = clang
export CXX = clang++
LIBRARY_PATH := "export DYLD_LIBRARY_PATH=$(BUILDDIR)/lib"
LIB_EXTENSION = dylib
DEP_TO_LIB += *.$(LIB_EXTENSION)

# Linux specific settings
else ifeq ($(UNAME),Linux)
LIBRARY_PATH := "export LD_LIBRARY_PATH=$(BUILDDIR)/lib:$(SRCDIR)"
LIB_EXTENSION := so
DEP_TO_LIB += *.$(LIB_EXTENSION)*
# Unknown platform
else
LIB_EXTENSION := dll
DEP_TO_LIB += *.$(LIB_EXTENSION)
endif

# Check if some binary dependencies already exists on build directory to skip
# downloading and building them.
DEPBINS := $(addprefix $(BUILDDIR)/,lib/libgrpc.$(LIB_EXTENSION))

all: source-default

env:
	@echo $(LIBRARY_PATH)
	@echo "export PATH='$(SRCDIR):$(BUILDDIR)/bin:/opt/cartesi/bin:${PATH}'"
	@echo "export LUA_CPATH='./?.so;$(SRCDIR)/?.so;/opt/cartesi/lib/lua/5.3/?.so;$$(lua5.3 -e 'print(package.cpath)')'"
	@echo "export LUA_PATH='./?.lua;$(SRCDIR)/?.lua;/opt/cartesi/share/lua/5.3/?.lua;$$(lua5.3 -e 'print(package.path)')'"

doc:
	cd doc && doxygen Doxyfile

help:
	@echo 'Common targets:'
	@echo '* all                        - build the src/ code. To build from a clean clone, run: make submodules dep all'
	@echo '  submodules                 - initialize and update submodules'
	@echo '  dep                        - build dependencies'
	@echo '  create-machines            - create machines for the server-manager tests'
	@echo '  test                       - run server-manager tests'
	@echo '  create-and-test            - create machines for the server-manager tests'
	@echo '  doc                        - build the doxygen documentation (requires doxygen to be installed)'
	@echo 'Docker targets:'
	@echo '  image                      - Build the server-manager docker image'
	@echo 'Cleaning targets:'
	@echo '  clean                      - clean the src/ artifacts'
	@echo '  depclean                   - clean + dependencies'
	@echo '  distclean                  - depclean + profile information'
	@echo '  clean-machines             - clean machines created for the server-manager tests'

$(BUILDDIR) $(BIN_INSTALL_PATH) $(LIB_INSTALL_PATH) $(IMAGES_INSTALL_PATH):
	mkdir -m 0755 -p $@

dep: $(DEPBINS)
	@rm -f $(BUILDDIR)/lib/*.a
	@$(STRIP_EXEC) \
		$(BUILDDIR)/bin/grpc* \
		$(BUILDDIR)/bin/protoc* \
		$(BUILDDIR)/lib/*.$(LIB_EXTENSION)*

submodules:
	git submodule update --init --recursive

$(SERVER_MANAGER_PROTO):
	$(info gprc-interfaces submodule not initialized!)
	@exit 1

test server-manager: | $(SERVER_MANAGER_PROTO)
test lint coverage-report check-format format server-manager create-machines create-and-test clean-machines run-test-server-manager:
	@eval $$($(MAKE) -s --no-print-directory env); $(MAKE) -C $(SRCDIR) $@

source-default:
	@eval $$($(MAKE) -s --no-print-directory env); $(MAKE) -C $(SRCDIR)

image:
	docker build -t cartesi/server-manager:$(TAG) -f Dockerfile --build-arg EMULATOR_REPOSITORY=$(EMULATOR_REPOSITORY) --build-arg EMULATOR_TAG=$(EMULATOR_TAG) .

installer-stage-image:
	docker build --target installer -t cartesi/server-manager:installer -f Dockerfile --build-arg EMULATOR_REPOSITORY=$(EMULATOR_REPOSITORY) --build-arg EMULATOR_TAG=$(EMULATOR_TAG) .

$(DEPDIR)/grpc $(BUILDDIR)/lib/libgrpc.$(LIB_EXTENSION): | $(BUILDDIR)
	if [ ! -d $(DEPDIR)/grpc ]; then git clone --branch $(GRPC_VERSION) --depth 1 https://github.com/grpc/grpc.git $(DEPDIR)/grpc; fi
	cd $(DEPDIR)/grpc && git submodule update --init --recursive --depth 1
	mkdir -p $(DEPDIR)/grpc/cmake/build && cd $(DEPDIR)/grpc/cmake/build && cmake -C $(abspath $(DEPDIR))/grpc.cmake -DCMAKE_INSTALL_PREFIX=$(BUILDDIR) ../..
	$(MAKE) -C $(DEPDIR)/grpc/cmake/build all install
	mkdir -p $(BUILDDIR)/share/grpc/health/v1/ && cp -a $(DEPDIR)/grpc/src/proto/grpc/health/v1/health.proto $(BUILDDIR)/share/grpc/health/v1/
	if [ "$(UNAME)" = "Darwin" ]; then install_name_tool -add_rpath @loader_path/../lib $(BUILDDIR)/bin/grpc_cpp_plugin; fi

$(SUBCLEAN) $(DEPCLEAN): %.clean:
	$(MAKE) -C $* clean

clean: $(SUBCLEAN)

depclean: $(DEPCLEAN) clean
	rm -rf $(BUILDDIR)

distclean: clean clean-profile
	rm -rf $(BUILDBASE) $(DEPDIRS)

clean-profile:
	$(MAKE) -C $(SRCDIR) $@

install-Darwin: install-strip
	cd $(BIN_INSTALL_PATH) && \
		for x in $(SERVER_MANAGER_TO_BIN); do \
			install_name_tool -delete_rpath $(BUILDDIR)/lib -delete_rpath $(SRCDIR) -add_rpath $(LIB_INSTALL_PATH) $$x ;\
		done

install-Linux: install-strip
	cd $(BIN_INSTALL_PATH) && for x in $(SERVER_MANAGER_TO_BIN); do patchelf --set-rpath $(LIB_INSTALL_PATH) $$x ; done

install-binary: $(BIN_INSTALL_PATH)
	cd src && $(INSTALL) $(SERVER_MANAGER_TO_BIN) $(BIN_INSTALL_PATH)
	cd $(BIN_INSTALL_PATH) && $(CHMOD_EXEC) $(SERVER_MANAGER_TO_BIN)

install-strip: install-binary
	cd $(BIN_INSTALL_PATH) && $(STRIP_EXEC) $(SERVER_MANAGER_TO_BIN)

install: $(INSTALL_PLAT)

.SECONDARY: $(DEPDIRS) $(SERVER_MANAGER_PROTO)

.PHONY: help all submodules doc clean distclean clean-profile downloads src test \
	$(SUBDIRS) $(SUBCLEAN) $(DEPCLEAN)
