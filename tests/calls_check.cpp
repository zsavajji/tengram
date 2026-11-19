// A whole call on the phone without Telegram: two tgcalls instances, the caller and the callee, with one key and
// their signalling handed to each other in-process, connect peer to peer over the phone's own network. Each sends a
// tone (440 Hz and 660 Hz) and measures what it hears. It proves the libraries link and run on QNX and that media
// flows both ways, and it is the bench for what a call costs: the version, the audio rate and NS/AGC are arguments.
//   calls_check [seconds [version [rate [ns agc]]]]   (defaults: 26, the newest version, 48000, 1 1)
// CPU is measured by what it leaves: two spinners at the lowest priority count loops for 5 s before the call and, from
// 6 s after it starts (connected by then), to the end of the run; the drop is what the two ends took together, in % of
// one core (one end is about half). Windows under ~10 s vary by several points. QNX
// charges CPU time by clock tick, which misreads threads waking every 10 ms, so pidin's times aren't used.
// Exit 0 when both ends reached Established and heard the other's tone.
#include "tgcalls/FakeAudioDeviceModule.h"
#include "tgcalls/Instance.h"
#include "tgcalls/v2/InstanceV2Impl.h"
#include "tgcalls/v2/InstanceV2ReferenceImpl.h"

#include <pthread.h>

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <future>
#include <mutex>
#include <thread>

namespace {

int rate = 48000;

// engine/calls.cpp's pacing: each tick due 10 ms after the last was due (the fake device's own fixed 10 ms sleep ran
// at 90 % of real time on the Q10).
struct Pace {
  std::int32_t wait_us() {
    const auto now = std::chrono::steady_clock::now();
    next = now - next > std::chrono::milliseconds(100) ? now + std::chrono::milliseconds(10)
                                                       : next + std::chrono::milliseconds(10);
    return std::int32_t(std::max<long long>(0, std::chrono::duration_cast<std::chrono::microseconds>(next - now).count()));
  }
  std::chrono::steady_clock::time_point next{};
};

class Tone final : public tgcalls::FakeAudioDeviceModule::Recorder {
 public:
  explicit Tone(double hz) : hz_(hz) {}
  tgcalls::AudioFrame Record() override {
    const int n = rate / 100;
    for (int i = 0; i < n; i++, n_++) buf_[i] = int16_t(8000 * std::sin(2 * M_PI * hz_ * n_ / double(rate)));
    return {buf_, size_t(n), 2, 1, uint32_t(rate), 0, 0};
  }
  int32_t WaitForUs() override { return pace_.wait_us(); }

 private:
  double hz_;
  int16_t buf_[480];
  long long n_ = 0;
  Pace pace_;
};

// The level of what an end hears, over the whole run after it connected.
class Ear final : public tgcalls::FakeAudioDeviceModule::Renderer {
 public:
  bool Render(const tgcalls::AudioFrame &f) override {
    double s = 0;
    for (size_t i = 0; i < f.num_samples * f.num_channels; i++) s += double(f.audio_samples[i]) * f.audio_samples[i];
    sum = sum + s;
    count = count + f.num_samples * f.num_channels;
    return true;
  }
  int32_t WaitForUs() override { return pace_.wait_us(); }
  Pace pace_;
  int rms() const { return count ? int(std::sqrt(sum / count)) : 0; }
  std::atomic<double> sum{0};
  std::atomic<long long> count{0};
};

// Signalling from one end to the other, delivered on a thread of its own as Telegram's servers would.
struct Pipe {
  std::mutex mu;
  std::condition_variable cv;
  std::deque<std::pair<int, std::vector<uint8_t>>> q;  // (to, data)
  bool stop = false;
};

std::atomic<long long> spins{0};
std::atomic<bool> spinning{true};

void spinner() {
  sched_param p{};
  p.sched_priority = 1;
  pthread_setschedparam(pthread_self(), SCHED_OTHER, &p);
  long long n = 0;
  while (spinning.load(std::memory_order_relaxed))
    if (++n % 1024 == 0) spins.fetch_add(1024, std::memory_order_relaxed);
}

long long spinsOver(int seconds) {
  const long long a = spins.load();
  std::this_thread::sleep_for(std::chrono::seconds(seconds));
  return spins.load() - a;
}

}  // namespace

