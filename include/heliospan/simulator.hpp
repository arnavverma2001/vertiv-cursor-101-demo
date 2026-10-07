#pragma once

#include "heliospan/domain.hpp"

#include <string>

namespace heliospan {

class FleetStore;

// One telemetry step. PDU totals, rack power, UPS load, and runtime are
// derived. UPS-B takes a short input sag on a fixed tick schedule.
void simulate_tick(FleetStore& store, TimePoint now);

}  // namespace heliospan
