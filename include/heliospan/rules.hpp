#pragma once

#include "heliospan/domain.hpp"

#include <optional>
#include <vector>

namespace heliospan {

// Thresholds are the source of truth for the rule engine and the rule catalog.
// The console copy for UPS load is maintained separately in web/app.js.
constexpr double UPS_LOAD_WARNING_PCT = 85.0;
constexpr double UPS_LOAD_CRITICAL_PCT = 95.0;

constexpr double BATTERY_HEALTH_WARNING_PCT = 70.0;
constexpr double BATTERY_HEALTH_CRITICAL_PCT = 50.0;

constexpr double RUNTIME_WARNING_MIN = 10.0;
constexpr double RUNTIME_CRITICAL_MIN = 5.0;

constexpr double INPUT_VOLTAGE_NOMINAL_V = 480.0;
constexpr double OUTPUT_VOLTAGE_NOMINAL_V = 208.0;
constexpr double VOLTAGE_WARNING_BAND = 0.05;
constexpr double VOLTAGE_CRITICAL_BAND = 0.08;

// ASHRAE TC 9.9 class A1: 18–27°C recommended, up to 32°C allowable.
constexpr double RACK_INLET_WARNING_C = 27.0;
constexpr double RACK_INLET_CRITICAL_C = 32.0;

constexpr double OUTLET_WARNING_KW = 6.0;
constexpr double OUTLET_CRITICAL_KW = 8.0;

constexpr double SUPPLY_DELTA_WARNING_C = 2.5;
constexpr double SUPPLY_DELTA_CRITICAL_C = 5.0;

constexpr double FAN_WARNING_PCT = 90.0;
constexpr double FAN_CRITICAL_PCT = 97.0;

// Pure evaluation. Does not read the clock, the network, or the store.
// Each metric emits at most one alert: critical suppresses warning.
// An unknown subject kind throws.
std::vector<Alert> evaluate_readings(const std::vector<Reading>& readings, TimePoint now);

// Keep opened_at while the same condition stays true. Drop alerts that cleared.
std::vector<Alert> reconcile_alerts(const std::vector<Alert>& previous, const std::vector<Alert>& current);

// Critical first, then open time, then name.
std::vector<Alert> sort_alerts(std::vector<Alert> alerts);

std::vector<RuleInfo> rule_catalog();

std::vector<ChartThreshold> chart_thresholds(const std::string& kind, std::optional<double> setpoint_c);

}  // namespace heliospan
