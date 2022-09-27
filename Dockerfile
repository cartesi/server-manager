ARG EMULATOR_REPOSITORY=cartesi/machine-emulator
ARG EMULATOR_TAG=0.14.0
ARG RELEASE=yes

FROM ${EMULATOR_REPOSITORY}:${EMULATOR_TAG} as dep-builder

RUN apt-get update && \
    DEBIAN_FRONTEND="noninteractive" apt-get install --no-install-recommends -y \
        build-essential wget git \
        libreadline-dev libboost-coroutine-dev libboost-context-dev \
        libboost-filesystem-dev libboost-log-dev libssl-dev libc-ares-dev zlib1g-dev \
        ca-certificates automake libtool patchelf cmake pkg-config lua5.3 liblua5.3-dev \
        libcrypto++-dev clang-tidy-14 clang-format-14 && \
    update-alternatives --install /usr/bin/clang-format clang-format /usr/bin/clang-format-14 120 && \
    update-alternatives --install /usr/bin/clang-tidy clang-tidy /usr/bin/clang-tidy-14 120 && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /usr/src/server-manager

COPY Makefile .
COPY third-party third-party

RUN make -j$(nproc) dep

FROM dep-builder as builder

COPY . .

RUN make -j$(nproc) release=$RELEASE

FROM builder as installer

RUN make install

# Add cartesi user to use this image stage on tests
RUN addgroup --system --gid 102 cartesi && \
    adduser --system --uid 102 --ingroup cartesi --disabled-login --no-create-home --home /nonexistent --gecos "cartesi user" --shell /bin/false cartesi

FROM ${EMULATOR_REPOSITORY}:${EMULATOR_TAG}

COPY --from=installer /opt/cartesi/bin/server-manager /opt/cartesi/bin/server-manager

RUN addgroup --system --gid 102 cartesi && \
    adduser --system --uid 102 --ingroup cartesi --disabled-login --no-create-home --home /nonexistent --gecos "cartesi user" --shell /bin/false cartesi

WORKDIR /opt/cartesi/bin

EXPOSE 5001

USER cartesi

CMD ["/opt/cartesi/bin/server-manager", "--manager-address=0.0.0.0:5001"]
