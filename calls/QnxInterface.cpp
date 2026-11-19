// tgcalls' platform for BlackBerry 10. Video passes through: no software codec (libvpx, openh264, ffmpeg) is linked, so
// WebRTC only packetizes the H.264 access units it is handed and depacketizes the ones it receives. The phone's
// hardware H.264 (the UI's camera encoder and OMX decoder) is where they come from and go to, in the UI process, over
// the video link (engine/calls.cpp): the capture device "camera" is that link (LinkCapturer; the UI's units come in
// through pushUnit), the peer's units go to a sink, and the keyframe requests go both ways (calls/QnxVideo.h).
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <memory>
#include <mutex>
#include <vector>

#include "QnxVideo.h"
#include "VideoCaptureInterface.h"
#include "VideoCapturerInterface.h"
#include "absl/strings/match.h"
#include "api/video/encoded_image.h"
#include "api/video/i420_buffer.h"
#include "api/video/video_frame.h"
#include "api/video/video_frame_buffer.h"
#include "media/base/adapted_video_track_source.h"
#include "modules/video_coding/codecs/interface/common_constants.h"
#include "modules/video_coding/include/video_codec_interface.h"
#include "modules/video_coding/include/video_error_codes.h"
#include "platform/PlatformInterface.h"
#include "rtc_base/time_utils.h"

namespace tgcalls {
namespace qnx {
namespace {

// ponytail: one call at a time, so one log, one sink, one capturer and one keyframe request for the whole process; two
// calls at once would need them per call (a call's id through the factories). The engine takes one call at a time.
std::mutex g_log_mutex;
std::function<void(const std::string &)> g_log;  // guarded by g_log_mutex
std::mutex g_sink_mutex;
UnitSink g_sink;  // guarded by g_sink_mutex, which is held while the sink runs
std::mutex g_requests_mutex;
std::function<void()> g_keyframe_requests;  // guarded by g_requests_mutex, which is held while it runs
std::mutex g_rates_mutex;
std::function<void(int)> g_target_rates;  // guarded by g_rates_mutex, which is held while it runs
// The UI's decoder lost its place: the next unit the decoder is given answers with a keyframe request.
std::atomic<bool> g_peer_keyframe{false};
// The size of the units pushUnit gets: the UI's camera says (setCameraSize) each time it starts, and a unit carries the
// size it came at. 640x480 until told. (Not read from the SPS: the record would have to be parsed for it.)
std::atomic<int> g_camera_width{640}, g_camera_height{480};

void logf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void logf(const char *fmt, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  std::lock_guard<std::mutex> lock(g_log_mutex);
  if (g_log) {
    g_log(buf);
  }
}

// The one video format: H.264 as the phone's hardware makes it (constrained baseline, level 3.1, one NAL unit per
// packet or fragmented). Telegram's apps offer the same.
webrtc::SdpVideoFormat h264Format() {
  return webrtc::SdpVideoFormat(
      "H264", {{"profile-level-id", "42e01f"}, {"level-asymmetry-allowed", "1"}, {"packetization-mode", "1"}});
}

bool isH264(const std::string &name) {
  return absl::EqualsIgnoreCase(name, "H264");
}

// The counts of the summary line a codec logs every 5 s.
struct Window {
  void add(std::size_t size, bool key) {
    ++units;
    keyframes += key ? 1 : 0;
    bytes += size;
  }
  bool over() const {
    return std::chrono::steady_clock::now() - start >= std::chrono::seconds(5);
  }
  double seconds() const {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  }
  int units = 0, keyframes = 0;
  std::size_t bytes = 0;
  std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
};

// ---- the frames WebRTC is given: an access unit in a native buffer, for the pass-through encoder

class EncodedBuffer : public webrtc::VideoFrameBuffer {
 public:
  EncodedBuffer(std::vector<std::uint8_t> bytes, int width, int height, bool keyframe)
      : bytes_(std::move(bytes)), width_(width), height_(height), keyframe_(keyframe) {
  }
  Type type() const override {
    return Type::kNative;
  }
  int width() const override {
    return width_;
  }
  int height() const override {
    return height_;
  }
  // Nothing on the send path asks for pixels: the encoder passes the bytes on. Black, if something does.
  webrtc::scoped_refptr<webrtc::I420BufferInterface> ToI420() override {
    if (!black_) {
      if (!warned_.exchange(true)) {
        logf("an access unit was asked for as pixels (ToI420)");
      }
      black_ = webrtc::I420Buffer::Create(width_, height_);
      webrtc::I420Buffer::SetBlack(black_.get());
    }
    return black_;
  }
  const std::vector<std::uint8_t> &bytes() const {
    return bytes_;
  }
  bool keyframe() const {
    return keyframe_;
  }

