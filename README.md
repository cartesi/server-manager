# Cartesi Server-Manager

The Cartesi Server-Manager is a microservice that serves as an interface between the Remote Cartesi Machine (RCM) and other components of the Cartesi Rollup node. It is designed to simplify the communication between the RCM and the rest of the Cartesi node. It's written in C/C++ with POSIX dependencies restricted to the terminal and process facilities. Currently, it is distributed as a docker image.

## Getting Started

Run `make help` for a list of target options. Here are some of them:

```
Common targets:
* all                        - build the src/ code. To build from a clean clone, run: make submodules dep all
  submodules                 - initialize and update submodules
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
- Lua 5.4.x
- Boost >= 1.81

Obs: Please note that Apple Clang Version number does not follow upstream LLVM/Clang.

#### Debian Bookworm

```
sudo apt-get install build-essential wget git libreadline-dev libboost-coroutine-dev libboost-context-dev libboost-filesystem-dev libboost-log-dev libssl-dev libc-ares-dev zlib1g-dev ca-certificates automake libtool patchelf cmake pkg-config lua5.4 liblua5.4-dev libgrpc++-dev libprotobuf-dev protobuf-compiler-grpc libcrypto++-dev
```
#### MacOS

##### MacPorts
```
sudo port install clang-16 automake boost libtool wget cmake pkgconfig c-ares zlib openssl lua54 grpc
```

For `create-machines.lua` script to work it is expected that `lua5.4` binary is available in the system PATH. If operating system/package manager that you are using provides only `lua` or lua binary named in a different way, please create symbolic link or alias `lua5.4`.

### Build

```bash
$ make submodules
$ make dep
$ make
```

Cleaning:

```bash
$ make depclean
$ make clean
```

### Running Tests

In order to run the tests for the Cartesi Server-Manager, it is essential to have a fully functional SDK installation with the binaries on the path. This installation enables the creation of test machines and the execution of the Remote Cartesi Machine. To create the test machines, use the following command:

```bash
$ make create-machines
```

Once the test machines have been created, the Cartesi Server-Manager tests can be executed using the following command:

```bash
$ make test
```

### Install

```bash
$ make install
```

## Linter

We use clang-tidy 15 as the linter.

### Linter install

#### Debian Bookworm

You need to install the package clang-tidy-16 and set it as the default executable with update-alternatives.

```bash
$ apt install clang-tidy-16
$ update-alternatives --install /usr/bin/clang-tidy clang-tidy /usr/bin/clang-tidy-16 120
```

### Running Lint

```bash
$ make lint -j$(nproc)
```

## Code format

We use clang-format to format the code base.

### Formatter install

#### Debian Bookworm

You need to install the package clang-format-16 and set is as the default executable with update-alternatives.

```bash
$ apt install clang-format-16
$ update-alternatives --install /usr/bin/clang-format clang-format /usr/bin/clang-format-16 120
```

### Formatting code

```bash
$ make format
```

### Checking whether the code is formatted

```bash
$ make check-format
```

## Contributing

Thank you for your interest in Cartesi! Head over to our [Contributing Guidelines](CONTRIBUTING.md) for instructions on how to sign our Contributors Agreement and get started with
Cartesi!

Please note we have a [Code of Conduct](CODE_OF_CONDUCT.md), please follow it in all your interactions with the project.

## License

The server-manager repository and all contributions are licensed under
[APACHE 2.0](https://www.apache.org/licenses/LICENSE-2.0). Please review our [LICENSE](LICENSE) file.
