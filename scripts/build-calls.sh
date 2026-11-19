#!/usr/bin/env bash
# Cross-build Telegram's call libraries for BlackBerry 10: tg_owt (Telegram's WebRTC) and tgcalls, as static
# libraries in build/prefix/lib (libtg_owt.a, libtgcalls.a), with build/prefix/calls.flags holding the defines and
# include paths a program using them compiles with. Then links and strips tests/calls_check, the on-phone check.
# Run from the Mac after scripts/build-deps.sh (it needs OpenSSL and libopus in build/prefix).
#
#   scripts/build-calls.sh             fetch (once), patch, build; incremental in build/calls
#   CLEAN=1 scripts/build-calls.sh     start build/calls over
#
# The pins are the pair Telegram Desktop builds (its prepare.py and its tgcalls submodule). The QNX changes are
# patches/tg_owt/*.patch (and patches/tgcalls/*.patch when there are any). calls/CMakeLists.txt builds both; software
# video codecs are left out (calls/QnxInterface.cpp's are pass-through), so libvpx, openh264, ffmpeg and libjpeg aren't
# needed.
set -euo pipefail

readonly TG_OWT_REPO="https://github.com/desktop-app/tg_owt.git"
readonly TG_OWT_COMMIT="020feec5c040fdae2cf752c2c2be62ddde7c236d"
readonly TGCALLS_REPO="https://github.com/TelegramMessenger/tgcalls.git"
readonly TGCALLS_COMMIT="1c236c09f8d8569fead14bd68000618a52051225"
readonly JOBS="${JOBS:-10}"
readonly IMAGE="tengram-build"

# ---------------------------------------------------------------- inside the container

build_in_container() {
    local P=/src/build/prefix E=/src/build/calls-empty B=/src/build/calls
    [ "${CLEAN:-}" = 1 ] && rm -rf "$B"
    mkdir -p "$E"
    # Release with -O2, as the rest of Tengram (CMake's Release is -O3). The video codecs' include paths are an
    # empty folder: tg_owt asks for them, and nothing that includes them is compiled on QNX.
    cmake -S /src/calls -B "$B" \
        -DCMAKE_TOOLCHAIN_FILE=/opt/bb10-cxx20/share/bb10-cxx20.cmake -DTENGRAM_THIRD_PARTY=/src/third_party \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS_RELEASE="-O2 -DNDEBUG" -DCMAKE_CXX_FLAGS_RELEASE="-O2 -DNDEBUG" \
        -DTG_OWT_BUILD_AUDIO_BACKENDS=OFF -DTG_OWT_USE_X11=OFF -DTG_OWT_USE_PIPEWIRE=OFF \
        -DTG_OWT_OPENSSL_INCLUDE_PATH=$P/include -DTG_OWT_OPUS_INCLUDE_PATH=$P/include/opus \
        -DTG_OWT_LIBJPEG_INCLUDE_PATH=$E -DTG_OWT_LIBVPX_INCLUDE_PATH=$E \
        -DTG_OWT_OPENH264_INCLUDE_PATH=$E -DTG_OWT_FFMPEG_INCLUDE_PATH=$E >/dev/null
    cmake --build "$B" --target tgcalls -- -j"$JOBS" > /src/build/calls-build.log 2>&1 ||
        { grep -E "error:" /src/build/calls-build.log | head -40; echo "build failed: build/calls-build.log" >&2; exit 1; }
    cp "$B/libtgcalls.a" "$B/tg_owt/libtg_owt.a" "$P/lib/"

    # What a program including tgcalls (and WebRTC) headers needs: tgcalls' own compile flags, as CMake wrote them.
    local f=$B/CMakeFiles/tgcalls.dir/flags.make
    { grep '^CXX_DEFINES' "$f" | cut -d= -f2-; grep '^CXX_INCLUDES' "$f" | cut -d= -f2-; } | tr '\n' ' ' > "$P/calls.flags"

    # The check: tgcalls on the phone without Telegram (tests/calls_check.cpp).
    local L=$P/lib
    bb10-clang++ -std=c++20 -O2 -mfpu=neon $(cat "$P/calls.flags") /src/tests/calls_check.cpp -o /src/build/calls_check \
        -Wl,--gc-sections $L/libtgcalls.a $L/libtg_owt.a $L/libssl.a $L/libcrypto.a $L/libopus.a \
        /opt/bb10-sdk/target/qnx6/armle-v7/usr/lib/libz.so -lsocket -lm
    /opt/bb10-toolchain/bin/arm-unknown-nto-qnx6.5.0eabi-strip -o /src/build/calls_check.stripped /src/build/calls_check
    ls -l "$P/lib/libtg_owt.a" "$P/lib/libtgcalls.a" /src/build/calls_check.stripped
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

# fetch <dir> <repo> <commit> [submodules]
fetch() {
    local dir=$1 repo=$2 commit=$3
    if [ ! -d "$dir/.git" ]; then
        info "Fetching $repo at $commit"
        git clone --quiet "$repo" "$dir"
        git -C "$dir" checkout --quiet "$commit"
        [ "${4:-}" = submodules ] && git -C "$dir" submodule update --init --recursive --quiet
    fi
    [ "$(git -C "$dir" rev-parse HEAD)" = "$commit" ] || die "$dir is not at $commit. Remove it to re-pin."
}

# A patch is applied, already applied (reverse applies), or the series is broken.
apply_patches() {
    local dir=$1 series=$2 p
    for p in "$series"/*.patch; do
        [ -e "$p" ] || continue
        if git -C "$dir" apply --check "$ROOT/$p" 2>/dev/null; then
            info "Applying $p"
            git -C "$dir" apply "$ROOT/$p"
        elif ! git -C "$dir" apply --check --reverse "$ROOT/$p" 2>/dev/null; then
            die "$p neither applies nor is applied in $dir"
        fi
    done
}

[ -f build/prefix/lib/libopus.a ] && [ -f build/prefix/lib/libcrypto.a ] || die "run scripts/build-deps.sh first"
fetch third_party/tg_owt "$TG_OWT_REPO" "$TG_OWT_COMMIT" submodules
fetch third_party/tgcalls "$TGCALLS_REPO" "$TGCALLS_COMMIT"
apply_patches third_party/tg_owt patches/tg_owt
apply_patches third_party/tgcalls patches/tgcalls
info "Building tg_owt and tgcalls"
docker run --rm -e CLEAN="${CLEAN:-}" -e JOBS="$JOBS" -v "$ROOT:/src" -w /src "$IMAGE" \
    bash scripts/build-calls.sh --in-container build_in_container