 private:
  const std::vector<std::uint8_t> bytes_;
  const int width_, height_;
  const bool keyframe_;
  webrtc::scoped_refptr<webrtc::I420Buffer> black_;
  std::atomic<bool> warned_{false};
};

// WebRTC's side of the camera. Frames go straight to the sinks: scaling or dropping would be done to pixels that are
// not there.
class EncodedSource : public rtc::AdaptedVideoTrackSource {
 public:
  void push(const webrtc::VideoFrame &frame) {
    OnFrame(frame);
  }
  SourceState state() const override {
    return kLive;
  }
  bool remote() const override {
    return false;
  }
  bool is_screencast() const override {
    return false;
  }
  absl::optional<bool> needs_denoising() const override {
    return absl::nullopt;
  }
};

// ---- the camera: the UI's, over the video link. Its units come in through pushUnit, on the link's reader thread.

webrtc::VideoRotation rotationOf(int degrees) {
  switch (degrees) {
    case 90:
      return webrtc::kVideoRotation_90;
    case 180:
      return webrtc::kVideoRotation_180;
    case 270:
      return webrtc::kVideoRotation_270;
    default:
      return webrtc::kVideoRotation_0;
  }
}

class LinkCapturer;
std::mutex g_capturer_mutex;
LinkCapturer *g_capturer = nullptr;  // the process's current capturer; guarded by g_capturer_mutex

class LinkCapturer final : public VideoCapturerInterface {
 public:
  LinkCapturer(webrtc::scoped_refptr<EncodedSource> source, std::function<void(VideoState)> stateUpdated,
               std::function<void(PlatformCaptureInfo)> captureInfoUpdated)
      : source_(std::move(source)),
        stateUpdated_(std::move(stateUpdated)),
        captureInfoUpdated_(std::move(captureInfoUpdated)) {
    std::lock_guard<std::mutex> lock(g_capturer_mutex);
    g_capturer = this;
  }
  ~LinkCapturer() override {
    std::lock_guard<std::mutex> lock(g_capturer_mutex);  // a push that is under way ends first
    if (g_capturer == this) {
      g_capturer = nullptr;
    }
  }

  void setState(VideoState state) override {
    active_ = state == VideoState::Active;
    if (stateUpdated_) {
      stateUpdated_(state);
    }
  }
  void setPreferredCaptureAspectRatio(float) override {
  }
  void setUncroppedOutput(std::shared_ptr<rtc::VideoSinkInterface<webrtc::VideoFrame>>) override {
  }
  int getRotation() override {
    return rotation_;
  }

  // With g_capturer_mutex held, on the pushing thread: pushUnit's arguments; the frame is the unit in a native buffer.
  void push(const std::uint8_t *data, std::size_t size, bool keyframe, int rotation) {
    if (!active_ || size == 0) {
      return;
    }
    if (rotation != rotation_) {
      rotation_ = rotation;
      if (captureInfoUpdated_) {
        captureInfoUpdated_({false, rotation});
      }
    }
    source_->push(webrtc::VideoFrame::Builder()
                      .set_video_frame_buffer(rtc::make_ref_counted<EncodedBuffer>(
                          std::vector<std::uint8_t>(data, data + size), g_camera_width, g_camera_height, keyframe))
                      .set_timestamp_us(rtc::TimeMicros())
                      .set_rotation(rotationOf(rotation))
                      .build());
  }

