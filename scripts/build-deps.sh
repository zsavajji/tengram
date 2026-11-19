#!/usr/bin/env bash
# Cross-build TDLight, OpenSSL and libopus as static libraries for BlackBerry 10
# (QNX 6.6, ARMv7 softfp), into build/prefix. Run it from the Mac.
#
# Host (this script): fetch TDLight at a pinned commit, apply patches/td/*.patch,
# download OpenSSL and libopus. Compiles run inside the `tengram-build` Docker image
# (FROM bb10-cxx20, see docker/Dockerfile); the same file is re-run in there
# with --in-container <function>.
#
#   scripts/build-deps.sh              build everything (idempotent, stamped)
#   FORCE_REBUILD=1 scripts/build-deps.sh
#   JOBS=2 scripts/build-deps.sh       if the td_api units get OOM-killed
set -euo pipefail

# TDLight is TDLib plus a few options. To build stock TDLib instead, point these at
# https://github.com/tdlib/td and a commit of it; patches/td/ was made against TDLight,
# and a patch that no longer applies fails the build loudly.
readonly TD_REPO="${TD_REPO:-https://github.com/tdlight-team/tdlight}"
readonly TD_COMMIT="${TD_COMMIT:-ccf8956e63d29074009867859ecad251c75f36e1}"

readonly OPENSSL_VERSION="3.5.7"
readonly OPENSSL_SHA256="a8c0d28a529ca480f9f36cf5792e2cd21984552a3c8e4aa11a24aa31aeac98e8"
# Static, with the ARM assembly (linux-armv4: NEON AES, SHA and bignum, 2-4 times the speed of
# the C code on the phone; patches/openssl/ makes it start on BB10). No engines/dso/async (TDLib
# needs none of them). -DNO_SYSLOG: the SDK has no <syslog.h>. Section flags: for --gc-sections.
readonly OPENSSL_CONFIGURE_FLAGS="linux-armv4 no-shared no-tests no-dso no-engine no-async -DNO_SYSLOG -O2 -ffunction-sections -fdata-sections"

# libopus, for the voice notes (engine/opus_ogg.cpp). QNX has no runtime CPU detection, so the ARM code
# paths are fixed at configure time: NEON is presumed, as every BB10 CPU has it.
readonly OPUS_VERSION="1.6.1"
readonly OPUS_SHA256="6ffcb593207be92584df15b32466ed64bbec99109f007c82205f0194572411a1"
readonly OPUS_CONFIGURE_FLAGS="--enable-static --disable-shared --disable-rtcd --enable-fixed-point --disable-doc --disable-extra-programs"

# td_api's generated translation units take over 1 GB of RAM each.
readonly JOBS="${JOBS:-4}"

readonly IMAGE="tengram-build"
# The toolchain image under it. LTO needs the driver's linker-plugin support (bb10-cxx20 with
# docs/drafts/bb10-cxx20-lto.patch); with an older image set TD_LTO=OFF.
readonly BASE_IMAGE="${BASE_IMAGE:-bb10-cxx20:emutls-lto}"
# Link-time optimisation for TDLight (CMake picks thin LTO for clang). The libraries are then
# LLVM bitcode, and the code is generated when a program is linked.
readonly TD_LTO="${TD_LTO:-ON}"
# Release is -O2. MinSizeRel (-Os) is about a tenth smaller and slower.
readonly TD_BUILD_TYPE="${TD_BUILD_TYPE:-Release}"
readonly BB10_CXX20="/opt/bb10-cxx20"
readonly BINUTILS="/opt/bb10-toolchain/bin/arm-unknown-nto-qnx6.5.0eabi"
readonly SDK_TARGET="/opt/bb10-sdk/target/qnx6"

# Paths as the container sees them (the repo is mounted at /src).
readonly SRC=/src
readonly TD_SRC=$SRC/third_party/td
readonly PREFIX=$SRC/build/prefix

# ---------------------------------------------------------------- inside the container

