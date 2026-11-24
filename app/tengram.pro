# Tengram's UI is a shared object, the bar's Qnx/Cascades entry point, which the launcher's /bin/cascades (already holding
# Cascades and Qt) loads and whose exported main() it calls. Built by scripts/build-ui.sh, out of tree in build/ui.
TEMPLATE = lib
TARGET = tengram
CONFIG += qt warn_on release plugin
QT += declarative network sql gui
# QtGui links anyway (QtDeclarative needs it); QImage and QImageReader make the photo previews and the
# thumbnail sent with a photo (step 6.3), since the engine decodes no picture.
LIBS += -lbbcascades -lbbdevice -lbbdata -lbbsystem -lbbplatform -lbb -lbbutilityi18n -lslog2 -lbbpim -lbps -lasound -laudio_manager -lscreen -lcamapi
QMAKE_LFLAGS += -Wl,-rpath-link,$${QNX_DIR}/$${QNX_CPUDIR}/usr/lib/qt4/lib
QMAKE_CXXFLAGS += -Wno-psabi
# gcc 4.8.3 and C++11 (the user, 2026-11-20). The spec's qcc -Vgcc_ntoarmv7le takes the NDK's default, gcc 4.6.3, whose C++11
# is partial (no override or final). 4.8.3 ships in the same NDK, but with GNU libstdc++ only (lib/gcc/4.8.3), and Qt and
# Cascades are built against Dinkumware (libcpp.so.4). So C++ is compiled by 4.8.3's _cpp variant (Dinkumware's headers) and
# linked against 4.6.3's Dinkumware library, the libcpp.so.4 Qt and Cascades load: one standard library in the process, as
# before. gnu++11, not c++11: strict ISO hides the POSIX calls main.cpp makes (open, getpid, clock_gettime). Not C++20: clang's
# libc++ would sit beside Dinkumware (the engine and libtgs.so are clang's, and libtgs.so shows the UI a C API only).
QMAKE_CC = qcc -V4.8.3,gcc_ntoarmv7le
QMAKE_CXX = qcc -V4.8.3,gcc_ntoarmv7le_cpp
QMAKE_LINK = $$QMAKE_CXX
QMAKE_LINK_SHLIB = $$QMAKE_CXX
QMAKE_LINK_C = $$QMAKE_CC
QMAKE_LINK_C_SHLIB = $$QMAKE_CC
QMAKE_CXXFLAGS += -std=gnu++11
LIBS += -L$$(QNX_TARGET)/armle-v7/lib/gcc/4.6.3 -lcpp

