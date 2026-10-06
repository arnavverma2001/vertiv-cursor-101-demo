#include "heliospan/power.hpp"

#include <algorithm>

namespace heliospan {

double estimate_runtime_minutes(double battery_health_pct, double load_pct, double full_load_runtime_min) {
  const double safe_load = std::max(load_pct, 1.0);
  const double health = std::max(battery_health_pct, 0.0) / 100.0;
  return full_load_runtime_min * health * (100.0 / safe_load);
}

}  // namespace heliospan
