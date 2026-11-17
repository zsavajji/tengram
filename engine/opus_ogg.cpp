// The voice notes' codec (opus_ogg.h). One logical stream, pages of whole packets; the reader also takes packets that
// span pages. Granule positions count 48 kHz samples whatever the input rate (RFC 7845).

#include "opus_ogg.h"

#include <opus/opus.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace {

struct Closer {
  void operator()(std::FILE *f) const {
    std::fclose(f);
  }
};
using File = std::unique_ptr<std::FILE, Closer>;

// A little-endian number of `bytes` bytes.
std::uint64_t get(const unsigned char *p, int bytes) {
  std::uint64_t v = 0;
  for (int i = bytes; i-- > 0;) {
    v = v << 8 | p[i];
  }
  return v;
}

void put(std::string &s, std::uint64_t v, int bytes) {
  for (int i = 0; i < bytes; ++i) {
    s += static_cast<char>(v >> 8 * i);
  }
}

// The 44 bytes before the samples of a mono 16-bit WAV.
std::string wav_header(std::int64_t samples, int rate) {
  std::string h = "RIFF";
  put(h, 36 + 2 * samples, 4);
  h += "WAVEfmt ";
  put(h, 16, 4);
  put(h, 1, 2);  // PCM
  put(h, 1, 2);  // mono
  put(h, rate, 4);
  put(h, 2 * rate, 4);
  put(h, 2, 2);
  put(h, 16, 2);
  h += "data";
  put(h, 2 * samples, 4);
  return h;
}

// Ogg's CRC: polynomial 0x04c11db7, not reflected, no final xor.
std::uint32_t ogg_crc(const std::string &b) {
  static const auto table = [] {
    std::array<std::uint32_t, 256> t;
    for (std::uint32_t i = 0; i < 256; ++i) {
      std::uint32_t r = i << 24;
      for (int j = 0; j < 8; ++j) {
        r = r & 0x80000000 ? r << 1 ^ 0x04c11db7 : r << 1;
      }
      t[i] = r;
    }
    return t;
  }();
  std::uint32_t crc = 0;
  for (const char c : b) {
    crc = crc << 8 ^ table[(crc >> 24) ^ static_cast<unsigned char>(c)];
  }
  return crc;
}

enum : unsigned char { kFirst = 2, kLast = 4 };

// The pages of the file being made. The serial is fixed, so the same input gives the same bytes.
struct Ogg {
  std::string buf;
  std::uint32_t seq = 0;

  // The packets as one page. They must fit in its 255 lacing values.
  void page(const std::vector<std::string> &packets, std::int64_t granule, unsigned char flags) {
    std::string lacing, body;
    for (const std::string &p : packets) {
      lacing.append(p.size() / 255, '\xff');
      lacing += static_cast<char>(p.size() % 255);
      body += p;
    }
    std::string h = "OggS";
    h += '\0';
    h += static_cast<char>(flags);
    put(h, granule, 8);
    put(h, 0x52474e54, 4);  // the serial, "TNGR"
    put(h, seq++, 4);
    put(h, 0, 4);  // the checksum, once the page is whole
    h += static_cast<char>(lacing.size());
    h += lacing;
    h += body;
    const std::uint32_t crc = ogg_crc(h);
    for (int i = 0; i < 4; ++i) {
      h[22 + i] = static_cast<char>(crc >> 8 * i);
    }
    buf += h;
  }
};

bool opus_error(std::string &why, const char *what, int code) {
  why = std::string("opus ") + what + ": " + opus_strerror(code);
  return false;
}