int main(int argc, char **argv) {
  const int seconds = argc > 1 ? std::atoi(argv[1]) : 26;
  tgcalls::Register<tgcalls::InstanceV2Impl>();
  tgcalls::Register<tgcalls::InstanceV2ReferenceImpl>();
  auto versions = tgcalls::Meta::Versions();
  const std::string version = argc > 2 ? argv[2] : versions.back();
  if (argc > 3) rate = std::atoi(argv[3]);
  const bool ns = argc > 4 ? std::atoi(argv[4]) : 1, agc = argc > 5 ? std::atoi(argv[5]) : 1;
  std::printf("versions:");
  for (auto &v : versions) std::printf(" %s", v.c_str());
  std::printf("\nmax layer %d; this run: %s, %d Hz, ns %d agc %d\n", tgcalls::Meta::MaxLayer(), version.c_str(), rate,
              ns, agc);

  std::thread s1(spinner), s2(spinner);
  const long long idle = spinsOver(5) * 3 / 5;  // per 3 s, as busy below is scaled

  auto key = std::make_shared<std::array<uint8_t, tgcalls::EncryptionKey::kSize>>();
  for (auto &b : *key) b = uint8_t(std::rand());
  Pipe pipe;
  std::unique_ptr<tgcalls::Instance> ends[2];
  std::shared_ptr<Ear> ears[2] = {std::make_shared<Ear>(), std::make_shared<Ear>()};
  std::atomic<bool> established[2] = {false, false};
  for (int i = 0; i < 2; i++) {
    tgcalls::FakeAudioDeviceModule::Options audio;
    audio.samples_per_sec = rate;
    audio.num_channels = 1;
    tgcalls::Descriptor d{
        .version = version,
        .config = tgcalls::Config{.initializationTimeout = 30, .receiveTimeout = 20, .enableP2P = true,
                                  .enableNS = ns, .enableAGC = agc, .maxApiLayer = tgcalls::Meta::MaxLayer()},
        .encryptionKey = tgcalls::EncryptionKey(key, i == 0),
        .stateUpdated = [i, &established](tgcalls::State s) {
          std::printf("end %d: state %d\n", i, int(s));
          if (s == tgcalls::State::Established) established[i] = true;
        },
        .signalingDataEmitted = [i, &pipe](const std::vector<uint8_t> &data) {
          std::lock_guard<std::mutex> lock(pipe.mu);
          pipe.q.push_back({1 - i, data});
          pipe.cv.notify_one();
        },
        .createAudioDeviceModule =
            tgcalls::FakeAudioDeviceModule::Creator(ears[i], std::make_shared<Tone>(i ? 660 : 440), audio),
    };
    ends[i] = tgcalls::Meta::Create(version, std::move(d));
    if (!ends[i]) return 1;
  }
  std::thread courier([&] {
    std::unique_lock<std::mutex> lock(pipe.mu);
    while (true) {
      pipe.cv.wait(lock, [&] { return pipe.stop || !pipe.q.empty(); });
      if (pipe.stop) return;
      auto m = std::move(pipe.q.front());
      pipe.q.pop_front();
      lock.unlock();
      ends[m.first]->receiveSignalingData(m.second);
      lock.lock();
    }
  });

  std::this_thread::sleep_for(std::chrono::seconds(6));
  const int window = seconds > 9 ? seconds - 6 : 3;
  const long long heard0 = ears[0]->count;
  const long long busy = spinsOver(window) * 3 / window;
  const long long heard = ears[0]->count - heard0;
  spinning = false;
  s1.join();
  s2.join();
  std::printf("cpu: %.1f%% of one core for both ends (spins %lld idle, %lld in the call)\n",
              200.0 * (idle - busy) / idle, idle, busy);
  std::printf("heard: end 0 rms %d, end 1 rms %d\n", ears[0]->rms(), ears[1]->rms());
  // The fake device's pace: what end 0 rendered per second of the window, against the rate (real time).
  std::printf("pace: end 0 rendered %.1f%% of real time\n", 100.0 * heard / window / rate);

  {
    std::lock_guard<std::mutex> lock(pipe.mu);
    pipe.stop = true;
    pipe.cv.notify_one();
  }
  courier.join();
  for (auto &end : ends) {
    std::promise<void> done;
    end->stop([&](tgcalls::FinalState) { done.set_value(); });
    done.get_future().wait_for(std::chrono::seconds(5));
    end.reset();
  }
  return established[0] && established[1] && ears[0]->rms() > 100 && ears[1]->rms() > 100 ? 0 : 2;
}
