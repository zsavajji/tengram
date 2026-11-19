// tgcalls on the phone without Telegram (gate T0): registers the call versions, starts an instance of the
// newest one with a random key and no servers, a 440 Hz tone as the microphone, and runs it for a few seconds.
// It proves the libraries link, their threads and sockets start on QNX, the audio module ticks (Record/Render
// counts) and signalling is produced. Prints what it saw; exit 0 when the tone was taken and signalling came out.
//   calls_check [seconds [rate [ns agc [version]]]]   (defaults: 9 48000 1 1, the newest version)
// CPU is measured by what it leaves: two spinners at the lowest priority count loops for 3 s before the instance
// starts and for the run's last 3 s; the drop is what the call took, in % of one core. QNX charges CPU time by
// clock tick, and a thread waking every 10 ms on the tick is what that accounting gets wrong, so pidin's times
// aren't used.
#include "tgcalls/FakeAudioDeviceModule.h"
#include "tgcalls/Instance.h"
#include "tgcalls/InstanceImpl.h"
#include "tgcalls/v2/InstanceV2Impl.h"
#include "tgcalls/v2/InstanceV2ReferenceImpl.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <pthread.h>
#include <thread>

namespace {

std::atomic<int> recorded{0}, rendered{0}, signaling{0};

int rate = 48000;

class Tone final : public tgcalls::FakeAudioDeviceModule::Recorder {
 public:
  tgcalls::AudioFrame Record() override {
    const int n = rate / 100;
    for (int i = 0; i < n; i++, n_++) buf_[i] = int16_t(8000 * std::sin(2 * M_PI * 440 * n_ / double(rate)));
    recorded++;
    return {buf_, size_t(n), 2, 1, uint32_t(rate), 0, 0};
  }

 private:
  int16_t buf_[480];
  long n_ = 0;
};

class Sink final : public tgcalls::FakeAudioDeviceModule::Renderer {
 public:
  bool Render(const tgcalls::AudioFrame &) override {
    rendered++;
    return true;
  }
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
  const int seconds = argc > 1 ? std::atoi(argv[1]) : 9;
  if (argc > 2) rate = std::atoi(argv[2]);
  const bool ns = argc > 3 ? std::atoi(argv[3]) : 1, agc = argc > 4 ? std::atoi(argv[4]) : 1;
  tgcalls::Register<tgcalls::InstanceImpl>();
  tgcalls::Register<tgcalls::InstanceV2Impl>();
  tgcalls::Register<tgcalls::InstanceV2ReferenceImpl>();
  auto versions = tgcalls::Meta::Versions();
  std::printf("versions:");
  for (auto &v : versions) std::printf(" %s", v.c_str());
  std::printf("\nmax layer: %d\n", tgcalls::Meta::MaxLayer());
  if (versions.empty()) return 1;

  std::thread s1(spinner), s2(spinner);
  const long long idle = spinsOver(3);

  auto key = std::make_shared<std::array<uint8_t, tgcalls::EncryptionKey::kSize>>();
  for (auto &b : *key) b = uint8_t(std::rand());
  tgcalls::FakeAudioDeviceModule::Options audio;
  audio.samples_per_sec = rate;
  audio.num_channels = 1;
  tgcalls::Descriptor d{
      .version = argc > 5 ? argv[5] : versions.back(),
      .config = tgcalls::Config{.initializationTimeout = 30, .receiveTimeout = 20, .enableP2P = true,
                                .enableNS = ns, .enableAGC = agc, .maxApiLayer = tgcalls::Meta::MaxLayer()},
      .encryptionKey = tgcalls::EncryptionKey(key, true),
      .stateUpdated = [](tgcalls::State s) { std::printf("state %d\n", int(s)); },
      .signalingDataEmitted = [](const std::vector<uint8_t> &data) {
        signaling++;
        std::printf("signaling %zu bytes\n", data.size());
      },
      .createAudioDeviceModule =
          tgcalls::FakeAudioDeviceModule::Creator(std::make_shared<Sink>(), std::make_shared<Tone>(), audio),
  };
  std::printf("version %s\n", d.version.c_str());
  auto instance = tgcalls::Meta::Create(d.version, std::move(d));
  if (!instance) return 1;
  if (seconds > 3) std::this_thread::sleep_for(std::chrono::seconds(seconds - 3));
  const long long busy = spinsOver(3);
  spinning = false;
  s1.join();
  s2.join();
  std::printf("cpu: %.1f%% of one core (spins %lld idle, %lld with the call)\n", 200.0 * (idle - busy) / idle, idle,
              busy);
  std::promise<void> done;
  instance->stop([&](tgcalls::FinalState) { done.set_value(); });
  done.get_future().wait_for(std::chrono::seconds(5));
  instance.reset();
  std::printf("recorded %d frames, rendered %d, signaling %d messages\n", recorded.load(), rendered.load(),
              signaling.load());
  return recorded > 0 && signaling > 0 ? 0 : 2;
}
