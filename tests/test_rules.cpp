#include "heliospan/domain.hpp"
#include "heliospan/rules.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <string>

namespace {

using namespace heliospan;

TimePoint at_seconds(int seconds) {
  return TimePoint{std::chrono::seconds{seconds}};
}

Reading reading(SubjectKind kind = SubjectKind::Ups, std::string subject_id = "ups-test",
               std::string name = "UPS-Test", Metrics metrics = {}, std::vector<Outlet> outlets = {}) {
  Metrics base = {
      {"load_pct", 40},          {"battery_health_pct", 98}, {"runtime_minutes", 25},
      {"input_voltage_v", 480},  {"output_voltage_v", 208},   {"output_kw", 20},
      {"supply_temp_c", 18.2},   {"return_temp_c", 28},       {"fan_speed_pct", 50},
      {"setpoint_c", 18},        {"inlet_temp_c", 23},        {"total_kw", 5},
      {"max_outlet_kw", 1.5},    {"power_kw", 5},
  };
  for (const auto& [key, value] : metrics) base[key] = value;
  Reading row;
  row.subject_id = std::move(subject_id);
  row.subject_name = std::move(name);
  row.kind = kind;
  row.site_id = "ashford";
  row.site_name = "Ashford Campus";
  row.room_id = "hall-a";
  row.room_name = "Hall A";
  row.metrics = std::move(base);
  row.outlets = std::move(outlets);
  return row;
}

bool has_rule(const std::vector<Alert>& alerts, const std::string& rule_id, Severity severity) {
  for (const Alert& alert : alerts) {
    if (alert.rule_id == rule_id && alert.severity == severity) return true;
  }
  return false;
}

}  // namespace

