#!/bin/sh
# Builds build/tdprobe (stripped) for BB10 inside the tengram-build image. Run from the Mac after
# scripts/build-deps.sh. Same link line as build-deps.sh's link test. TD_LTO= (empty) if the
# libraries were built with TD_LTO=OFF.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
docker run --rm -e TD_LTO="${TD_LTO-1}" -v "$ROOT:/src" -w /src tengram-build sh -c '
L=build/prefix/lib
bb10-clang++ -O2 ${TD_LTO:+-flto=thin} -Wall -Ibuild/prefix/include probe/tdprobe.cpp -o build/tdprobe.unstripped \
  -Wl,--gc-sections -Wl,--start-group $L/libtdclient.a $L/libtdcore.a $L/libtdapi.a $L/libtdmtproto.a \
  $L/libtdnet.a $L/libtddb.a $L/libtdsqlite.a $L/libtdactor.a $L/libtde2e.a $L/libtdutils.a \
  $L/libssl.a $L/libcrypto.a -Wl,--end-group /opt/bb10-sdk/target/qnx6/armle-v7/usr/lib/libz.so -lsocket -lm &&
/opt/bb10-toolchain/bin/arm-unknown-nto-qnx6.5.0eabi-strip -o build/tdprobe build/tdprobe.unstripped &&
ls -l build/tdprobe'
