ARG EMULATOR_REPOSITORY=cartesi/machine-emulator
ARG EMULATOR_TAG=0.15.0
ARG RELEASE=yes

FROM ${EMULATOR_REPOSITORY}:${EMULATOR_TAG} as dep-builder

USER root

RUN apt-get update && \
    DEBIAN_FRONTEND="noninteractive" apt-get install --no-install-recommends -y \
        build-essential wget git \
        libreadline-dev libboost-coroutine-dev libboost-context-dev \
        libboost-filesystem-dev libboost-log-dev libssl-dev libc-ares-dev zlib1g-dev \
        ca-certificates automake libtool patchelf cmake pkg-config lua5.4 liblua5.4-dev \
        libgrpc++-dev libprotobuf-dev protobuf-compiler-grpc \
        libcrypto++-dev clang-tidy-15 clang-format-15 && \
    update-alternatives --install /usr/bin/clang-format clang-format /usr/bin/clang-format-15 120 && \
    update-alternatives --install /usr/bin/clang-tidy clang-tidy /usr/bin/clang-tidy-15 120 && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /usr/src/server-manager

FROM dep-builder as builder

COPY . .

RUN make -j$(nproc) dep && \
    make -j$(nproc) release=$RELEASE

FROM builder as installer

RUN make install

FROM ${EMULATOR_REPOSITORY}:${EMULATOR_TAG}

USER root

RUN apt-get update && DEBIAN_FRONTEND="noninteractive" apt-get install -y \
    libboost-log1.74.0 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=installer /usr/bin/server-manager /usr/bin/server-manager

WORKDIR /opt/cartesi

EXPOSE 5001

USER cartesi

CMD ["/usr/bin/server-manager", "--manager-address=0.0.0.0:5001"]
