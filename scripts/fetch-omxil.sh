#!/bin/sh
# Fetches the Khronos OpenMAX IL 1.1.2 headers (as AOSP carries them, Khronos licence in each file)
# into lib/omxil/, pinned to a commit of platform/frameworks/native and checked by sha256, for
# app/src/VideoDecoder.cpp. The phone's libOmxCore.so is dlopen()ed, so only headers are needed.
# Not committed. Does no work when every file is there and matches.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
URL=https://android.googlesource.com/platform/frameworks/native/+/4f463a6b1de9198963dc6aff74154a504ba3f8f6/headers/media_plugin/media/openmax
DIR=$ROOT/lib/omxil
mkdir -p "$DIR"
fetch() { # <file> <sha256>
	# The hash is compared here, not by sha256sum -c: macOS's takes no --status, and fails -c on stdin.
	[ -f "$DIR/$1" ] && [ "$(sha256sum "$DIR/$1" | cut -d" " -f1)" = "$2" ] && return
	# gitiles serves a raw file only as base64 (?format=TEXT).
	curl -fsSL "$URL/$1?format=TEXT" | base64 -d >"$DIR/$1.part"
	[ "$(sha256sum "$DIR/$1.part" | cut -d" " -f1)" = "$2" ] || { rm -f "$DIR/$1.part"; echo "$1: sha256 mismatch" >&2; exit 1; }
	mv "$DIR/$1.part" "$DIR/$1"
}
fetch OMX_Audio.h 5feb6d40ae2ad3e3e172957c9a54f5aa51ee30c5c8dda1a47b5a34f6f9f4ea1f
fetch OMX_Component.h 32ddf10d8b32f3e100b87a5e88049b8221464db2580a9c7c218758397993810a
fetch OMX_Core.h 32bdcf4de4533e7fea1cd11e0954294f220dd1dc4d5f568a2a53d81868fcc6fd
fetch OMX_Image.h d3d9f1c9643b60fefbcfa7ad13a1c8905b5d8dc7d907e35515ac2c2a1290c68d
fetch OMX_Index.h 6e53aa3d4311f86a0b45901e652fdfa783e7f0a20f6dd17f29e7aaedb5113bb7
fetch OMX_IVCommon.h 70a176bb069cd2f2b4f5845f3fa6f26133e0089e75044fc5caf8e400f0d1f685
fetch OMX_Other.h fc00885b8a98f01b98255b236fa7312bd22e3d8c1061c1944f482a54e607c310
fetch OMX_Types.h 6ff64a0101834dadddb9c68d2f63dd9893b759c05807cc0448afec3413514264
fetch OMX_Video.h 48f5208993d14e8db8c81d22e4ee8c41a62ef06638cf393b0741c1402a2953bf
