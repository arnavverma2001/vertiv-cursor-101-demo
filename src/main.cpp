#include "heliospan/config.hpp"
#include "heliospan/server.hpp"
#include "heliospan/store.hpp"

#include <httplib.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <exception>
#include <mutex>
#include <thread>

int main() {
  using namespace heliospan;
  FleetStore store = FleetStore::seed();
  store.tick();

  std::mutex store_mu;
  std::atomic<bool> run{true};
  std::thread ticker([&] {
    while (run.load()) {
      std::this_thread::sleep_for(std::chrono::duration<double>(kTickSeconds));
      if (!run.load()) break;
      try {
        std::lock_guard<std::mutex> lock(store_mu);
        store.tick();
      } catch (const std::exception& ex) {
        std::fprintf(stderr, "simulator tick failed: %s\n", ex.what());
      }
    }
  });

  httplib::Server server;
  install_routes(server, store, store_mu, true, web_dir_from_env_or_default());
  std::printf("HelioSpan Monitor listening on http://127.0.0.1:8000\n");
  std::fflush(stdout);
  const bool ok = server.listen("127.0.0.1", 8000);
  run = false;
  ticker.join();
  return ok ? 0 : 1;
}
