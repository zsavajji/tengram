# The toolchain image: bb10-cxx20 with the QNX emulated-TLS runtime and LTO (scripts/build-deps.sh passes it).
ARG BASE=bb10-cxx20:emutls-lto
FROM $BASE
# gperf: TDLib's generated sources. perl: full distribution (FindBin etc.) for OpenSSL's Configure.
# libssl-dev, zlib1g-dev: the HOST pass of the TDLib build (prepare_cross_compiling runs on the build machine).
# patch: patches/openssl/ is applied inside the container, to the extracted tarball.
RUN apt-get update && apt-get install -y --no-install-recommends gperf perl patch libssl-dev zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*