TEST(Rules, LoadWarningAndCriticalAreMutuallyExclusive) {
  const TimePoint now = at_seconds(1'000);
  const auto warning = evaluate_readings({reading(SubjectKind::Ups, "ups-test", "UPS-Test", {{"load_pct", UPS_LOAD_WARNING_PCT + 1}})}, now);
  EXPECT_TRUE(has_rule(warning, "ups-load-high", Severity::Warning));
  EXPECT_FALSE(has_rule(warning, "ups-load-high", Severity::Critical));

  const auto critical = evaluate_readings(
      {reading(SubjectKind::Ups, "ups-test", "UPS-Test", {{"load_pct", UPS_LOAD_CRITICAL_PCT + 1}})}, now);
  int load_alerts = 0;
  Severity severity = Severity::Warning;
  for (const Alert& alert : critical) {
    if (alert.rule_id == "ups-load-high") {
      ++load_alerts;
      severity = alert.severity;
    }
  }
  EXPECT_EQ(load_alerts, 1);
  EXPECT_EQ(severity, Severity::Critical);
}

TEST(Rules, LoadAtExactWarningThresholdIsQuiet) {
  const auto alerts = evaluate_readings(
      {reading(SubjectKind::Ups, "ups-test", "UPS-Test", {{"load_pct", UPS_LOAD_WARNING_PCT}})}, at_seconds(1));
  for (const Alert& alert : alerts) EXPECT_NE(alert.rule_id, "ups-load-high");
}

TEST(Rules, HealthyUpsIsQuiet) {
  EXPECT_TRUE(evaluate_readings({reading()}, at_seconds(1)).empty());
}

TEST(Rules, BatteryHealthBands) {
  const TimePoint now = at_seconds(1);
  const auto warning = evaluate_readings(
      {reading(SubjectKind::Ups, "ups-test", "UPS-Test", {{"battery_health_pct", BATTERY_HEALTH_WARNING_PCT - 1}})}, now);
  EXPECT_TRUE(has_rule(warning, "ups-battery-low", Severity::Warning));

  const auto at_warning = evaluate_readings(
      {reading(SubjectKind::Ups, "ups-test", "UPS-Test", {{"battery_health_pct", BATTERY_HEALTH_WARNING_PCT}})}, now);
  for (const Alert& alert : at_warning) EXPECT_NE(alert.rule_id, "ups-battery-low");

  const auto critical = evaluate_readings(
      {reading(SubjectKind::Ups, "ups-test", "UPS-Test", {{"battery_health_pct", BATTERY_HEALTH_CRITICAL_PCT - 0.1}})},
      now);
  int count = 0;
  Severity severity = Severity::Warning;
  for (const Alert& alert : critical) {
    if (alert.rule_id == "ups-battery-low") {
      ++count;
      severity = alert.severity;
    }
  }
  EXPECT_EQ(count, 1);
  EXPECT_EQ(severity, Severity::Critical);
}

TEST(Rules, RuntimeBelowWarning) {
  const TimePoint now = at_seconds(1);
  const auto alerts = evaluate_readings(
      {reading(SubjectKind::Ups, "ups-test", "UPS-Test", {{"runtime_minutes", RUNTIME_WARNING_MIN - 0.5}})}, now);
  EXPECT_TRUE(has_rule(alerts, "ups-runtime-low", Severity::Warning));
  const auto quiet = evaluate_readings(
      {reading(SubjectKind::Ups, "ups-test", "UPS-Test", {{"runtime_minutes", RUNTIME_WARNING_MIN}})}, now);
  for (const Alert& alert : quiet) EXPECT_NE(alert.rule_id, "ups-runtime-low");
}

TEST(Rules, OutputVoltageOutsideBand) {
  const auto alerts =
      evaluate_readings({reading(SubjectKind::Ups, "ups-test", "UPS-Test", {{"output_voltage_v", 190.0}})}, at_seconds(1));
  int count = 0;
  for (const Alert& alert : alerts) {
    if (alert.rule_id != "ups-output-voltage") continue;
    ++count;
    EXPECT_EQ(alert.severity, Severity::Critical);
    EXPECT_NE(alert.message.find("208"), std::string::npos);
  }
  EXPECT_EQ(count, 1);
}

TEST(Rules, InputVoltageMildSagIsWarning) {
  const auto alerts =
      evaluate_readings({reading(SubjectKind::Ups, "ups-test", "UPS-Test", {{"input_voltage_v", 450.0}})}, at_seconds(1));
  int count = 0;
  for (const Alert& alert : alerts) {
    if (alert.rule_id != "ups-input-voltage") continue;
    ++count;
    EXPECT_EQ(alert.severity, Severity::Warning);
  }
  EXPECT_EQ(count, 1);
}

TEST(Rules, RackInletFollowsAshraeBands) {
  const TimePoint now = at_seconds(1);
  const auto recommended = evaluate_readings(
      {reading(SubjectKind::Rack, "h02", "Rack H02", {{"inlet_temp_c", RACK_INLET_WARNING_C + 0.4}})}, now);
  EXPECT_TRUE(has_rule(recommended, "rack-inlet-high", Severity::Warning));

  const auto allowable = evaluate_readings(
      {reading(SubjectKind::Rack, "h01", "Rack H01", {{"inlet_temp_c", RACK_INLET_CRITICAL_C + 0.4}})}, now);
  int count = 0;
  Severity severity = Severity::Warning;
  for (const Alert& alert : allowable) {
    if (alert.rule_id == "rack-inlet-high") {
      ++count;
      severity = alert.severity;
    }
  }
  EXPECT_EQ(count, 1);
  EXPECT_EQ(severity, Severity::Critical);

  const auto at_recommended =
      evaluate_readings({reading(SubjectKind::Rack, "a01", "Rack A01", {{"inlet_temp_c", RACK_INLET_WARNING_C}})}, now);
  for (const Alert& alert : at_recommended) EXPECT_NE(alert.rule_id, "rack-inlet-high");
}

TEST(Rules, PduReportsTheWorstOutletOnly) {
  const auto alerts = evaluate_readings(
      {reading(SubjectKind::Pdu, "pdu-h02", "PDU-H02", {},
               {{"1", "1", 1.2}, {"4", "4", OUTLET_WARNING_KW + 0.4}, {"5", "5", 1.0}})},
      at_seconds(1));
  ASSERT_EQ(alerts.size(), 1u);
  EXPECT_EQ(alerts[0].rule_id, "pdu-outlet-power");
  EXPECT_EQ(alerts[0].severity, Severity::Warning);
  EXPECT_NE(alerts[0].message.find("Outlet 4"), std::string::npos);
}

TEST(Rules, PduWithNoOutletsDoesNotAlert) {
  EXPECT_TRUE(evaluate_readings({reading(SubjectKind::Pdu, "pdu-empty", "PDU")}, at_seconds(1)).empty());
}

TEST(Rules, CoolingSupplyAndFan) {
  const auto alerts = evaluate_readings(
      {reading(SubjectKind::Cooling, "mer-crac-2", "CRAC-2",
               {{"supply_temp_c", 18.0 + SUPPLY_DELTA_WARNING_C + 0.4},
                {"setpoint_c", 18.0},
                {"fan_speed_pct", FAN_WARNING_PCT + 1}})},
      at_seconds(1));
  EXPECT_TRUE(has_rule(alerts, "cooling-supply-high", Severity::Warning));
  EXPECT_TRUE(has_rule(alerts, "cooling-fan-high", Severity::Warning));
}

TEST(Rules, ReconcileKeepsOpenedAtAndDropsClearedAlerts) {
  const TimePoint now = at_seconds(1'700'000'000);
  const auto hot = evaluate_readings(
      {reading(SubjectKind::Ups, "ups-test", "UPS-Test", {{"load_pct", UPS_LOAD_WARNING_PCT + 2}})}, now);
  const auto opened = reconcile_alerts({}, hot);
  ASSERT_EQ(opened.size(), 1u);
  EXPECT_EQ(opened[0].opened_at, now);

  const TimePoint later = now + std::chrono::seconds(4);
  const auto still = evaluate_readings(
      {reading(SubjectKind::Ups, "ups-test", "UPS-Test", {{"load_pct", UPS_LOAD_WARNING_PCT + 3}})}, later);
  const auto kept = reconcile_alerts(opened, still);
  ASSERT_EQ(kept.size(), 1u);
  EXPECT_EQ(kept[0].opened_at, now);
  EXPECT_EQ(kept[0].last_seen, later);

  const auto calm = evaluate_readings({reading(SubjectKind::Ups, "ups-test", "UPS-Test", {{"load_pct", 40}})}, later);
  EXPECT_TRUE(reconcile_alerts(kept, calm).empty());
}

TEST(Rules, SortPutsCriticalBeforeWarning) {
  const TimePoint now = at_seconds(10);
  const auto battery =
      evaluate_readings({reading(SubjectKind::Ups, "ups-z", "UPS-Z", {{"battery_health_pct", 40}})}, now);
  const auto load = evaluate_readings(
      {reading(SubjectKind::Ups, "ups-a", "UPS-A", {{"load_pct", UPS_LOAD_WARNING_PCT + 1}})}, now);
  std::vector<Alert> combined = load;
  combined.insert(combined.end(), battery.begin(), battery.end());
  const auto ordered = sort_alerts(combined);
  ASSERT_EQ(ordered.size(), 2u);
  EXPECT_EQ(ordered[0].severity, Severity::Critical);
  EXPECT_EQ(ordered[1].severity, Severity::Warning);
}
