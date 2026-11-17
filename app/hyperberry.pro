# HyperBerry's UI, a plain executable as the bar's Qnx/Elf entry point. Built and packaged by
# scripts/build-app.sh, out of tree in build/ui.
TEMPLATE = app
TARGET = hyperberry
CONFIG += qt warn_on release
QT += declarative network sql gui
# QtGui links anyway (QtDeclarative needs it); QImage and QImageReader make the photo previews and the
# thumbnail sent with a photo (step 6.3), since the engine decodes no picture.
LIBS += -lbbcascades -lbbdevice -lbbdata -lbbsystem -lbbplatform -lbb -lbbutilityi18n -lslog2 -lbbpim -lbps -lasound -laudio_manager -lscreen -lcamapi
QMAKE_LFLAGS += -Wl,-rpath-link,$${QNX_DIR}/$${QNX_CPUDIR}/usr/lib/qt4/lib
QMAKE_CXXFLAGS += -Wno-psabi

SOURCES += src/main.cpp src/EngineLink.cpp src/ChatListModel.cpp src/CallsModel.cpp src/StoriesModel.cpp src/EmojiModel.cpp src/MessageModel.cpp src/MessageRows.cpp src/MessageCards.cpp src/ChatRows.cpp src/MessageCard.cpp src/MessageCardActions.cpp src/MessageCardBubbles.cpp src/Albums.cpp src/MessageKinds.cpp src/Polls.cpp src/Pins.cpp src/ChatController.cpp src/ChatControllerGroup.cpp src/ContactBook.cpp src/ContactInfo.cpp src/ContactPicker.cpp src/MapChooser.cpp src/MediaData.cpp src/MediaOpener.cpp src/QrImage.cpp src/Waveform.cpp src/OpenRequests.cpp src/Invites.cpp src/Profile.cpp src/Settings.cpp src/Sticker.cpp src/CallAudio.cpp src/CallVideo.cpp src/LinkIo.cpp src/Proximity.cpp src/VideoDecoder.cpp src/VideoFormat.cpp src/VideoOutput.cpp src/VideoWindow.cpp src/CameraEncoder.cpp $$PWD/../lib/qrcodegen/qrcodegen.c
HEADERS += src/EngineLink.h src/ChatListModel.h src/CallsModel.h src/StoriesModel.h src/EmojiModel.h src/emojiTable.h src/MessageModel.h src/MessageCards.h src/ChatRows.h src/MessageCard.h src/CardStyle.h src/Albums.h src/MessageKinds.h src/Polls.h src/Pins.h src/ChatController.h src/ContactBook.h src/ContactInfo.h src/ContactPicker.h src/MapChooser.h src/MediaData.h src/MediaOpener.h src/Moves.h src/dates.h src/colors.h src/QrImage.h src/Waveform.h src/OpenRequests.h src/Invites.h src/Profile.h src/Settings.h src/Sticker.h src/sizes.h src/CallAudio.h src/CallVideo.h src/LinkIo.h src/Proximity.h src/VideoDecoder.h src/VideoWindow.h src/Nv12Rotate.h src/CameraEncoder.h
# The cards' bubbles (step 8.6b), one class each: whichever are written so far.
SOURCES += $$files($$PWD/src/*Bubble.cpp) $$files($$PWD/src/LinkPreview.cpp)
HEADERS += $$files($$PWD/src/*Bubble.h) $$files($$PWD/src/LinkPreview.h)

DESTDIR = app
OBJECTS_DIR = obj
MOC_DIR = moc

# qrcodegen is C99, fetched by scripts/fetch-qrcodegen.sh.
INCLUDEPATH += $$PWD/../lib/qrcodegen
QMAKE_CFLAGS += -std=c99

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

# lupdate reads the QML too; scripts/build-app.sh turns the .ts into the .qm the bar ships.
CODECFORTR = UTF-8
CODECFORSRC = UTF-8
TRANSLATIONS = translations/hyperberry_it.ts \
    translations/hyperberry_fr.ts \
    translations/hyperberry_de.ts \
    translations/hyperberry_es.ts \
    translations/hyperberry_ru.ts \
    translations/hyperberry_uk.ts \
    translations/hyperberry_pl.ts \
    translations/hyperberry_hi.ts \
    translations/hyperberry_id.ts \
    translations/hyperberry_vi.ts
lupdate_only {
    SOURCES += assets/*.qml assets/errors.js
}
