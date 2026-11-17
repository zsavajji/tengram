// voice_check: the voice codec (engine/opus_ogg.cpp) end to end. Makes 2 s of a 440 Hz sine, 48 kHz stereo, as the
// recorder writes a WAV (its sizes left 0), reads it, encodes it to OGG Opus, decodes that to a WAV and reads it back.
// Prints "voice_check: ok", or what failed and exit 1. Usage: voice_check [dir for the temporary files, default .]

#include "../engine/opus_ogg.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int fail(const std::string &what) {
  std::fprintf(stderr, "voice_check: FAIL: %s\n", what.c_str());
  return 1;
}

double rms(const std::vector<std::int16_t> &pcm, std::size_t from, std::size_t to) {
  double sum = 0;
  for (std::size_t i = from; i < to; ++i) {
    sum += double(pcm[i]) * pcm[i];
  }
  return std::sqrt(sum / double(to - from));
}

void put(std::FILE *f, std::uint32_t v, int bytes) {
  for (int i = 0; i < bytes; ++i) {
    std::fputc(int(v >> 8 * i & 255), f);
  }
}

}  // namespace

int main(int argc, char **argv) {
  const std::string dir = argc > 1 ? argv[1] : ".";
  const std::string in_wav = dir + "/voice_check_in.wav", ogg = dir + "/voice_check.ogg",
                    out_wav = dir + "/voice_check_out.wav";
  const int rate = 48000, seconds = 2;

  std::FILE *f = std::fopen(in_wav.c_str(), "wb");
  if (!f) {
    return fail("cannot write " + in_wav);
  }
  std::fputs("RIFF", f);
  put(f, 0, 4);
  std::fputs("WAVEfmt ", f);
  put(f, 16, 4);
  put(f, 1, 2);  // PCM
  put(f, 2, 2);  // stereo
  put(f, rate, 4);
  put(f, rate * 4, 4);
  put(f, 4, 2);
  put(f, 16, 2);
  std::fputs("data", f);
  put(f, 0, 4);
  for (int i = 0; i < rate * seconds; ++i) {
    const auto s = static_cast<std::uint16_t>(std::lround(10000 * std::sin(2 * 3.14159265358979 * 440 * i / rate)));
    put(f, s, 2);
    put(f, s, 2);
  }
  if (std::fclose(f) != 0) {
    return fail("cannot write " + in_wav);
  }

  std::string why;
  std::vector<std::int16_t> mono, back;
  int got_rate = 0, back_rate = 0;
  if (!wav_read(in_wav, mono, got_rate, why)) {
    return fail(why);
  }
  if (got_rate != rate || mono.size() != std::size_t(rate * seconds)) {
    return fail("wav_read: " + std::to_string(mono.size()) + " samples at " + std::to_string(got_rate) + " Hz");
  }
  if (!ogg_opus_encode(mono, rate, ogg, why) || !ogg_opus_decode(ogg, out_wav, why) ||
      !wav_read(out_wav, back, back_rate, why)) {
    return fail(why);
  }
  const double length = double(back.size()) / back_rate;
  const double rms_in = rms(mono, rate / 2, rate * 3 / 2), rms_out = rms(back, rate / 2, rate * 3 / 2);
  const std::string bits = waveform_bits(mono);
  std::printf("decoded %zu samples at %d Hz (%.3f s), rms in %.0f out %.0f, waveform %zu bytes\n", back.size(),
              back_rate, length, rms_in, rms_out, bits.size());
  std::remove(in_wav.c_str());
  std::remove(ogg.c_str());
  std::remove(out_wav.c_str());

  if (back_rate != rate || std::fabs(length - seconds) > 0.025) {
    return fail("the decoded WAV is not 2 s of 48 kHz");
  }
  if (std::fabs(rms_out - rms_in) > 0.3 * rms_in) {
    return fail("the decoded middle second's level is not within 30% of the input's");
  }
  if (bits.size() != 63 || bits == std::string(63, '\0')) {
    return fail("waveform_bits is not 63 bytes, or it is all zero");
  }
  std::puts("voice_check: ok");
  return 0;
}