 private:
  const webrtc::scoped_refptr<EncodedSource> source_;
  const std::function<void(VideoState)> stateUpdated_;
  const std::function<void(PlatformCaptureInfo)> captureInfoUpdated_;
  // Atomic: tgcalls' threads ask while the pushing thread holds the mutex.
  std::atomic<bool> active_{false};
  std::atomic<int> rotation_{0};  // degrees clockwise, 0, 90, 180 or 270: the last unit's
};

// ---- the pass-through encoder: the unit WebRTC was given is the encoded frame

class PassthroughEncoder final : public webrtc::VideoEncoder {
 public:
  using webrtc::VideoEncoder::InitEncode;

  int InitEncode(const webrtc::VideoCodec *codec, const Settings &) override {
    logf("encoder: %dx%d, at most %u kbit/s and %u fps", codec->width, codec->height,
         static_cast<unsigned>(codec->maxBitrate), static_cast<unsigned>(codec->maxFramerate));
    return WEBRTC_VIDEO_CODEC_OK;
  }
  int32_t RegisterEncodeCompleteCallback(webrtc::EncodedImageCallback *callback) override {
    callback_ = callback;
    return WEBRTC_VIDEO_CODEC_OK;
  }
  int32_t Release() override {
    callback_ = nullptr;
    return WEBRTC_VIDEO_CODEC_OK;
  }

  int32_t Encode(const webrtc::VideoFrame &frame, const std::vector<webrtc::VideoFrameType> *types) override {
    const auto buffer = frame.video_frame_buffer();
    if (buffer->type() != webrtc::VideoFrameBuffer::Type::kNative) {
      if (!warned_) {
        warned_ = true;
        logf("encoder: a frame that is not an access unit (buffer type %d)", static_cast<int>(buffer->type()));
      }
      return WEBRTC_VIDEO_CODEC_ERROR;
    }
    if (!callback_) {
      return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
    }
    const auto &unit = *static_cast<const EncodedBuffer *>(buffer.get());
    // WebRTC wants a keyframe (a new receiver, a lost packet) and this unit is not one: the camera is asked for one,
    // once for each run of delta units. This one goes out as it is.
    if (unit.keyframe()) {
      pending_ = false;
    } else if (types &&
               std::find(types->begin(), types->end(), webrtc::VideoFrameType::kVideoFrameKey) != types->end()) {
      ++requests_;
      if (!pending_) {
        pending_ = true;
        std::lock_guard<std::mutex> lock(g_requests_mutex);
        if (g_keyframe_requests) {
          g_keyframe_requests();
        }
      }
    }

    webrtc::EncodedImage image;
    image.SetEncodedData(webrtc::EncodedImageBuffer::Create(unit.bytes().data(), unit.bytes().size()));
    image._encodedWidth = static_cast<uint32_t>(unit.width());
    image._encodedHeight = static_cast<uint32_t>(unit.height());
    image._frameType =
        unit.keyframe() ? webrtc::VideoFrameType::kVideoFrameKey : webrtc::VideoFrameType::kVideoFrameDelta;
    image.SetRtpTimestamp(frame.timestamp());
    image.capture_time_ms_ = frame.render_time_ms();
    image.rotation_ = frame.rotation();
    webrtc::CodecSpecificInfo info;
    info.codecType = webrtc::kVideoCodecH264;
    info.codecSpecific.H264.packetization_mode = webrtc::H264PacketizationMode::NonInterleaved;
    info.codecSpecific.H264.temporal_idx = webrtc::kNoTemporalIdx;
    info.codecSpecific.H264.base_layer_sync = false;
    info.codecSpecific.H264.idr_frame = unit.keyframe();
    callback_->OnEncodedImage(image, &info);

    window_.add(unit.bytes().size(), unit.keyframe());
    if (window_.over()) {
      logf("encoder: %d units (%d keyframes), %zu kB in %.1f s, %d keyframe requests, target %u kbit/s at %.0f fps",
           window_.units, window_.keyframes, window_.bytes / 1024, window_.seconds(), requests_, target_kbps_, fps_);
      window_ = Window();
      requests_ = 0;
    }
    return WEBRTC_VIDEO_CODEC_OK;
  }