# OpenSSL. Only libcrypto.a and libssl.a are built: `make build_libs` also builds
# apps/, which needs syslog.h.
build_openssl_in_container() {
    local dir=$SRC/build/openssl-$OPENSSL_VERSION
    rm -rf "$dir"
    tar -xzf "$SRC/third_party/openssl-$OPENSSL_VERSION.tar.gz" -C "$SRC/build"
    cd "$dir"
    local p
    for p in "$SRC"/patches/openssl/*.patch; do
        patch -p1 < "$p"
    done
    CC=bb10-clang AR="$BINUTILS-ar" RANLIB="$BINUTILS-ranlib" \
        ./Configure $OPENSSL_CONFIGURE_FLAGS --prefix="$PREFIX" --openssldir=/etc/ssl
    # The generated headers (include/openssl/safestack.h...) are not prerequisites of the
    # library targets: without this the SDK's old OpenSSL headers get picked up instead.
    make -s -j"$JOBS" build_generated
    make -s -j"$JOBS" libcrypto.a libssl.a
    mkdir -p "$PREFIX/lib" "$PREFIX/include"
    cp libcrypto.a libssl.a "$PREFIX/lib/"
    rm -rf "$PREFIX/include/openssl"
    cp -RL include/openssl "$PREFIX/include/"
}

# libopus: lib/libopus.a and include/opus/opus.h.
build_opus_in_container() {
    local dir=$SRC/build/opus-$OPUS_VERSION
    rm -rf "$dir"
    tar -xzf "$SRC/third_party/opus-$OPUS_VERSION.tar.gz" -C "$SRC/build"
    cd "$dir"
    ./configure --host=arm-unknown-nto-qnx6.5.0eabi CC=bb10-clang CFLAGS="-O2 -mfpu=neon" \
        $OPUS_CONFIGURE_FLAGS --prefix="$PREFIX" >/dev/null
    make -s -j"$JOBS"
    make -s install >/dev/null
}

# TDLight, two passes: a host build of `prepare_cross_compiling` writes the generated
# sources (td/generate/auto) into the source tree, then the cross build uses them.
build_td_in_container() {
    local gen=$SRC/build/td-generate out=$SRC/build/td-bb10
    rm -rf "$gen" "$out"

    # Pass 1: host compiler, host libssl/libz (docker/Dockerfile installs the -dev packages).
    CC=clang CXX=clang++ cmake -S "$TD_SRC" -B "$gen" -DCMAKE_BUILD_TYPE=Release \
        -DTD_SKIP_TEST=ON -DTD_SKIP_BENCHMARK=ON -DTD_SKIP_TG_CLI=ON
    cmake --build "$gen" --target prepare_cross_compiling --parallel "$JOBS"

    # Pass 2: BB10. OpenSSL is ours (the SDK's is 1.0.x), zlib is the SDK's own.
    # TD_INSTALL_SHARED_LIBRARIES=OFF: no shared tdjson.
    cmake -S "$TD_SRC" -B "$out" \
        -DCMAKE_TOOLCHAIN_FILE=$BB10_CXX20/share/bb10-cxx20.cmake \
        -DCMAKE_BUILD_TYPE="$TD_BUILD_TYPE" -DTD_ENABLE_LTO="$TD_LTO" \
        -DTD_SKIP_TEST=ON -DTD_SKIP_BENCHMARK=ON -DTD_INSTALL_SHARED_LIBRARIES=OFF \
        -DOPENSSL_FOUND=1 \
        -DOPENSSL_INCLUDE_DIR="$PREFIX/include" \
        -DOPENSSL_CRYPTO_LIBRARY="$PREFIX/lib/libcrypto.a" \
        -DOPENSSL_SSL_LIBRARY="$PREFIX/lib/libssl.a" \
        -DZLIB_FOUND=1 \
        -DZLIB_LIBRARIES="$SDK_TARGET/armle-v7/usr/lib/libz.so" \
        -DZLIB_INCLUDE_DIR="$SDK_TARGET/usr/include"
    # tdclient is the C++ API (td::ClientManager); its dependencies come with it. The
    # JSON interface is not built, so `cmake --install` (which wants it) is not used.
    cmake --build "$out" --target tdclient --parallel "$JOBS"

    mkdir -p "$PREFIX/lib" "$PREFIX/include/td/telegram" "$PREFIX/include/td/tl"
    # Not CMakeFiles/: CMake's LTO check leaves a libfoo.a in there.
    find "$out" -name 'lib*.a' -not -path '*/CMakeFiles/*' -exec cp {} "$PREFIX/lib/" \;
    cp "$TD_SRC/td/telegram/Client.h" "$TD_SRC/td/telegram/Log.h" "$PREFIX/include/td/telegram/"
    # td_api.h/.hpp come from the host pass (prepare_cross_compiling), in the source tree.
    cp "$TD_SRC/td/generate/auto/td/telegram/td_api.h" "$TD_SRC/td/generate/auto/td/telegram/td_api.hpp" \
        "$PREFIX/include/td/telegram/"
    cp "$TD_SRC/td/tl/TlObject.h" "$PREFIX/include/td/tl/"
}

# Every member of every .a is ARM, and nothing in prefix/lib is a host object.
verify_in_container() {
    local a bad=0
    for a in "$PREFIX"/lib/*.a; do
        # readelf prints one header per member; every one must say ARM.
        local machines
        machines=$("$BINUTILS-readelf" -h "$a" 2>/dev/null | awk -F: '/Machine:/ {gsub(/^ +/, "", $2); print $2}' | sort -u || true)
        # With LTO the TDLight archives hold bitcode, which readelf does not read: the link
        # test below is then what proves the target.
        if [ "$machines" != "ARM" ] && ! { [ "$TD_LTO" = ON ] && [ -z "$machines" ]; }; then
            echo "NOT ARM: $a ($machines)" >&2
            bad=1
        fi
    done
    [ "$bad" = 0 ]
    ls -l "$PREFIX"/lib/*.a
}

# The proof that the libraries have no unresolved symbols: link a main() that uses
# td::ClientManager. Not run (no phone here). Explicit paths to our .a files: the SDK
# has its own, older libssl/libcrypto in the default search path.
link_test_in_container() {
    local lib=$PREFIX/lib
    mkdir -p "$SRC/build"
    local lto=; [ "$TD_LTO" = ON ] && lto=-flto=thin
    bb10-clang++ -O2 $lto -Wall -I"$PREFIX/include" "$SRC/tests/td-link-test.cpp" -o "$SRC/build/td-link-test" \
        -Wl,--gc-sections -Wl,--start-group \
        "$lib"/libtdclient.a "$lib"/libtdcore.a "$lib"/libtdapi.a "$lib"/libtdmtproto.a \
        "$lib"/libtdnet.a "$lib"/libtddb.a "$lib"/libtdsqlite.a "$lib"/libtdactor.a \
        "$lib"/libtde2e.a "$lib"/libtdutils.a "$lib"/libssl.a "$lib"/libcrypto.a \
        -Wl,--end-group "$SDK_TARGET/armle-v7/usr/lib/libz.so" -lsocket -lm
    "$BINUTILS-readelf" -h "$SRC/build/td-link-test" | grep -E 'Machine|Type'
    "$BINUTILS-strip" -o "$SRC/build/td-link-test.stripped" "$SRC/build/td-link-test"
    ls -l "$SRC/build/td-link-test" "$SRC/build/td-link-test.stripped"
}

if [ "${1:-}" = "--in-container" ]; then
    shift
    "$@"
    exit
fi

# ---------------------------------------------------------------- on the Mac

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$ROOT"

info() { printf '\033[34m==> %s\033[0m\n' "$*"; }
die() { printf '\033[31mERROR: %s\033[0m\n' "$*" >&2; exit 1; }
sha256() { shasum -a 256 "$@" | awk '{print $1}'; }

in_container() {
    docker run --rm -e TD_LTO="$TD_LTO" -e TD_BUILD_TYPE="$TD_BUILD_TYPE" -v "$ROOT:$SRC" -w "$SRC" "$IMAGE" bash scripts/build-deps.sh --in-container "$@"
}

fetch_td() {
    if [ ! -d third_party/td/.git ]; then
        info "Fetching $TD_REPO at $TD_COMMIT"
        mkdir -p third_party/td
        git -C third_party/td init --quiet
        git -C third_party/td remote add origin "$TD_REPO"
        git -C third_party/td fetch --quiet --depth=1 origin "$TD_COMMIT"
        git -C third_party/td checkout --quiet FETCH_HEAD
    fi
    [ "$(git -C third_party/td rev-parse HEAD)" = "$TD_COMMIT" ] ||
        die "third_party/td is not at $TD_COMMIT. Remove it to re-pin."
}

# A patch is applied, already applied (reverse applies), or the series is broken.
apply_patches() {
    local p
    for p in "$ROOT"/patches/td/*.patch; do
        [ -e "$p" ] || break
        if git -C third_party/td apply --reverse --check "$p" 2>/dev/null; then
            echo "already applied: $(basename "$p")"
        elif git -C third_party/td apply --check "$p"; then
            git -C third_party/td apply "$p"
            echo "applied: $(basename "$p")"
        else
            die "$(basename "$p") neither applies nor is already applied. TDLight moved? Rebase the patch."
        fi
    done
}

fetch_openssl() {
    local f=third_party/openssl-$OPENSSL_VERSION.tar.gz
    if [ ! -f "$f" ]; then
        info "Downloading OpenSSL $OPENSSL_VERSION"
        curl -fsSL -o "$f.part" "https://www.openssl.org/source/openssl-$OPENSSL_VERSION.tar.gz"
        mv "$f.part" "$f"
    fi
    [ "$(sha256 "$f")" = "$OPENSSL_SHA256" ] || die "SHA256 mismatch for $f (expected $OPENSSL_SHA256)"
}

fetch_opus() {
    local f=third_party/opus-$OPUS_VERSION.tar.gz
    if [ ! -f "$f" ]; then
        info "Downloading libopus $OPUS_VERSION"
        curl -fsSL -o "$f.part" "https://downloads.xiph.org/releases/opus/opus-$OPUS_VERSION.tar.gz"
        mv "$f.part" "$f"
    fi
    [ "$(sha256 "$f")" = "$OPUS_SHA256" ] || die "SHA256 mismatch for $f (expected $OPUS_SHA256)"
}

# Run a build step unless its stamp (a string naming everything that changes the
# output) is already recorded. The stamp is written only after the step succeeds.
stamped() { # name stamp fn
    local name=$1 want=$2 fn=$3 file=build/prefix/.stamp-$1
    if [ -z "${FORCE_REBUILD:-}" ] && [ -f "$file" ] && [ "$(cat "$file")" = "$want" ]; then
        echo "$name up to date (FORCE_REBUILD=1 to redo)"
        return
    fi
    info "Building $name"
    in_container "$fn"
    mkdir -p build/prefix
    echo "$want" > "$file"
}

main() {
    command -v docker >/dev/null || die "docker not found"
    info "Docker image $IMAGE"
    docker build -q --build-arg BASE="$BASE_IMAGE" -t "$IMAGE" docker >/dev/null

    mkdir -p third_party build/prefix
    fetch_td
    apply_patches
    fetch_openssl
    fetch_opus

    local patches_sha openssl_patches_sha
    patches_sha=$(cat patches/td/*.patch 2>/dev/null | shasum -a 256 | awk '{print $1}')
    openssl_patches_sha=$(cat patches/openssl/*.patch 2>/dev/null | shasum -a 256 | awk '{print $1}')
    # The stamps name everything that shapes the output: sources, flags and the toolchain image
    # (its id, so a rebuilt image rebuilds everything). Other TD cmake flags are not in them:
    # FORCE_REBUILD=1 after changing those.
    local base_id
    base_id=$(docker image inspect -f '{{.Id}}' "$BASE_IMAGE")
    stamped openssl "$OPENSSL_VERSION:$OPENSSL_CONFIGURE_FLAGS:$openssl_patches_sha:$base_id" build_openssl_in_container
    stamped opus "$OPUS_VERSION:$OPUS_CONFIGURE_FLAGS:$base_id" build_opus_in_container
    stamped td "$TD_COMMIT:$patches_sha:$OPENSSL_VERSION:$TD_LTO:$TD_BUILD_TYPE:$base_id" build_td_in_container

    info "Verifying"
    in_container verify_in_container
    info "Link test"
    in_container link_test_in_container
}

main "$@"
