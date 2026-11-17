// The navigator and the network status, on a thread of their own: BPS is per thread, so the initialization, the
// event requests and every bps_get_event are on this one. Ported from ../hyperberry/engine/navigator_qnx.go and
// net_qnx.go. Only a run with --service starts it. What it receives goes to the main thread through a queue and
// one byte on the wake socket, as the TDLib thread does it.

#include "engine.h"
#include "hub.h"

// netstatus_get_default_interface is deprecated, and it is what net_qnx.go uses and the Q10 proved.
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

#include <bps/bps.h>
#include <bps/navigator.h>
#include <bps/navigator_invoke.h>
#include <bps/netstatus.h>
#include <unistd.h>

#include <mutex>
#include <thread>
#include <vector>

namespace {

struct Event {
  bool invoke = false;                          // a Hub item's action; else the network
  std::string action, mime, uri, data;          // the invocation
  bool available = true, wifi = true;           // the network
};

int wake_fd = -1;
std::mutex mu;
std::vector<Event> queue;  // guarded by mu

void push(Event e) {
  bool was_empty;
  {
    std::lock_guard<std::mutex> lock(mu);
    was_empty = queue.empty();
    queue.push_back(std::move(e));
  }
  if (was_empty) {
    ssize_t n = write(wake_fd, "b", 1);
    (void)n;
  }
}

// Whether the default network interface is not the cellular one, as net_qnx.go asks it: anything else is
// "wifi", and so is any failure, so that nothing is taken for mobile data by accident.
bool default_is_wifi() {
  char *name = nullptr;
  bool wifi = true;
  if (netstatus_get_default_interface(&name) == BPS_SUCCESS && name) {
    netstatus_interface_details_t *details = nullptr;
    if (netstatus_get_interface_details(name, &details) == BPS_SUCCESS && details) {
      wifi = netstatus_interface_get_type(details) != NETSTATUS_INTERFACE_TYPE_CELLULAR;
      netstatus_free_interface_details(&details);
    }
    bps_free(name);
  }
  return wifi;
}

void run() {
  if (bps_initialize() != BPS_SUCCESS || navigator_request_events(0) != BPS_SUCCESS) {
    say("not connected to the navigator");  // started from a shell: nothing to connect to, the engine runs on
    return;
  }
  if (netstatus_request_events(0) != BPS_SUCCESS) {
    say("no network status events");
  }
  for (;;) {
    bps_event_t *ev = nullptr;
    if (bps_get_event(&ev, -1) != BPS_SUCCESS) {
      say("bps_get_event failed");
      return;
    }
    if (!ev) {
      continue;
    }
    if (bps_event_get_domain(ev) == netstatus_get_domain()) {
      // The event owns its info: no free.
      netstatus_info_t *info = bps_event_get_code(ev) == NETSTATUS_INFO ? netstatus_event_get_info(ev) : nullptr;
      if (info) {
        Event e;
        e.available = netstatus_info_get_availability(info);
        e.wifi = e.available ? default_is_wifi() : true;
        push(std::move(e));
      }
      continue;
    }
    if (bps_event_get_domain(ev) != navigator_get_domain()) {
      continue;
    }
    if (bps_event_get_code(ev) == NAVIGATOR_EXIT) {  // the same as SIGTERM; the navigator allows a few seconds
      ssize_t n = write(wake_fd, "q", 1);
      (void)n;
      return;
    }
    if (bps_event_get_code(ev) == NAVIGATOR_INVOKE_TARGET) {
      const navigator_invoke_invocation_t *inv = navigator_invoke_event_get_invocation(ev);
      const char *action = navigator_invoke_invocation_get_action(inv);
      if (!action) {
        continue;
      }
      // The strings and the data belong to the event, which the next bps_get_event frees: copy them now.
      const char *mime = navigator_invoke_invocation_get_type(inv);
      const char *uri = navigator_invoke_invocation_get_uri(inv);
      const void *data = navigator_invoke_invocation_get_data(inv);
      const int length = navigator_invoke_invocation_get_data_length(inv);
      Event e;
      e.invoke = true;
      e.action = action;
      e.mime = mime ? mime : "";
      e.uri = uri ? uri : "";
      if (data && length > 0) {
        e.data.assign(static_cast<const char *>(data), static_cast<std::size_t>(length));
      }
      push(std::move(e));
    }
  }
}

}  // namespace

void bb10_start(int wake) {
  wake_fd = wake;
  std::thread(run).detach();
}

void bb10_drain() {
  std::vector<Event> events;
  {
    std::lock_guard<std::mutex> lock(mu);
    events.swap(queue);
  }
  for (Event &e : events) {
    if (e.invoke) {
      hub_invoked(e.action, e.mime, e.uri, e.data);
    } else {
      mode_network(e.available, e.wifi);
    }
  }
}
