#!/bin/sh
# Builds build/tengramd (stripped), build/tengramd.unstripped, build/dbq, build/voice_check and build/https_check for BB10 inside the
# tengram-build image. Run from the Mac after scripts/build-deps.sh. With TG_API_ID and TG_API_HASH in the environment they are
# compiled in (the packaged service gets no environment of ours); they are never written to a file. Same link line as probe/build.sh. TD_LTO= (empty) if the
# libraries were built with TD_LTO=OFF. TG_TEST_DC=1 builds an engine on Telegram's test servers (numbers 99966XYYYY, X the
# DC 1 to 3, sign in or up with the code XXXXX), for trying the sign-up without a new number; its TDLib database is not the
# real servers' one, so log out before installing such a build, and again before going back.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
docker run --rm -e TD_LTO="${TD_LTO-1}" -e TG_API_ID="${TG_API_ID-}" -e TG_API_HASH="${TG_API_HASH-}" -e TG_TEST_DC="${TG_TEST_DC-}" -v "$ROOT:/src" -w /src tengram-build sh -c '
L=build/prefix/lib
# The engine uses TDLib'"'"'s internal headers (SqliteDb, JsonBuilder): -isystem keeps their warnings out of the output.
INC="-isystem build/prefix/include -isystem third_party/td/tddb -isystem third_party/td/tdutils \
-isystem build/td-bb10/tdutils -isystem third_party/td -isystem third_party/td/sqlite \
-isystem calls -isystem third_party/tgcalls -isystem third_party/tgcalls/tgcalls -DTGCALLS_USE_STD_OPTIONAL"
LIBS="-Wl,--gc-sections -Wl,--start-group $L/libtdclient.a $L/libtdcore.a $L/libtdapi.a $L/libtdmtproto.a \
$L/libtdnet.a $L/libtddb.a $L/libtdsqlite.a $L/libtdactor.a $L/libtde2e.a $L/libtdutils.a \
$L/libtgcalls.a $L/libtg_owt.a $L/libssl.a $L/libcrypto.a $L/libopus.a -Wl,--end-group \
/opt/bb10-sdk/target/qnx6/armle-v7/usr/lib/libz.so -lsocket -lm"
KEYS=
if [ -n "$TG_API_ID" ] && [ -n "$TG_API_HASH" ]; then KEYS="-DTENGRAM_API_ID=$TG_API_ID -DTENGRAM_API_HASH=\"$TG_API_HASH\""; fi
if [ -n "$TG_TEST_DC" ]; then KEYS="$KEYS -DTENGRAM_TEST_DC"; fi
STRIP=/opt/bb10-toolchain/bin/arm-unknown-nto-qnx6.5.0eabi-strip
bb10-clang++ -std=c++20 -O2 ${TD_LTO:+-flto=thin} -Wall $KEYS $INC engine/*.cpp -o build/tengramd.unstripped $LIBS -lbps -lmmrndclient -lstrm &&
$STRIP -o build/tengramd build/tengramd.unstripped &&
bb10-clang++ -std=c++20 -O2 ${TD_LTO:+-flto=thin} -Wall $INC tests/dbq.cpp -o build/dbq $LIBS &&
$STRIP build/dbq &&
bb10-clang++ -std=c++20 -O2 -Wall -isystem build/prefix/include tests/voice_check.cpp engine/opus_ogg.cpp -o build/voice_check $L/libopus.a -lm &&
$STRIP build/voice_check &&
bb10-clang++ -std=c++20 -O2 -Wall -isystem build/prefix/include tests/https_check.cpp engine/https.cpp -o build/https_check $L/libssl.a $L/libcrypto.a -lsocket -lm &&
$STRIP build/https_check &&
ls -l build/tengramd build/dbq build/voice_check build/https_check'