// Decodes the packets of the OGG in `in` into `out`, after the 44 bytes the header takes. `samples` is what the
// stream holds: the pre-skip is not written, and the padding after the last granule is, to be cut by the caller.
bool decode(std::FILE *in, std::FILE *out, std::string &why, std::int64_t &samples) {
  std::unique_ptr<OpusDecoder, decltype(&opus_decoder_destroy)> dec(nullptr, opus_decoder_destroy);
  std::vector<std::int16_t> pcm(5760);  // 120 ms at 48 kHz, the longest an Opus packet holds
  std::string packet, bytes(44, '\0');
  std::int64_t pre_skip = 0, end = -1, pos = 0, written = 0, packets = 0;  // pos: samples decoded before this packet

  auto got = [&]() {  // a whole packet is in `packet`
    if (packets++ == 0) {
      if (packet.size() < 19 || packet.compare(0, 8, "OpusHead") != 0 || packet[9] < 1 || packet[9] > 2) {
        why = "opus: no OpusHead";
        return false;
      }
      pre_skip = static_cast<std::int64_t>(get(reinterpret_cast<const unsigned char *>(packet.data()) + 10, 2));
      int err = 0;
      // One channel for a stereo stream too: libopus mixes it down.
      dec.reset(opus_decoder_create(48000, 1, &err));
      return dec ? true : opus_error(why, "decoder", err);
    }
    if (packets == 2 || packet.empty()) {  // OpusTags
      return true;
    }
    const int n = opus_decode(dec.get(), reinterpret_cast<const unsigned char *>(packet.data()),
                              static_cast<opus_int32>(packet.size()), pcm.data(), static_cast<int>(pcm.size()), 0);
    if (n < 0) {
      return opus_error(why, "decode", n);
    }
    bytes.clear();
    for (std::int64_t i = std::max<std::int64_t>(pos, pre_skip) - pos; i < n; ++i) {
      put(bytes, static_cast<std::uint16_t>(pcm[i]), 2);
      ++written;
    }
    pos += n;
    if (std::fwrite(bytes.data(), 1, bytes.size(), out) != bytes.size()) {
      why = "cannot write the WAV";
      return false;
    }
    return true;
  };

  if (std::fwrite(bytes.data(), 1, bytes.size(), out) != bytes.size()) {  // the header's place
    why = "cannot write the WAV";
    return false;
  }
  unsigned char h[27];
  while (std::fread(h, 1, sizeof h, in) == sizeof h) {
    if (std::memcmp(h, "OggS", 4) != 0) {
      why = "ogg: bad page";
      return false;
    }
    unsigned char lacing[255];
    std::size_t size = 0;
    if (std::fread(lacing, 1, h[26], in) != h[26]) {
      why = "ogg: short page";
      return false;
    }
    for (int i = 0; i < h[26]; ++i) {
      size += lacing[i];
    }
    std::string body(size, '\0');
    if (std::fread(&body[0], 1, size, in) != size) {
      why = "ogg: short page";
      return false;
    }
    end = static_cast<std::int64_t>(get(h + 6, 8));  // of the last page, in the end
    std::size_t at = 0;
    for (int i = 0; i < h[26]; ++i) {
      packet.append(body, at, lacing[i]);
      at += lacing[i];
      if (lacing[i] < 255) {  // a packet ends; one of 255 goes on in the next page
        if (!got()) {
          return false;
        }
        packet.clear();
      }
    }
  }
  if (packets == 0) {
    why = "ogg: no pages";
    return false;
  }
  // The last granule is where the audio ends. One before the pre-skip is wrong: all of it plays.
  samples = end >= pre_skip ? std::min(written, end - pre_skip) : written;
  return true;
}

}  // namespace