SOURCES += src/main.cpp src/EngineLink.cpp src/ChatListModel.cpp src/ChatsDb.cpp src/EmojiModel.cpp src/MessageModel.cpp src/MessageRows.cpp src/MessageCards.cpp src/ChatRows.cpp src/ChatSearch.cpp src/MessageCard.cpp src/MessageCardActions.cpp src/MessageCardBubbles.cpp src/Albums.cpp src/MessageKinds.cpp src/Polls.cpp src/Pins.cpp src/ChatController.cpp src/ChatControllerGroup.cpp src/ChatControllerTranslate.cpp src/ChatControllerReactions.cpp src/ContactBook.cpp src/ContactInfo.cpp src/ContactPicker.cpp src/MapChooser.cpp src/MediaData.cpp src/MediaOpener.cpp src/QrImage.cpp src/Waveform.cpp src/OpenRequests.cpp src/Invites.cpp src/MemoryPatch.cpp src/Privacy.cpp src/Profile.cpp src/CallLog.cpp src/Sessions.cpp src/Settings.cpp src/Storage.cpp src/TopicModel.cpp src/Sticker.cpp src/PreviewMaker.cpp src/StickerPlayer.cpp src/StickerPicker.cpp src/StickerGrid.cpp src/LinkIo.cpp src/Proximity.cpp src/CallAudio.cpp src/CallVideo.cpp src/CameraEncoder.cpp src/VideoDecoder.cpp src/VideoFormat.cpp src/VideoOutput.cpp src/VideoWindow.cpp $$PWD/../lib/qrcodegen/qrcodegen.c
HEADERS += src/EngineLink.h src/ChatListModel.h src/ChatsDb.h src/EmojiModel.h src/emojiTable.h src/MessageModel.h src/MessageCards.h src/ChatRows.h src/ChatSearch.h src/MessageCard.h src/CardStyle.h src/Albums.h src/MessageKinds.h src/Polls.h src/Pins.h src/ChatController.h src/ContactBook.h src/ContactInfo.h src/ContactPicker.h src/MapChooser.h src/MediaData.h src/MediaOpener.h src/Mp4Length.h src/Moves.h src/dates.h src/colors.h src/QrImage.h src/Waveform.h src/OpenRequests.h src/Invites.h src/MemoryPatch.h src/Privacy.h src/Profile.h src/CallLog.h src/Sessions.h src/Settings.h src/Storage.h src/TopicModel.h src/Sticker.h src/PreviewMaker.h src/StickerPlayer.h src/StickerPicker.h src/StickerGrid.h src/sizes.h src/LinkIo.h src/Proximity.h src/CallAudio.h src/CallVideo.h src/CameraEncoder.h src/VideoDecoder.h src/VideoWindow.h src/Nv12Rotate.h
# The cards' bubbles (step 8.6b), one class each: whichever are written so far.
SOURCES += $$files($$PWD/src/*Bubble.cpp) $$files($$PWD/src/LinkPreview.cpp)
HEADERS += $$files($$PWD/src/*Bubble.h) $$files($$PWD/src/LinkPreview.h)

DESTDIR = app
OBJECTS_DIR = obj
MOC_DIR = moc

# qrcodegen is C99, fetched by scripts/fetch-qrcodegen.sh. gnu99 and not c99, which is strict ISO C and hides the
# POSIX functions (open, fcntl, pthread_mutexattr_settype) that SQLite's amalgamation calls: it does not compile.
INCLUDEPATH += $$PWD/../lib/qrcodegen
QMAKE_CFLAGS += -std=gnu99

# The OpenMAX IL headers of VideoDecoder, fetched by scripts/fetch-omxil.sh; the core itself is
# dlopen()ed from the phone (/base/lib/libOmxCore.so), so nothing links.
INCLUDEPATH += $$PWD/../lib/omxil

# libwebp 1.4.0, fetched by scripts/fetch-libwebp.sh: the decoder only (sticker pictures, step 6.5b),
# the files of its makefile.unix's DEC_OBJS, DSP_DEC_OBJS and UTILS_DEC_OBJS, and the demuxer with
# the animation decoder (an animated sticker shows its first frame). The sources include "src/..."
# (lib/libwebp) and <webp/decode.h> (lib/libwebp/src). Threads stay off (no WEBP_USE_THREAD): a
# sticker is one small frame. qcc passes -mcpu=cortex-a9 and no -mfpu=neon, so __ARM_NEON__ is unset
# and libwebp's NEON, SSE and MIPS files compile to empty stubs: the plain C code decodes.
# ponytail: add -mfpu=neon to QMAKE_CFLAGS if a sticker takes long to draw; untried.
WEBP_DIR = $$PWD/../lib/libwebp
WEBP_DEC = alpha_dec buffer_dec frame_dec idec_dec io_dec quant_dec tree_dec vp8_dec vp8l_dec webp_dec
WEBP_DSP = alpha_processing alpha_processing_mips_dsp_r2 alpha_processing_neon alpha_processing_sse2 \
    alpha_processing_sse41 cpu dec dec_clip_tables dec_mips32 dec_mips_dsp_r2 dec_msa dec_neon dec_sse2 \
    dec_sse41 filters filters_mips_dsp_r2 filters_msa filters_neon filters_sse2 lossless \
    lossless_mips_dsp_r2 lossless_msa lossless_neon lossless_sse2 lossless_sse41 rescaler rescaler_mips32 \
    rescaler_mips_dsp_r2 rescaler_msa rescaler_neon rescaler_sse2 upsampling upsampling_mips_dsp_r2 \
    upsampling_msa upsampling_neon upsampling_sse2 upsampling_sse41 yuv yuv_mips32 yuv_mips_dsp_r2 \
    yuv_neon yuv_sse2 yuv_sse41
WEBP_UTILS = bit_reader_utils color_cache_utils filters_utils huffman_utils palette quant_levels_dec_utils \
    random_utils rescaler_utils thread_utils utils
for(f, WEBP_DEC): SOURCES += $$WEBP_DIR/src/dec/$${f}.c
for(f, WEBP_DSP): SOURCES += $$WEBP_DIR/src/dsp/$${f}.c
for(f, WEBP_UTILS): SOURCES += $$WEBP_DIR/src/utils/$${f}.c
SOURCES += $$WEBP_DIR/src/demux/demux.c $$WEBP_DIR/src/demux/anim_decode.c
INCLUDEPATH += $$WEBP_DIR $$WEBP_DIR/src

# rlottie, for the animated stickers (src/StickerPlayer.cpp, docs/stickers-plan.md): it needs C++14, so scripts/build-tgs.sh
# builds it with clang into build/prefix/lib/libtgs.so, which exports only its C API (rlottie_capi.h) and ships in the bar's
# native/lib. The UI calls it as plain C; zlib (the phone's) unzips the .tgs.
INCLUDEPATH += $$PWD/../third_party/rlottie/inc
LIBS += -L$$PWD/../build/prefix/lib -ltgs -lz

# chats.db is SQLCipher (src/ChatsDb.cpp): Qt 4.8.6's own SQLite driver, renamed QSQLCipherDriver by
# scripts/fetch-qtsqlcipher.sh (the phone's QtSql already has a QSQLiteDriver), on TDLib's SQLCipher
# amalgamation. ui/sqlcipher/ is the spike with its test; these are its flags.
# Our libcrypto.a is built with clang and needs the __aeabi_mem* helpers of aeabi_mem.c, and -lsocket.
TD_SQLITE = $$PWD/../third_party/td/sqlite
SQLCIPHER_DIR = $$PWD/../lib/qtsqlcipher
SPIKE_DIR = $$PWD/../ui/sqlcipher
INCLUDEPATH += $$TD_SQLITE $$SQLCIPHER_DIR $$SPIKE_DIR $$PWD/../build/prefix/include
HEADERS += $$SQLCIPHER_DIR/qsql_sqlcipher.h $$SPIKE_DIR/tdsqlite_names.h
SOURCES += $$SQLCIPHER_DIR/qsql_sqlcipher.cpp $$TD_SQLITE/sqlite/sqlite3.c $$SPIKE_DIR/aeabi_mem.c
# TDLib's own flags for this file (third_party/td/sqlite/CMakeLists.txt), without SQLITE_OMIT_DECLTYPE:
# Qt's driver asks for declared column types.
DEFINES += OMIT_MEMLOCK SQLITE_DEFAULT_MEMSTATUS=0 SQLITE_DEFAULT_RECURSIVE_TRIGGERS=1 \
    SQLITE_DEFAULT_SYNCHRONOUS=1 SQLITE_DISABLE_LFS SQLITE_HAS_CODEC SQLITE_OMIT_DEPRECATED \
    SQLITE_OMIT_DESERIALIZE SQLITE_OMIT_LOAD_EXTENSION SQLITE_OMIT_PROGRESS_CALLBACK SQLITE_TEMP_STORE=2 \
    USE_PREAD=1
# The C libraries' warnings (libwebp, SQLite) are silenced; the C++ ones stay on.
QMAKE_CFLAGS_WARN_ON = -w
LIBS += $$PWD/../build/prefix/lib/libcrypto.a -lsocket
QMAKE_LFLAGS += -Wl,--gc-sections
# Nothing of the static libcrypto may show in the library's dynamic symbols: a system library that uses the
# phone's own OpenSSL 1.0 (QtNetwork, say) would bind to ours, 3.5, and crash.
QMAKE_LFLAGS += -Wl,--exclude-libs,ALL
# As a shared object loaded into a process that already holds the system's libraries, our own globals (libwebp, qrcodegen,
# our classes) would otherwise bind to a same-named symbol loaded before us; -Bsymbolic binds our references to our own
# definitions.
QMAKE_LFLAGS += -Wl,-Bsymbolic

# lupdate reads the QML too; scripts/build-ui.sh turns the .ts into the .qm the bar ships.
CODECFORTR = UTF-8
CODECFORSRC = UTF-8
TRANSLATIONS = translations/tengram_it.ts \
    translations/tengram_fr.ts \
    translations/tengram_de.ts \
    translations/tengram_es.ts \
    translations/tengram_ru.ts \
    translations/tengram_uk.ts \
    translations/tengram_pl.ts \
    translations/tengram_hi.ts \
    translations/tengram_id.ts \
    translations/tengram_vi.ts
lupdate_only {
    SOURCES += assets/*.qml assets/errors.js
}
