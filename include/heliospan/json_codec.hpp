#pragma once

#include "heliospan/store.hpp"

#include <nlohmann/json.hpp>
#include <vector>

namespace heliospan {

nlohmann::json to_json(const Alert& alert);
nlohmann::json to_json(const RuleInfo& rule);
nlohmann::json to_json(const FleetSnapshot& snapshot);
nlohmann::json to_json(const Summary& summary);
nlohmann::json to_json(const SiteCard& site);
nlohmann::json to_json(const DeviceDetail& detail);
nlohmann::json to_json(const RackDetail& detail);

}  // namespace heliospan
