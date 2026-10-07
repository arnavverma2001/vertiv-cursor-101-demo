#pragma once

#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace heliospan {

// Device kinds the console knows today. A CDU is not a cooling subtype:
// CRAC and CRAH share air metrics, and a new kind is dispatched explicitly
// in the seed, the simulator, the snapshot, and the rule engine.
enum class DeviceKind { Ups, Pdu, Cooling };
enum class CoolingSubtype { Crac, Crah };
enum class SubjectKind { Ups, Pdu, Cooling, Rack };
enum class Severity { Critical, Warning };

using TimePoint = std::chrono::system_clock::time_point;
using Metrics = std::map<std::string, double>;

struct Outlet {
  std::string id;
  std::string label;
  double kw = 0;
};

struct Site {
  std::string id;
  std::string name;
  std::string location;
  std::string climate_note;
};

struct Room {
  std::string id;
  std::string site_id;
  std::string name;
  std::string role;
};

struct Rack {
  std::string id;
  std::string site_id;
  std::string room_id;
  std::string name;
};

struct Device {
  std::string id;
  std::string name;
  DeviceKind kind = DeviceKind::Ups;
  std::string site_id;
  std::string room_id;
  std::optional<std::string> rack_id;
  std::optional<double> rated_kw;
  double auxiliary_kw = 0;
  std::optional<double> full_load_runtime_min;
  std::optional<CoolingSubtype> cooling_subtype;
  std::optional<double> setpoint_c;
  std::optional<std::string> fed_by;
};

struct HistoryPoint {
  TimePoint ts;
  Metrics metrics;
};

// One subject's latest metrics, detached from the fleet graph.
// Rules evaluate readings and nothing else.
struct Reading {
  std::string subject_id;
  std::string subject_name;
  SubjectKind kind = SubjectKind::Ups;
  std::string site_id;
  std::string site_name;
  std::string room_id;
  std::string room_name;
  std::optional<std::string> rack_id;
  Metrics metrics;
  std::vector<Outlet> outlets;
};

struct Alert {
  std::string id;
  std::string rule_id;
  std::string rule_name;
  Severity severity = Severity::Warning;
  std::string subject_id;
  std::string subject_name;
  SubjectKind subject_kind = SubjectKind::Ups;
  std::string site_id;
  std::string site_name;
  std::string room_id;
  std::string room_name;
  std::string message;
  std::string metric;
  double value = 0;
  double threshold = 0;
  std::string unit;
  TimePoint opened_at;
  TimePoint last_seen;
};

struct ChartThreshold {
  std::string metric;
  double value = 0;
  Severity severity = Severity::Warning;
  std::string label;
};

struct RuleInfo {
  std::string id;
  std::string name;
  std::string description;
  SubjectKind subject_kind = SubjectKind::Ups;
  std::string comparator;
  std::string unit;
  std::optional<double> warning_threshold;
  std::optional<double> critical_threshold;
};

struct SeriesSpec {
  std::string key;
  std::string label;
  std::string unit;
};

struct NamedLink {
  std::string id;
  std::string name;
};

const char* to_string(DeviceKind kind);
const char* to_string(CoolingSubtype subtype);
const char* to_string(SubjectKind kind);
const char* to_string(Severity severity);

DeviceKind device_kind_from(const std::string& text);
SubjectKind subject_kind_from(const std::string& text);

}  // namespace heliospan
