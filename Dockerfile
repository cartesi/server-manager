ARG EMULATOR_REPOSITORY=cartesi/machine-emulator
ARG EMULATOR_TAG=0.16.1
ARG RELEASE=yes

FROM --platform=$TARGETPLATFORM ${EMULATOR_REPOSITORY}:${EMULATOR_TAG} as linux-env

USER root

RUN apt-get update && \
    DEBIAN_FRONTEND="noninteractive" apt-get install --no-install-recommends -y \
    build-essential wget git procps \
    libreadline-dev libboost-coroutine1.81-dev libboost-context1.81-dev \
    libboost-filesystem1.81-dev libboost-log1.81-dev libssl-dev libc-ares-dev zlib1g-dev \
    ca-certificates automake libtool patchelf cmake pkg-config lua5.4 liblua5.4-dev \
    libgrpc++-dev libprotobuf-dev protobuf-compiler-grpc \
    libcrypto++-dev clang-tidy-16 clang-format-16 && \
    update-alternatives --install /usr/bin/clang-format clang-format /usr/bin/clang-format-16 120 && \
    update-alternatives --install /usr/bin/clang-tidy clang-tidy /usr/bin/clang-tidy-16 120 && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /usr/src/server-manager

FROM --platform=$TARGETPLATFORM linux-env as builder

COPY . .

RUN make -j$(nproc) dep && \
    make -j$(nproc) release=$RELEASE

FROM --platform=$TARGETPLATFORM builder as installer

RUN make install

FROM --platform=$TARGETPLATFORM ${EMULATOR_REPOSITORY}:${EMULATOR_TAG}

USER root

RUN apt-get update && DEBIAN_FRONTEND="noninteractive" apt-get install -y \
    libboost-coroutine1.81.0 \
    libboost-context1.81.0 \
    libboost-filesystem1.81.0 \
    libboost-log1.81.0 \
    libcrypto++8 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=installer /usr/bin/server-manager /usr/bin/server-manager

WORKDIR /opt/cartesi

EXPOSE 5001

USER cartesi

CMD ["/usr/bin/server-manager", "--manager-address=0.0.0.0:5001"]
