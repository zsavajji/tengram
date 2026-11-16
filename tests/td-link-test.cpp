// Link test only: proves the static TDLib libs have no unresolved symbols on BB10.
// It is never run here (no phone). create_client_id()/send() pull in Td, the actors,
// the network stack and OpenSSL, so a missing symbol anywhere shows up at link time.
#include <iostream>

#include "td/telegram/Client.h"
#include "td/telegram/td_api.h"
#include "td/telegram/td_api.hpp"

int main() {
  using namespace td;

  // Synchronous request: needs no network and no Td instance.
  auto result = ClientManager::execute(td_api::make_object<td_api::getOption>("version"));
  if (result->get_id() == td_api::optionValueString::ID) {
    std::cout << "TDLib " << static_cast<td_api::optionValueString &>(*result).value_ << std::endl;
  }

  ClientManager manager;
  auto client_id = manager.create_client_id();
  manager.send(client_id, 1, td_api::make_object<td_api::getOption>("version"));
  auto response = manager.receive(1.0);
  if (response.object != nullptr) {
    std::cout << td_api::to_string(response.object) << std::endl;
  }
  return 0;
}
