# Cartesi Server-Manager

The Cartesi Server-Manager is a microservice that serves as an interface between the Remote Cartesi Machine (RCM) and other components of the Cartesi Rollup node. It is designed to simplify the communication between the RCM and the rest of the Cartesi node. It's written in C/C++ with POSIX dependencies restricted to the terminal and process facilites. Currently, it is distributed as a docker image.

## Getting Started

Run `make help` for a list of target options. Here are some of them:

```
Common targets:
* all                        - build the src/ code. To build from a clean clone, run: make submodules dep all
  submodules                 - initialize and update submodules
  dep                        - build dependencies
  create-machines            - create machines for the server-manager tests
  test                       - run server-manager tests
  create-and-test            - create machines for the server-manager tests
  doc                        - build the doxygen documentation (requires doxygen to be installed)
Docker targets:
  image                      - Build the server-manager docker image
Cleaning targets:
  clean                      - clean the src/ artifacts
  depclean                   - clean + dependencies
  distclean                  - depclean + profile information
  clean-machines             - clean machines created for the server-manager tests
```

### Requirements

- C++ Compiler with support for C++17 (tested with GCC >= 8+ and Clang >= 8.x).
- GNU Make >= 3.81
- GRPC 1.50.0
- Lua 5.3.5
- Boost >= 1.71

Obs: Please note that Apple Clang Version number does not follow upstream LLVM/Clang.

#### Ubuntu 22.04

```shell
sudo apt-get install build-essential automake libtool patchelf cmake pkg-config wget git libreadline-dev libboost-coroutine-dev libboost-context-dev libboost-filesystem-dev libssl-dev openssl libc-ares-dev zlib1g-dev ca-certificates liblua5.3-dev
```

#### MacOS

##### MacPorts

```shell
sudo port install clang-14 automake boost libtool wget cmake pkgconfig c-ares zlib openssl lua
```

##### Homebrew

```shell
brew install llvm@14 automake boost libomp wget cmake pkg-config c-ares zlib openssl lua@5.3
```

For `create-machines.lua` script to work it is expected that `lua5.3` binary is available in the system PATH. If operating system/package manager that you are using provides only `lua` or lua binary named in a different way (e.g. on `Homebrew`), please create symbolic link or alias `lua5.3`.

### Build

```shell
make submodules
make dep
make
```

Cleaning:

```shell
make depclean
make clean
```

### Running Tests

In order to run the tests for the Cartesi Server-Manager, it is essential to have a fully functional SDK installation located at `/opt/cartesi`. This installation enables the creation of test machines and the execution of the Remote Cartesi Machine. To create the test machines, use the following command:

```shell
make create-machines
```

Once the test machines have been created, the Cartesi Server-Manager tests can be executed using the following command:

```shell
make test
```

### Install

```shell
make install
```

## Linter

We use clang-tidy 14 as the linter.

### Linter install

#### Ubuntu 22.04

You need to install the package clang-tidy-14 and set it as the default executable with update-alternatives.

```shell
apt install clang-tidy-14
update-alternatives --install /usr/bin/clang-tidy clang-tidy /usr/bin/clang-tidy-14 120
```

### Running Lint

```shell
make lint -j$(nproc)
```

## Code format

We use clang-format to format the code base.

### Formatter install

#### Ubuntu

You need to install the package clang-format-14 and set is as the default executable with update-alternatives.

```shell
apt install clang-format-14
update-alternatives --install /usr/bin/clang-format clang-format /usr/bin/clang-format-14 120
```

### Formatting code

```shell
make format
```

### Checking whether the code is formatted

```shell
make check-format
```

## Contributing

Thank you for your interest in Cartesi! Head over to our [Contributing Guidelines](CONTRIBUTING.md) for instructions on how to sign our Contributors Agreement and get started with
Cartesi!

Please note we have a [Code of Conduct](CODE_OF_CONDUCT.md), please follow it in all your interactions with the project.

## License

The server-manager repository and all contributions are licensed under
[APACHE 2.0](https://www.apache.org/licenses/LICENSE-2.0). Please review our [LICENSE](LICENSE) file.