  void SetRates(const RateControlParameters &parameters) override {
    target_kbps_ = static_cast<unsigned>(parameters.target_bitrate.get_sum_kbps());
    fps_ = parameters.framerate_fps;
    // The UI's camera follows it (the engine passes it on over the video link).
    std::lock_guard<std::mutex> lock(g_rates_mutex);
    if (g_target_rates) {
      g_target_rates(static_cast<int>(parameters.target_bitrate.get_sum_bps()));
    }
  }
  EncoderInfo GetEncoderInfo() const override {
    EncoderInfo info;
    info.supports_native_handle = true;
    info.is_hardware_accelerated = true;
    info.has_trusted_rate_controller = true;
    info.implementation_name = "BB10 pass-through";
    return info;
  }

 private:
  webrtc::EncodedImageCallback *callback_ = nullptr;
  Window window_;
  int requests_ = 0;
  bool pending_ = false;  // the camera was asked for a keyframe and none has come
  bool warned_ = false;
  unsigned target_kbps_ = 0;
  double fps_ = 0;
};

// ---- the pass-through decoder: the unit WebRTC assembled goes to the sink; a black frame stands in for the picture

class PassthroughDecoder final : public webrtc::VideoDecoder {
 public:
  using webrtc::VideoDecoder::Decode;

  bool Configure(const Settings &) override {
    logf("decoder: configured");
    return true;
  }
  int32_t Decode(const webrtc::EncodedImage &image, int64_t) override {
    if (!callback_) {
      return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
    }
    const bool key = image._frameType == webrtc::VideoFrameType::kVideoFrameKey;
    rotation_ = static_cast<int>(image.rotation_);
    width_ = static_cast<int>(image._encodedWidth);
    height_ = static_cast<int>(image._encodedHeight);
    {
      std::lock_guard<std::mutex> lock(g_sink_mutex);
      if (g_sink) {
        g_sink(image.data(), image.size(), key, rotation_, width_, height_);
      }
    }
    // WebRTC's receive statistics and sinks count decoded frames.
    if (!black_) {
      black_ = webrtc::I420Buffer::Create(16, 16);
      webrtc::I420Buffer::SetBlack(black_.get());
    }
    webrtc::VideoFrame frame = webrtc::VideoFrame::Builder()
                                   .set_video_frame_buffer(black_)
                                   .set_timestamp_rtp(image.RtpTimestamp())
                                   .set_rotation(image.rotation_)
                                   .build();
    callback_->Decoded(frame);

    window_.add(image.size(), key);
    if (window_.over()) {
      logf("decoder: %d units (%d keyframes), %zu kB in %.1f s, last %dx%d rotated %d", window_.units,
           window_.keyframes, window_.bytes / 1024, window_.seconds(), width_, height_, rotation_);
      window_ = Window();
    }
    if (g_peer_keyframe.exchange(false)) {
      logf("decoder: the UI lost its place, asking the sender for a keyframe");
      return WEBRTC_VIDEO_CODEC_OK_REQUEST_KEYFRAME;  // decoded, plus a keyframe request to the sender
    }
    return WEBRTC_VIDEO_CODEC_OK;
  }
  int32_t RegisterDecodeCompleteCallback(webrtc::DecodedImageCallback *callback) override {
    callback_ = callback;
    return WEBRTC_VIDEO_CODEC_OK;
  }
  int32_t Release() override {
    callback_ = nullptr;
    return WEBRTC_VIDEO_CODEC_OK;
  }
  DecoderInfo GetDecoderInfo() const override {
    DecoderInfo info;
    info.implementation_name = "BB10 pass-through";
    info.is_hardware_accelerated = true;
    return info;
  }
  const char *ImplementationName() const override {
    return "BB10 pass-through";
  }

