#!/usr/bin/env bash
# Animated stickers (docs/stickers-plan.md, G1): rlottie built by clang into build/prefix/lib/libtgs.so, which
# exports rlottie's C API only (tests/tgs.map) with libc++ linked in statically, so the UI's gcc 4.8 C++ never meets
# it; then tests/tgs_check, plain C built by the NDK's qcc as the UI is, linked against libtgs.so.
#
#   scripts/build-tgs.sh            fetch rlottie (once), build; incremental in build/rlottie
#   CLEAN=1 scripts/build-tgs.sh    start build/rlottie over
#
# rlottie is Telegram Desktop's fork, at the pin below: Samsung's (ea06d2f) left parts of stickers out (nothing of
# location.tgs after frame 33). Options: no image-loader plugin (dlopen), no render thread (a sticker renders on the
# caller's thread), no cache; NEON on (the fork's default is off), pixman's asm assembled apart. LOT_BUILD makes the
# C API visible in a static build. -O3 measured no faster than -O2 (2026-11-20).
set -euo pipefail

readonly RLOTTIE_REPO="https://github.com/desktop-app/rlottie.git"
readonly RLOTTIE_COMMIT="cbfdcdd2830b4beba53e64e72c11cb0328a720a9"
readonly JOBS="${JOBS:-10}"
readonly IMAGE="tengram-build"
readonly NDK_IMAGE="${BB10_NDK_IMAGE:-yamsergey/bb10-ndk:0.6}"

# ---------------------------------------------------------------- inside the clang container

build_rlottie() {
    local B=/src/build/rlottie L=/src/build/prefix/lib
    [ "${CLEAN:-}" = 1 ] && rm -rf "$B"
    mkdir -p "$L"
    cmake -S /src/third_party/rlottie -B "$B" \
        -DCMAKE_TOOLCHAIN_FILE=/opt/bb10-cxx20/share/bb10-cxx20.cmake -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_FLAGS="-mfpu=neon" -DCMAKE_CXX_FLAGS="-mfpu=neon -DLOT_BUILD" \
        -DCMAKE_C_FLAGS_RELEASE="-O2 -DNDEBUG" -DCMAKE_CXX_FLAGS_RELEASE="-O2 -DNDEBUG" \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DBUILD_SHARED_LIBS=OFF \
        -DLOTTIE_MODULE=OFF -DLOTTIE_THREAD=OFF -DLOTTIE_CACHE=OFF -DLOTTIE_TEST=OFF -DLOTTIE_DISABLE_ARM_NEON=OFF >/dev/null
    cmake --build "$B" --target rlottie -- -j"$JOBS" > /src/build/rlottie-build.log 2>&1 ||
        { grep -E "error" /src/build/rlottie-build.log | head -40; echo "build failed: build/rlottie-build.log" >&2; exit 1; }
    # pixman's NEON fills (vdrawhelper_neon.cpp calls two): GNU as macros that clang's assembler rejects, so the
    # armnto GNU as assembles them, as rlottie's ARCH=arm would with gcc.
    local X=/src/third_party/rlottie/src/vector/pixman
    bb10-clang -E -x assembler-with-cpp -I"$X" "$X/pixman-arm-neon-asm.S" > "$B/pixman-neon.s"
    /opt/bb10-toolchain/bin/arm-unknown-nto-qnx6.5.0eabi-as -meabi=5 -march=armv7-a -mfpu=neon -mfloat-abi=softfp "$B/pixman-neon.s" -o "$B/pixman-neon.o"
    bb10-clang++ -shared -o "$L/libtgs.so" -Wl,-soname,libtgs.so "$B/pixman-neon.o" \
        -Wl,--whole-archive "$B/librlottie.a" -Wl,--no-whole-archive -Wl,--version-script=/src/tests/tgs.map -lm
    /opt/bb10-toolchain/bin/arm-unknown-nto-qnx6.5.0eabi-strip "$L/libtgs.so"
    # The same check with rlottie linked in: a program started over ssh can't map a library outside the system's
    # trusted file system (mmap of its code fails, EPERM, even after pathtrust), while a bar's native/lib loads.
    bb10-clang -O2 -mfpu=neon -I/src/third_party/rlottie/inc -c /src/tests/tgs_check.c -o "$B/tgs_check.o"
    bb10-clang++ -o /src/build/tgs_check_static "$B/tgs_check.o" "$B/pixman-neon.o" "$B/librlottie.a" \
        /opt/bb10-sdk/target/qnx6/armle-v7/usr/lib/libz.so -lm
    /opt/bb10-toolchain/bin/arm-unknown-nto-qnx6.5.0eabi-strip /src/build/tgs_check_static
    ls -l "$L/libtgs.so" /src/build/tgs_check_static
}

# ---------------------------------------------------------------- inside the NDK container

build_check() {
    qcc -Vgcc_ntoarmv7le -O2 -std=gnu99 -Wall -I/src/third_party/rlottie/inc /src/tests/tgs_check.c \
        -o /src/build/tgs_check -L/src/build/prefix/lib -ltgs -lz
    ntoarmv7-strip /src/build/tgs_check
    ls -l /src/build/tgs_check
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

if [ ! -d third_party/rlottie/.git ]; then
    info "Fetching $RLOTTIE_REPO at $RLOTTIE_COMMIT"
    git clone --quiet "$RLOTTIE_REPO" third_party/rlottie
    git -C third_party/rlottie checkout --quiet "$RLOTTIE_COMMIT"
fi
[ "$(git -C third_party/rlottie rev-parse HEAD)" = "$RLOTTIE_COMMIT" ] || die "third_party/rlottie is not at $RLOTTIE_COMMIT"

info "Building libtgs.so (rlottie)"
docker run --rm -e CLEAN="${CLEAN:-}" -e JOBS="$JOBS" -v "$ROOT:/src" -w /src "$IMAGE" \
    bash scripts/build-tgs.sh --in-container build_rlottie
info "Building tgs_check (qcc)"
docker run --rm -v "$ROOT:/src" -w /src "$NDK_IMAGE" \
    bash -c 'source /root/bbndk/bbndk-env_10_3_1_995.sh >/dev/null && bash scripts/build-tgs.sh --in-container build_check'
