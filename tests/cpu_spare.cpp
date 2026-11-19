// How much CPU the rest of the phone leaves: two spinners at the lowest priority (one per core of the Classic and the
// Q10) count loops, and every second the count is printed. Run it before something starts and while it runs: the drop,
// as a share of the idle count, is what that something costs (200 % = both cores). pidin's times are not used: QNX
// charges CPU by clock tick, which misreads threads that wake every 10 ms as an audio call's do.
//   cpu_spare [seconds]
#include <pthread.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

std::atomic<long long> spins{0};

void spin() {
  sched_param p{};
  p.sched_priority = 1;
  pthread_setschedparam(pthread_self(), SCHED_OTHER, &p);
  for (long long n = 1;; n++)
    if (n % 1024 == 0) spins.fetch_add(1024, std::memory_order_relaxed);
}

int main(int argc, char **argv) {
  const int seconds = argc > 1 ? std::atoi(argv[1]) : 30;
  std::thread(spin).detach();
  std::thread(spin).detach();
  long long last = 0;
  for (int i = 0; i < seconds; i++) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    const long long now = spins.load();
    std::printf("%d %lld\n", i + 1, now - last);
    std::fflush(stdout);
    last = now;
  }
}
