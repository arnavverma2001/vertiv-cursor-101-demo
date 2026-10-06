#pragma once

namespace heliospan {

// Sample period for the in-memory fleet. The console polls on this cadence.
constexpr double kTickSeconds = 2.0;
constexpr int kHistoryLimit = 60;

}  // namespace heliospan
