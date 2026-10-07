#pragma once

namespace heliospan {

// Estimated minutes of support at the current load and battery health.
// At constant health, runtime scales inversely with load: half load runs
// about twice as long as full nameplate load. This is not a discharge model.
double estimate_runtime_minutes(double battery_health_pct, double load_pct, double full_load_runtime_min);

}  // namespace heliospan
