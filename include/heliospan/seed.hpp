#pragma once

#include "heliospan/domain.hpp"

#include <map>
#include <string>
#include <vector>

namespace heliospan {

// Static topology for the two-site fleet. UPS load is not sampled on its own:
// each tick derives it from the PDUs that module feeds, plus auxiliary_kw.

std::vector<Site> build_sites();
std::vector<Room> build_rooms();
std::vector<Rack> build_racks();
std::vector<Device> build_devices();

std::map<std::string, Metrics> metric_baselines();
std::map<std::string, std::vector<double>> build_outlet_baselines();

}  // namespace heliospan