 private:
  webrtc::DecodedImageCallback *callback_ = nullptr;
  webrtc::scoped_refptr<webrtc::I420Buffer> black_;
  Window window_;
  int rotation_ = 0, width_ = 0, height_ = 0;
};

// ---- the factories

class Encoders final : public webrtc::VideoEncoderFactory {
 public:
  std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override {
    return {h264Format()};
  }
  std::unique_ptr<webrtc::VideoEncoder> CreateVideoEncoder(const webrtc::SdpVideoFormat &format) override {
    logf("an encoder for %s", format.name.c_str());
    if (!isH264(format.name)) {
      return nullptr;
    }
    return std::make_unique<PassthroughEncoder>();
  }
};

class Decoders final : public webrtc::VideoDecoderFactory {
 public:
  std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override {
    return {h264Format()};
  }
  std::unique_ptr<webrtc::VideoDecoder> Create(const webrtc::Environment &,
                                               const webrtc::SdpVideoFormat &format) override {
    logf("a decoder for %s", format.name.c_str());
    if (!isH264(format.name)) {
      return nullptr;
    }
    return std::make_unique<PassthroughDecoder>();
  }
};

}  // namespace

void setLog(std::function<void(const std::string &)> log) {
  std::lock_guard<std::mutex> lock(g_log_mutex);
  g_log = std::move(log);
}

void setReceivedUnits(UnitSink sink) {
  std::lock_guard<std::mutex> lock(g_sink_mutex);
  g_sink = std::move(sink);
}

void setKeyframeRequests(std::function<void()> f) {
  std::lock_guard<std::mutex> lock(g_requests_mutex);
  g_keyframe_requests = std::move(f);
}

void setTargetRates(std::function<void(int)> f) {
  std::lock_guard<std::mutex> lock(g_rates_mutex);
  g_target_rates = std::move(f);
}

void setCameraSize(int width, int height) {
  g_camera_width = width;
  g_camera_height = height;
}

void requestPeerKeyframe() {
  g_peer_keyframe = true;
}

void pushUnit(const std::uint8_t *data, std::size_t size, bool keyframe, int rotation) {
  std::lock_guard<std::mutex> lock(g_capturer_mutex);
  if (g_capturer) {
    g_capturer->push(data, size, keyframe, rotation == 90 || rotation == 180 || rotation == 270 ? rotation : 0);
  }
}

}  // namespace qnx

namespace {

class QnxInterface final : public PlatformInterface {
 public:
  std::unique_ptr<webrtc::VideoEncoderFactory> makeVideoEncoderFactory(bool, bool) override {
    return std::make_unique<qnx::Encoders>();
  }
  std::unique_ptr<webrtc::VideoDecoderFactory> makeVideoDecoderFactory() override {
    return std::make_unique<qnx::Decoders>();
  }
  bool supportsEncoding(const std::string &codecName) override {
    return qnx::isH264(codecName);
  }
  webrtc::scoped_refptr<webrtc::VideoTrackSourceInterface> makeVideoSource(rtc::Thread *, rtc::Thread *) override {
    return rtc::make_ref_counted<qnx::EncodedSource>();
  }
  void adaptVideoSource(webrtc::scoped_refptr<webrtc::VideoTrackSourceInterface>, int, int, int) override {
  }
  std::unique_ptr<VideoCapturerInterface> makeVideoCapturer(
      webrtc::scoped_refptr<webrtc::VideoTrackSourceInterface> source, std::string deviceId,
      std::function<void(VideoState)> stateUpdated, std::function<void(PlatformCaptureInfo)> captureInfoUpdated,
      std::shared_ptr<PlatformContext>, std::pair<int, int> &outResolution) override {
    if (deviceId != "camera") {
      qnx::logf("no capture device %s (only camera)", deviceId.c_str());
      return nullptr;
    }
    if (!source) {
      qnx::logf("no video source");
      return nullptr;
    }
    outResolution = {qnx::g_camera_width.load(), qnx::g_camera_height.load()};
    return std::make_unique<qnx::LinkCapturer>(
        webrtc::scoped_refptr<qnx::EncodedSource>(static_cast<qnx::EncodedSource *>(source.get())),
        std::move(stateUpdated), std::move(captureInfoUpdated));
  }
};

}  // namespace

std::unique_ptr<PlatformInterface> CreatePlatformInterface() {
  return std::make_unique<QnxInterface>();
}

}  // namespace tgcalls
