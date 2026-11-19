// tgcalls' platform for BlackBerry 10. Voice only for now: the video factories offer no codec, so no software
// codec (libvpx, openh264, ffmpeg) is linked and a call never negotiates video. The phone's hardware H.264
// (HyperBerry's OMX decoder and camera encoder) is where video comes from later.
#include "platform/PlatformInterface.h"

#include "api/video_codecs/video_decoder.h"
#include "api/video_codecs/video_encoder.h"

namespace tgcalls {
namespace {

class NoEncoders final : public webrtc::VideoEncoderFactory {
 public:
  std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override { return {}; }
  std::unique_ptr<webrtc::VideoEncoder> CreateVideoEncoder(const webrtc::SdpVideoFormat &) override { return nullptr; }
};

class NoDecoders final : public webrtc::VideoDecoderFactory {
 public:
  std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override { return {}; }
  std::unique_ptr<webrtc::VideoDecoder> Create(const webrtc::Environment &, const webrtc::SdpVideoFormat &) override {
    return nullptr;
  }
};

class QnxInterface final : public PlatformInterface {
 public:
  std::unique_ptr<webrtc::VideoEncoderFactory> makeVideoEncoderFactory(bool, bool) override {
    return std::make_unique<NoEncoders>();
  }
  std::unique_ptr<webrtc::VideoDecoderFactory> makeVideoDecoderFactory() override {
    return std::make_unique<NoDecoders>();
  }
  bool supportsEncoding(const std::string &) override { return false; }
  webrtc::scoped_refptr<webrtc::VideoTrackSourceInterface> makeVideoSource(rtc::Thread *, rtc::Thread *) override {
    return nullptr;
  }
  void adaptVideoSource(webrtc::scoped_refptr<webrtc::VideoTrackSourceInterface>, int, int, int) override {}
  std::unique_ptr<VideoCapturerInterface> makeVideoCapturer(
      webrtc::scoped_refptr<webrtc::VideoTrackSourceInterface>, std::string, std::function<void(VideoState)>,
      std::function<void(PlatformCaptureInfo)>, std::shared_ptr<PlatformContext>, std::pair<int, int> &) override {
    return nullptr;
  }
};

}  // namespace

std::unique_ptr<PlatformInterface> CreatePlatformInterface() {
  return std::make_unique<QnxInterface>();
}

}  // namespace tgcalls