bool wav_read(const std::string &path, std::vector<std::int16_t> &mono, int &rate, std::string &why) {
  mono.clear();
  File f(std::fopen(path.c_str(), "rb"));
  if (!f) {
    why = "wav: cannot open the file";
    return false;
  }
  unsigned char h[12];
  if (std::fread(h, 1, sizeof h, f.get()) != sizeof h || std::memcmp(h, "RIFF", 4) != 0 ||
      std::memcmp(h + 8, "WAVE", 4) != 0) {
    why = "wav: not RIFF/WAVE";
    return false;
  }
  int format = 0, channels = 0, bits = 0;
  rate = 0;
  unsigned char c[8];
  while (std::fread(c, 1, sizeof c, f.get()) == sizeof c) {
    const std::uint64_t size = get(c + 4, 4);
    std::uint64_t skip = size + (size & 1);  // chunks are padded to an even size
    if (std::memcmp(c, "fmt ", 4) == 0) {
      unsigned char b[40];
      const std::size_t n = std::min<std::uint64_t>(size, sizeof b);
      if (n < 16 || std::fread(b, 1, n, f.get()) != n) {
        why = "wav: short fmt chunk";
        return false;
      }
      format = static_cast<int>(get(b, 2));
      channels = static_cast<int>(get(b + 2, 2));
      rate = static_cast<int>(get(b + 4, 4));
      bits = static_cast<int>(get(b + 14, 2));
      if (format == 0xfffe && n >= 26) {  // extensible: the real format is the subformat's
        format = static_cast<int>(get(b + 24, 2));
      }
      skip -= n;
    } else if (std::memcmp(c, "data", 4) == 0) {
      if (format != 1 || bits != 16 || channels < 1 || channels > 8 || rate < 1) {
        why = "wav: unsupported format " + std::to_string(format) + ", " + std::to_string(bits) + " bit, " +
              std::to_string(channels) + " ch, " + std::to_string(rate) + " Hz";
        return false;
      }
      const std::size_t frame = 2 * channels;
      std::vector<unsigned char> buf(frame * 4096);
      for (std::size_t n; (n = std::fread(buf.data(), 1, buf.size(), f.get())) > 0;) {
        for (std::size_t at = 0; at + frame <= n; at += frame) {  // a partial frame at the end is dropped
          int sum = 0;
          for (int ch = 0; ch < channels; ++ch) {
            sum += static_cast<std::int16_t>(get(&buf[at + 2 * ch], 2));
          }
          mono.push_back(static_cast<std::int16_t>(sum / channels));
        }
      }
      return true;
    }
    if (skip > 0x7fffffff || std::fseek(f.get(), static_cast<long>(skip), SEEK_CUR) != 0) {
      break;
    }
  }
  why = "wav: no data chunk";
  return false;
}

