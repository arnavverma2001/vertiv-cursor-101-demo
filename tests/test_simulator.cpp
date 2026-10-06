#include "heliospan/config.hpp"
#include "heliospan/power.hpp"
#include "heliospan/store.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>

namespace {

heliospan::TimePoint fixed_now() {
  return heliospan::TimePoint{std::chrono::seconds{1'759'000'000}};
}

}  // namespace

TEST(Power, RuntimeScalesInverselyWithLoad) {
  EXPECT_DOUBLE_EQ(heliospan::estimate_runtime_minutes(100, 100, 10), 10);
  EXPECT_DOUBLE_EQ(heliospan::estimate_runtime_minutes(100, 50, 10), 20);
  EXPECT_DOUBLE_EQ(heliospan::estimate_runtime_minutes(50, 50, 10), 10);
}

TEST(Simulator, ManyTicksStayInTheSeededBands) {
  auto store = heliospan::FleetStore::seed();
  const auto now = fixed_now();
  std::vector<double> input_voltages;
  for (int i = 0; i < 70; ++i) {
    store.tick(now);
    const double ash_load = store.latest.at("ash-ups-a").at("load_pct");
    const double mer_load = store.latest.at("mer-ups-2").at("load_pct");
    const double h01 = store.latest.at("h01").at("inlet_temp_c");
    const double h02 = store.latest.at("h02").at("inlet_temp_c");
    const double battery = store.latest.at("mer-ups-1").at("battery_health_pct");
    EXPECT_GT(ash_load, 80.5);
    EXPECT_LT(ash_load, 84.5);
    EXPECT_GT(mer_load, 86.0);
    EXPECT_LT(mer_load, 94.0);
    EXPECT_GT(h01, 32.0);
    EXPECT_GT(h02, 27.5);
    EXPECT_LT(h02, 31.5);
    EXPECT_LT(battery, 50.0);
    input_voltages.push_back(store.latest.at("ash-ups-b").at("input_voltage_v"));
    EXPECT_GT(store.latest.at("pdu-h02").at("max_outlet_kw"), 6.0);
  }
  EXPECT_LT(*std::min_element(input_voltages.begin(), input_voltages.end()), 450);
  EXPECT_GT(*std::max_element(input_voltages.begin(), input_voltages.end()), 470);
  EXPECT_EQ(static_cast<int>(store.history.at("ash-ups-a").size()), heliospan::kHistoryLimit);
}
