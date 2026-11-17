// The voice notes' codec: a recorder's WAV in, an OGG Opus file out, and back (voice.* in docs/protocol.md). No TDLib
// in it: tests/voice_check.cpp links opus_ogg.cpp and libopus alone. The format is HyperBerry's (engine/voice.go).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

// A WAV's 16-bit PCM mixed down to mono, at its own rate. The data chunk runs to the end of the file whatever its size
// field says (a recorder that did not finish leaves 0 or 0xFFFFFFFF). False and `why` for anything else.
bool wav_read(const std::string &path, std::vector<std::int16_t> &mono, int &rate, std::string &why);

// Writes mono PCM at `rate` (8000, 12000, 16000, 24000 or 48000) as an OGG Opus voice note: VOIP, 20 kbps, 20 ms
// frames.
bool ogg_opus_encode(const std::vector<std::int16_t> &mono, int rate, const std::string &out_path, std::string &why);

// Decodes an OGG Opus file (any number of channels up to 2) into a 16-bit mono WAV at 48 kHz, without the pre-skip and
// the padding after the last granule. The WAV is written under a temporary name and renamed.
bool ogg_opus_decode(const std::string &in_path, const std::string &wav_path, std::string &why);

// TDLib's waveform: 100 samples of 5 bits, packed least significant bit first (63 bytes). Each is the peak of a
// hundredth of the note, the loudest one being 31.
std::string waveform_bits(const std::vector<std::int16_t> &mono);