bool ogg_opus_encode(const std::vector<std::int16_t> &mono, int rate, const std::string &out_path, std::string &why) {
  if (rate != 8000 && rate != 12000 && rate != 16000 && rate != 24000 && rate != 48000) {
    why = "opus: a sample rate of " + std::to_string(rate) + " Hz (8, 12, 16, 24 or 48 kHz only)";
    return false;
  }
  int err = 0;
  std::unique_ptr<OpusEncoder, decltype(&opus_encoder_destroy)> enc(
      opus_encoder_create(rate, 1, OPUS_APPLICATION_VOIP, &err), opus_encoder_destroy);
  if (!enc) {
    return opus_error(why, "encoder", err);
  }
  // VOIP, voice, 20 kbps (more costs upload time, and a phone's microphone gains nothing), complexity 5 (about half the
  // CPU of 10, no audible difference on speech).
  opus_encoder_ctl(enc.get(), OPUS_SET_BITRATE(20000));
  opus_encoder_ctl(enc.get(), OPUS_SET_COMPLEXITY(5));
  opus_encoder_ctl(enc.get(), OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
  opus_int32 lookahead = 0;
  opus_encoder_ctl(enc.get(), OPUS_GET_LOOKAHEAD(&lookahead));

  // The decoder drops the encoder's lookahead (pre-skip) at the start and everything past the last page's granule at
  // the end, so the input runs on in zeros until the lookahead is flushed.
  const std::int64_t pre_skip = lookahead * std::int64_t{48000} / rate;
  const std::int64_t end = pre_skip + static_cast<std::int64_t>(mono.size()) * 48000 / rate;
  const std::int64_t frames = (end + 959) / 960;  // of 20 ms
  const std::size_t frame = rate / 50;

  Ogg ogg;
  std::string head = "OpusHead";
  head += '\1';  // version
  head += '\1';  // one channel
  put(head, pre_skip, 2);
  put(head, rate, 4);
  put(head, 0, 2);  // output gain
  head += '\0';     // channel mapping family 0
  ogg.page({head}, 0, kFirst);
  const std::string vendor = std::string("Tengram, ") + opus_get_version_string();
  std::string tags = "OpusTags";
  put(tags, vendor.size(), 4);
  tags += vendor;
  put(tags, 0, 4);  // no comments
  ogg.page({tags}, 0, 0);

  std::vector<std::string> page;
  std::size_t lacing = 0;  // values the page has
  std::vector<std::int16_t> padded(frame);
  unsigned char out[1500];
  for (std::int64_t i = 0; i < frames; ++i) {
    const std::size_t at = static_cast<std::size_t>(i) * frame;
    const std::int16_t *in = padded.data();
    if (at + frame <= mono.size()) {
      in = &mono[at];
    } else {  // the last frames, padded with zeros
      std::fill(padded.begin(), padded.end(), 0);
      if (at < mono.size()) {
        std::copy(mono.begin() + at, mono.end(), padded.begin());
      }
    }
    const int n = opus_encode(enc.get(), in, static_cast<int>(frame), out, sizeof out);
    if (n < 0) {
      return opus_error(why, "encode", n);
    }
    // About a second of packets to a page; a page has 255 lacing values at most.
    if (page.size() == 50 || lacing + n / 255 + 1 > 255) {
      ogg.page(page, i * 960, 0);
      page.clear();
      lacing = 0;
    }
    page.emplace_back(reinterpret_cast<const char *>(out), n);
    lacing += n / 255 + 1;
  }
  ogg.page(page, end, kLast);

  File f(std::fopen(out_path.c_str(), "wb"));
  if (!f || std::fwrite(ogg.buf.data(), 1, ogg.buf.size(), f.get()) != ogg.buf.size()) {
    why = "opus: cannot write " + out_path;
    return false;
  }
  if (std::fclose(f.release()) != 0) {
    unlink(out_path.c_str());
    why = "opus: cannot write " + out_path;
    return false;
  }
  return true;
}

bool ogg_opus_decode(const std::string &in_path, const std::string &wav_path, std::string &why) {
  File in(std::fopen(in_path.c_str(), "rb"));
  if (!in) {
    why = "opus: cannot open " + in_path;
    return false;
  }
  const std::string tmp = wav_path + ".tmp";
  File out(std::fopen(tmp.c_str(), "wb"));
  if (!out) {
    why = "wav: cannot write " + wav_path;
    return false;
  }
  std::int64_t samples = 0;
  bool ok = decode(in.get(), out.get(), why, samples);
  if (ok) {  // the header is written last, now that the length is known
    const std::string header = wav_header(samples, 48000);
    ok = std::fflush(out.get()) == 0 && ftruncate(fileno(out.get()), 44 + 2 * samples) == 0 &&
         std::fseek(out.get(), 0, SEEK_SET) == 0 &&
         std::fwrite(header.data(), 1, header.size(), out.get()) == header.size();
    why = ok ? "" : "wav: cannot write " + wav_path;
  }
  if (std::fclose(out.release()) != 0 && ok) {
    ok = false;
    why = "wav: cannot write " + wav_path;
  }
  if (ok && std::rename(tmp.c_str(), wav_path.c_str()) != 0) {
    ok = false;
    why = "wav: cannot rename " + tmp;
  }
  if (!ok) {
    unlink(tmp.c_str());
  }
  return ok;
}

std::string waveform_bits(const std::vector<std::int16_t> &mono) {
  constexpr int kSamples = 100;
  int peak[kSamples] = {};
  int loudest = 0;
  const std::size_t n = mono.size();
  for (int i = 0; i < kSamples; ++i) {
    const std::size_t lo = i * n / kSamples;
    for (std::size_t k = lo; k < std::min(n, std::max((i + 1) * n / kSamples, lo + 1)); ++k) {
      peak[i] = std::max(peak[i], std::abs(mono[k]));
    }
    loudest = std::max(loudest, peak[i]);
  }
  std::string bits((kSamples * 5 + 7) / 8, '\0');
  for (int i = 0; i < kSamples && loudest > 0; ++i) {
    const int sample = peak[i] * 31 / loudest;
    for (int bit = 0; bit < 5; ++bit) {
      const int k = i * 5 + bit;
      bits[k / 8] = static_cast<char>(bits[k / 8] | ((sample >> bit & 1) << (k % 8)));
    }
  }
  return bits;
}
