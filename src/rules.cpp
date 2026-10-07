#include "heliospan/rules.hpp"

#include "format.hpp"

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <unordered_map>

namespace heliospan {
namespace {

int severity_rank(Severity severity) { return severity == Severity::Critical ? 0 : 1; }

Alert make_alert(const Reading& reading, TimePoint now, const std::string& rule_id, const std::string& rule_name,
                 Severity severity, const std::string& metric, double value, double threshold, const std::string& unit,
                 const std::string& message) {
  Alert alert;
  alert.id = rule_id + ":" + reading.subject_id;
  alert.rule_id = rule_id;
  alert.rule_name = rule_name;
  alert.severity = severity;
  alert.subject_id = reading.subject_id;
  alert.subject_name = reading.subject_name;
  alert.subject_kind = reading.kind;
  alert.site_id = reading.site_id;
  alert.site_name = reading.site_name;
  alert.room_id = reading.room_id;
  alert.room_name = reading.room_name;
  alert.message = message;
  alert.metric = metric;
  alert.value = unit == "kW" ? round_n(value, 2) : round_n(value, 1);
  alert.threshold = threshold;
  alert.unit = unit;
  alert.opened_at = now;
  alert.last_seen = now;
  return alert;
}

std::optional<Alert> above(const Reading& reading, TimePoint now, const std::string& metric, double warning,
                           double critical, const std::string& rule_id, const std::string& rule_name,
                           const std::string& unit, const std::string& noun) {
  const double value = reading.metrics.at(metric);
  Severity severity = Severity::Warning;
  double threshold = warning;
  if (value > critical) {
    severity = Severity::Critical;
    threshold = critical;
  } else if (value > warning) {
    severity = Severity::Warning;
    threshold = warning;
  } else {
    return std::nullopt;
  }
  const std::string message = noun + " is " + format_unit(value, unit) + " (" + to_string(severity) + " above " +
                              format_unit(threshold, unit) + ").";
  return make_alert(reading, now, rule_id, rule_name, severity, metric, value, threshold, unit, message);
}

std::optional<Alert> below(const Reading& reading, TimePoint now, const std::string& metric, double warning,
                           double critical, const std::string& rule_id, const std::string& rule_name,
                           const std::string& unit, const std::string& noun) {
  const double value = reading.metrics.at(metric);
  Severity severity = Severity::Warning;
  double threshold = warning;
  if (value < critical) {
    severity = Severity::Critical;
    threshold = critical;
  } else if (value < warning) {
    severity = Severity::Warning;
    threshold = warning;
  } else {
    return std::nullopt;
  }
  const std::string message = noun + " is " + format_unit(value, unit) + " (" + to_string(severity) + " below " +
                              format_unit(threshold, unit) + ").";
  return make_alert(reading, now, rule_id, rule_name, severity, metric, value, threshold, unit, message);
}

std::optional<Alert> voltage(const Reading& reading, TimePoint now, const std::string& metric, double nominal,
                             const std::string& rule_id, const std::string& rule_name, const std::string& noun) {
  const double value = reading.metrics.at(metric);
  const double deviation = std::abs(value - nominal) / nominal;
  Severity severity = Severity::Warning;
  double band = VOLTAGE_WARNING_BAND;
  if (deviation > VOLTAGE_CRITICAL_BAND) {
    severity = Severity::Critical;
    band = VOLTAGE_CRITICAL_BAND;
  } else if (deviation > VOLTAGE_WARNING_BAND) {
    severity = Severity::Warning;
    band = VOLTAGE_WARNING_BAND;
  } else {
    return std::nullopt;
  }
  const double threshold = value >= nominal ? nominal * (1.0 + band) : nominal * (1.0 - band);
  const std::string message = noun + " is " + format_unit(value, "V") + ", outside ±" + format_percent_ratio(band) +
                              " of " + format_unit(nominal, "V") + " (" + to_string(severity) + ").";
  return make_alert(reading, now, rule_id, rule_name, severity, metric, value, round_n(threshold, 1), "V", message);
}

std::vector<Alert> ups_alerts(const Reading& reading, TimePoint now) {
  std::vector<Alert> alerts;
  if (auto band = above(reading, now, "load_pct", UPS_LOAD_WARNING_PCT, UPS_LOAD_CRITICAL_PCT, "ups-load-high",
                        "UPS load high", "%", "Load")) {
    alerts.push_back(*band);
  }
  if (auto band = below(reading, now, "battery_health_pct", BATTERY_HEALTH_WARNING_PCT, BATTERY_HEALTH_CRITICAL_PCT,
                        "ups-battery-low", "Battery health low", "%", "Battery health")) {
    alerts.push_back(*band);
  }
  if (auto band = below(reading, now, "runtime_minutes", RUNTIME_WARNING_MIN, RUNTIME_CRITICAL_MIN, "ups-runtime-low",
                        "Runtime low", "min", "Estimated runtime")) {
    alerts.push_back(*band);
  }
  if (auto band = voltage(reading, now, "input_voltage_v", INPUT_VOLTAGE_NOMINAL_V, "ups-input-voltage",
                          "Input voltage out of band", "Input voltage")) {
    alerts.push_back(*band);
  }
  if (auto band = voltage(reading, now, "output_voltage_v", OUTPUT_VOLTAGE_NOMINAL_V, "ups-output-voltage",
                          "Output voltage out of band", "Output voltage")) {
    alerts.push_back(*band);
  }
  return alerts;
}

std::vector<Alert> pdu_alerts(const Reading& reading, TimePoint now) {
  if (reading.outlets.empty()) return {};
  const auto worst = std::max_element(reading.outlets.begin(), reading.outlets.end(),
                                      [](const Outlet& a, const Outlet& b) { return a.kw < b.kw; });
  Severity severity = Severity::Warning;
  double threshold = OUTLET_WARNING_KW;
  if (worst->kw > OUTLET_CRITICAL_KW) {
    severity = Severity::Critical;
    threshold = OUTLET_CRITICAL_KW;
  } else if (worst->kw > OUTLET_WARNING_KW) {
    severity = Severity::Warning;
    threshold = OUTLET_WARNING_KW;
  } else {
    return {};
  }
  const std::string message = "Outlet " + worst->label + " is drawing " + format_unit(worst->kw, "kW") + " (" +
                              to_string(severity) + " above " + format_unit(threshold, "kW") + ").";
  return {make_alert(reading, now, "pdu-outlet-power", "Outlet power high", severity, "max_outlet_kw", worst->kw,
                     threshold, "kW", message)};
}

std::vector<Alert> cooling_alerts(const Reading& reading, TimePoint now) {
  std::vector<Alert> alerts;
  const double supply = reading.metrics.at("supply_temp_c");
  const double setpoint = reading.metrics.at("setpoint_c");
  const double delta = supply - setpoint;
  if (delta > SUPPLY_DELTA_CRITICAL_C || delta > SUPPLY_DELTA_WARNING_C) {
    const Severity severity = delta > SUPPLY_DELTA_CRITICAL_C ? Severity::Critical : Severity::Warning;
    const double threshold = delta > SUPPLY_DELTA_CRITICAL_C ? SUPPLY_DELTA_CRITICAL_C : SUPPLY_DELTA_WARNING_C;
    const std::string message = "Supply air is " + format_unit(supply, "°C") + ", " + format_unit(delta, "°C") +
                                " above the " + format_unit(setpoint, "°C") + " setpoint (" + to_string(severity) +
                                " above " + format_unit(threshold, "°C") + ").";
    alerts.push_back(make_alert(reading, now, "cooling-supply-high", "Supply air high", severity, "supply_temp_c",
                                round_n(supply, 1), round_n(setpoint + threshold, 1), "°C", message));
  }
  if (auto band = above(reading, now, "fan_speed_pct", FAN_WARNING_PCT, FAN_CRITICAL_PCT, "cooling-fan-high",
                        "Fan speed high", "%", "Fan speed")) {
    alerts.push_back(*band);
  }
  return alerts;
}

std::vector<Alert> rack_alerts(const Reading& reading, TimePoint now) {
  if (auto band = above(reading, now, "inlet_temp_c", RACK_INLET_WARNING_C, RACK_INLET_CRITICAL_C, "rack-inlet-high",
                        "Rack inlet high", "°C", "Rack inlet")) {
    return {*band};
  }
  return {};
}

std::vector<Alert> alerts_for(const Reading& reading, TimePoint now) {
  switch (reading.kind) {
    case SubjectKind::Ups:
      return ups_alerts(reading, now);
    case SubjectKind::Pdu:
      return pdu_alerts(reading, now);
    case SubjectKind::Cooling:
      return cooling_alerts(reading, now);
    case SubjectKind::Rack:
      return rack_alerts(reading, now);
  }
  throw std::invalid_argument(std::string("Unsupported subject kind: ") + to_string(reading.kind));
}

ChartThreshold line(const std::string& metric, double value, Severity severity, const std::string& label) {
  return ChartThreshold{metric, value, severity, label};
}

}  // namespace

std::vector<Alert> evaluate_readings(const std::vector<Reading>& readings, TimePoint now) {
  std::vector<Alert> alerts;
  for (const Reading& reading : readings) {
    auto part = alerts_for(reading, now);
    alerts.insert(alerts.end(), part.begin(), part.end());
  }
  return alerts;
}

std::vector<Alert> sort_alerts(std::vector<Alert> alerts) {
  std::sort(alerts.begin(), alerts.end(), [](const Alert& a, const Alert& b) {
    if (severity_rank(a.severity) != severity_rank(b.severity)) {
      return severity_rank(a.severity) < severity_rank(b.severity);
    }
    if (a.opened_at != b.opened_at) return a.opened_at < b.opened_at;
    if (a.subject_name != b.subject_name) return a.subject_name < b.subject_name;
    return a.rule_id < b.rule_id;
  });
  return alerts;
}

std::vector<Alert> reconcile_alerts(const std::vector<Alert>& previous, const std::vector<Alert>& current) {
  std::unordered_map<std::string, TimePoint> opened;
  opened.reserve(previous.size());
  for (const Alert& alert : previous) opened.emplace(alert.id, alert.opened_at);
  std::vector<Alert> merged;
  merged.reserve(current.size());
  for (Alert alert : current) {
    const auto found = opened.find(alert.id);
    if (found != opened.end()) alert.opened_at = found->second;
    merged.push_back(std::move(alert));
  }
  return sort_alerts(std::move(merged));
}

std::vector<RuleInfo> rule_catalog() {
  return {
      {"ups-load-high", "UPS load high",
       "Output load above " + fixed(UPS_LOAD_WARNING_PCT, 0) + "% of nameplate, critical above " +
           fixed(UPS_LOAD_CRITICAL_PCT, 0) + "%.",
       SubjectKind::Ups, "above", "%", UPS_LOAD_WARNING_PCT, UPS_LOAD_CRITICAL_PCT},
      {"ups-battery-low", "Battery health low",
       "Battery health below " + fixed(BATTERY_HEALTH_WARNING_PCT, 0) + "%, critical below " +
           fixed(BATTERY_HEALTH_CRITICAL_PCT, 0) + "%.",
       SubjectKind::Ups, "below", "%", BATTERY_HEALTH_WARNING_PCT, BATTERY_HEALTH_CRITICAL_PCT},
      {"ups-runtime-low", "Runtime low",
       "Estimated runtime below " + fixed(RUNTIME_WARNING_MIN, 0) + " min, critical below " +
           fixed(RUNTIME_CRITICAL_MIN, 0) + " min.",
       SubjectKind::Ups, "below", "min", RUNTIME_WARNING_MIN, RUNTIME_CRITICAL_MIN},
      {"ups-input-voltage", "Input voltage out of band",
       "Input voltage outside ±" + format_percent_ratio(VOLTAGE_WARNING_BAND) + " of " +
           fixed(INPUT_VOLTAGE_NOMINAL_V, 0) + " V, critical outside ±" + format_percent_ratio(VOLTAGE_CRITICAL_BAND) +
           ".",
       SubjectKind::Ups, "outside_band", "V", VOLTAGE_WARNING_BAND, VOLTAGE_CRITICAL_BAND},
      {"ups-output-voltage", "Output voltage out of band",
       "Output voltage outside ±" + format_percent_ratio(VOLTAGE_WARNING_BAND) + " of " +
           fixed(OUTPUT_VOLTAGE_NOMINAL_V, 0) + " V, critical outside ±" +
           format_percent_ratio(VOLTAGE_CRITICAL_BAND) + ".",
       SubjectKind::Ups, "outside_band", "V", VOLTAGE_WARNING_BAND, VOLTAGE_CRITICAL_BAND},
      {"rack-inlet-high", "Rack inlet high",
       "Rack inlet above the ASHRAE recommended " + fixed(RACK_INLET_WARNING_C, 0) +
           "°C, critical above the A1 allowable " + fixed(RACK_INLET_CRITICAL_C, 0) + "°C.",
       SubjectKind::Rack, "above", "°C", RACK_INLET_WARNING_C, RACK_INLET_CRITICAL_C},
      {"pdu-outlet-power", "Outlet power high",
       "Any outlet above " + fixed(OUTLET_WARNING_KW, 1) + " kW, critical above " + fixed(OUTLET_CRITICAL_KW, 1) +
           " kW.",
       SubjectKind::Pdu, "above", "kW", OUTLET_WARNING_KW, OUTLET_CRITICAL_KW},
      {"cooling-supply-high", "Supply air high",
       "Supply air more than " + fixed(SUPPLY_DELTA_WARNING_C, 1) + "°C above setpoint, critical above " +
           fixed(SUPPLY_DELTA_CRITICAL_C, 1) + "°C.",
       SubjectKind::Cooling, "above", "°C", SUPPLY_DELTA_WARNING_C, SUPPLY_DELTA_CRITICAL_C},
      {"cooling-fan-high", "Fan speed high",
       "Cooling fan speed above " + fixed(FAN_WARNING_PCT, 0) + "%, critical above " + fixed(FAN_CRITICAL_PCT, 0) +
           "%.",
       SubjectKind::Cooling, "above", "%", FAN_WARNING_PCT, FAN_CRITICAL_PCT},
  };
}

std::vector<ChartThreshold> chart_thresholds(const std::string& kind, std::optional<double> setpoint_c) {
  if (kind == "ups") {
    return {
        line("load_pct", UPS_LOAD_WARNING_PCT, Severity::Warning, fixed(UPS_LOAD_WARNING_PCT, 0) + "% warning"),
        line("load_pct", UPS_LOAD_CRITICAL_PCT, Severity::Critical, fixed(UPS_LOAD_CRITICAL_PCT, 0) + "% critical"),
        line("battery_health_pct", BATTERY_HEALTH_WARNING_PCT, Severity::Warning,
             fixed(BATTERY_HEALTH_WARNING_PCT, 0) + "% warning"),
        line("battery_health_pct", BATTERY_HEALTH_CRITICAL_PCT, Severity::Critical,
             fixed(BATTERY_HEALTH_CRITICAL_PCT, 0) + "% critical"),
        line("runtime_minutes", RUNTIME_WARNING_MIN, Severity::Warning, fixed(RUNTIME_WARNING_MIN, 0) + " min warning"),
        line("runtime_minutes", RUNTIME_CRITICAL_MIN, Severity::Critical,
             fixed(RUNTIME_CRITICAL_MIN, 0) + " min critical"),
        line("input_voltage_v", INPUT_VOLTAGE_NOMINAL_V * (1.0 - VOLTAGE_WARNING_BAND), Severity::Warning, "input low"),
        line("input_voltage_v", INPUT_VOLTAGE_NOMINAL_V * (1.0 + VOLTAGE_WARNING_BAND), Severity::Warning,
             "input high"),
        line("output_voltage_v", OUTPUT_VOLTAGE_NOMINAL_V * (1.0 - VOLTAGE_WARNING_BAND), Severity::Warning,
             "output low"),
        line("output_voltage_v", OUTPUT_VOLTAGE_NOMINAL_V * (1.0 + VOLTAGE_WARNING_BAND), Severity::Warning,
             "output high"),
    };
  }
  if (kind == "rack") {
    return {
        line("inlet_temp_c", RACK_INLET_WARNING_C, Severity::Warning, fixed(RACK_INLET_WARNING_C, 0) + "°C recommended"),
        line("inlet_temp_c", RACK_INLET_CRITICAL_C, Severity::Critical,
             fixed(RACK_INLET_CRITICAL_C, 0) + "°C allowable"),
    };
  }
  if (kind == "cooling" && setpoint_c) {
    return {
        line("supply_temp_c", *setpoint_c + SUPPLY_DELTA_WARNING_C, Severity::Warning,
             "+" + fixed(SUPPLY_DELTA_WARNING_C, 1) + "°C"),
        line("supply_temp_c", *setpoint_c + SUPPLY_DELTA_CRITICAL_C, Severity::Critical,
             "+" + fixed(SUPPLY_DELTA_CRITICAL_C, 1) + "°C"),
        line("fan_speed_pct", FAN_WARNING_PCT, Severity::Warning, fixed(FAN_WARNING_PCT, 0) + "% warning"),
        line("fan_speed_pct", FAN_CRITICAL_PCT, Severity::Critical, fixed(FAN_CRITICAL_PCT, 0) + "% critical"),
    };
  }
  if (kind == "pdu") {
    return {
        line("total_kw", OUTLET_WARNING_KW, Severity::Warning, fixed(OUTLET_WARNING_KW, 0) + " kW outlet"),
        line("max_outlet_kw", OUTLET_WARNING_KW, Severity::Warning, fixed(OUTLET_WARNING_KW, 0) + " kW warning"),
        line("max_outlet_kw", OUTLET_CRITICAL_KW, Severity::Critical, fixed(OUTLET_CRITICAL_KW, 0) + " kW critical"),
    };
  }
  if (kind != "ups" && kind != "pdu" && kind != "cooling" && kind != "rack") {
    throw std::invalid_argument("Unsupported subject kind: " + kind);
  }
  return {};
}

}  // namespace heliospan
