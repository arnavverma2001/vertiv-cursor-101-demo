#pragma once

#include "heliospan/store.hpp"

#include <mutex>
#include <string>

namespace httplib {
class Server;
}

namespace heliospan {

// Installs the JSON routes and the console files. The mutex guards `store`
// for every request. `telemetry_running` is reported by /api/health.
void install_routes(httplib::Server& server, FleetStore& store, std::mutex& store_mu,
                    bool telemetry_running, const std::string& web_dir);

std::string web_dir_from_env_or_default();

}  // namespace heliospan
