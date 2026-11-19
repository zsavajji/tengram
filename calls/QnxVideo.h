// What the engine sees of tgcalls' video on BlackBerry 10 (calls/QnxInterface.cpp): the UI's camera in, the peer's
// video out, the keyframe requests both ways. Free of WebRTC's headers and defines: the engine compiles without them.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace tgcalls::qnx {

// Where QnxInterface.cpp logs, from any thread of tgcalls and WebRTC. Without one it logs nothing.
void setLog(std::function<void(const std::string &)> log);

// The peer's video: each H.264 access unit (Annex-B) as WebRTC assembled it, on WebRTC's decoder thread. rotation is
// clockwise degrees (0, 90, 180 or 270) from the RTP video-orientation extension; width and height are 0 when unknown.
using UnitSink = std::function<void(const std::uint8_t *data, std::size_t size, bool keyframe, int rotation, int width,
                                    int height)>;
// An empty function clears it; once this returns, no unit is delivered to the old one.
void setReceivedUnits(UnitSink sink);

// Our camera: one access unit (Annex-B) from the UI, rotation = the clockwise degrees (0, 90, 180 or 270) that turn it
// upright. It goes to the call's capturer (the capture device "camera") when there is one and it is active, and is
// dropped otherwise. Any thread; once a capturer is gone no unit reaches it.
void pushUnit(const std::uint8_t *data, std::size_t size, bool keyframe, int rotation);
// Called on WebRTC's encoder thread when it wants a keyframe from our camera (a new receiver, a lost packet): once for
// each run of delta units. An empty function clears it; once this returns, it is not called any more.
void setKeyframeRequests(std::function<void()> f);
// Called on WebRTC's encoder thread each time it sets our target rate (its bandwidth estimate, capped by tgcalls'),
// with the sum over its layers in bit/s: what our camera should encode at. An empty function clears it; once this
// returns, it is not called any more.
void setTargetRates(std::function<void(int bitsPerSecond)> f);
// The size of the units pushUnit gets from now on (the camera restarts at another size: CallVideo's type-3 record).
// WebRTC reconfigures its encoder by itself when the size of the frames changes. 640x480 until told. Any thread.
void setCameraSize(int width, int height);
// The UI's decoder lost its place: the next unit WebRTC hands the decoder is answered with a keyframe request to the
// sender. Any thread.
void requestPeerKeyframe();

}  // namespace tgcalls::qnx
