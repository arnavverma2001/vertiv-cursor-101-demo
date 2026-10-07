#include "heliospan/json_codec.hpp"

#include "format.hpp"

#include <nlohmann/json.hpp>

namespace heliospan {
namespace {

nlohmann::json opt_string(const std::optional<std::string>& value) {
  return value ? nlohmann::json(*value) : nlohmann::json(nullptr);
}

nlohmann::json opt_number(const std::optional<double>& value) {
  return value ? nlohmann::json(*value) : nlohmann::json(nullptr);
}

nlohmann::json outlet_json(const Outlet& outlet) {
  return {{"id", outlet.id}, {"label", outlet.label}, {"kw", outlet.kw}};
}

nlohmann::json metrics_json(const Metrics& metrics) {
  nlohmann::json out = nlohmann::json::object();
  for (const auto& [key, value] : metrics) out[key] = value;
  return out;
}

nlohmann::json history_json(const std::vector<HistoryPoint>& history) {
  nlohmann::json rows = nlohmann::json::array();
  for (const HistoryPoint& point : history) {
    nlohmann::json row = metrics_json(point.metrics);
    row["ts"] = format_time(point.ts);
    rows.push_back(std::move(row));
  }
  return rows;
}

nlohmann::json series_json(const std::vector<SeriesSpec>& series) {
  nlohmann::json rows = nlohmann::json::array();
  for (const SeriesSpec& spec : series) {
    rows.push_back({{"key", spec.key}, {"label", spec.label}, {"unit", spec.unit}});
  }
  return rows;
}

nlohmann::json thresholds_json(const std::vector<ChartThreshold>& thresholds) {
  nlohmann::json rows = nlohmann::json::array();
  for (const ChartThreshold& line : thresholds) {
    rows.push_back({{"metric", line.metric},
                    {"value", line.value},
                    {"severity", to_string(line.severity)},
                    {"label", line.label}});
  }
  return rows;
}

nlohmann::json ups_json(const UpsCard& card) {
  return {{"id", card.id},
          {"name", card.name},
          {"rated_kw", card.rated_kw},
          {"load_pct", card.load_pct},
          {"output_kw", card.output_kw},
          {"battery_health_pct", card.battery_health_pct},
          {"runtime_minutes", card.runtime_minutes},
          {"input_voltage_v", card.input_voltage_v},
          {"output_voltage_v", card.output_voltage_v},
          {"status", card.status}};
}

nlohmann::json cooling_json(const CoolingCard& card) {
  return {{"id", card.id},
          {"name", card.name},
          {"subtype", to_string(card.subtype)},
          {"supply_temp_c", card.supply_temp_c},
          {"return_temp_c", card.return_temp_c},
          {"fan_speed_pct", card.fan_speed_pct},
          {"setpoint_c", card.setpoint_c},
          {"status", card.status}};
}

nlohmann::json pdu_json(const PduCard& card) {
  nlohmann::json outlets = nlohmann::json::array();
  for (const Outlet& outlet : card.outlets) outlets.push_back(outlet_json(outlet));
  return {{"id", card.id},
          {"name", card.name},
          {"rack_id", card.rack_id},
          {"fed_by", card.fed_by},
          {"total_kw", card.total_kw},
          {"max_outlet_kw", card.max_outlet_kw},
          {"outlets", std::move(outlets)},
          {"status", card.status}};
}

nlohmann::json rack_json(const RackCard& card) {
  return {{"id", card.id},
          {"name", card.name},
          {"inlet_temp_c", card.inlet_temp_c},
          {"power_kw", card.power_kw},
          {"pdu_id", card.pdu_id},
          {"max_outlet_kw", card.max_outlet_kw},
          {"status", card.status}};
}

nlohmann::json room_json(const RoomCard& room) {
  nlohmann::json ups = nlohmann::json::array();
  nlohmann::json cooling = nlohmann::json::array();
  nlohmann::json pdus = nlohmann::json::array();
  nlohmann::json racks = nlohmann::json::array();
  for (const UpsCard& card : room.ups) ups.push_back(ups_json(card));
  for (const CoolingCard& card : room.cooling) cooling.push_back(cooling_json(card));
  for (const PduCard& card : room.pdus) pdus.push_back(pdu_json(card));
  for (const RackCard& card : room.racks) racks.push_back(rack_json(card));
  return {{"id", room.id}, {"name", room.name}, {"role", room.role},
          {"ups", std::move(ups)}, {"cooling", std::move(cooling)},
          {"pdus", std::move(pdus)}, {"racks", std::move(racks)}};
}

}  // namespace

nlohmann::json to_json(const Alert& alert) {
  return {{"id", alert.id},
          {"rule_id", alert.rule_id},
          {"rule_name", alert.rule_name},
          {"severity", to_string(alert.severity)},
          {"subject_id", alert.subject_id},
          {"subject_name", alert.subject_name},
          {"subject_kind", to_string(alert.subject_kind)},
          {"site_id", alert.site_id},
          {"site_name", alert.site_name},
          {"room_id", alert.room_id},
          {"room_name", alert.room_name},
          {"message", alert.message},
          {"metric", alert.metric},
          {"value", alert.value},
          {"threshold", alert.threshold},
          {"unit", alert.unit},
          {"opened_at", format_time(alert.opened_at)},
          {"last_seen", format_time(alert.last_seen)}};
}

nlohmann::json to_json(const RuleInfo& rule) {
  return {{"id", rule.id},
          {"name", rule.name},
          {"description", rule.description},
          {"subject_kind", to_string(rule.subject_kind)},
          {"comparator", rule.comparator},
          {"unit", rule.unit},
          {"warning_threshold", opt_number(rule.warning_threshold)},
          {"critical_threshold", opt_number(rule.critical_threshold)}};
}

nlohmann::json to_json(const Summary& summary) {
  return {{"site_count", summary.site_count},
          {"room_count", summary.room_count},
          {"rack_count", summary.rack_count},
          {"device_count", summary.device_count},
          {"it_load_kw", summary.it_load_kw},
          {"ups_output_kw", summary.ups_output_kw},
          {"active_alerts", summary.active_alerts},
          {"critical_alerts", summary.critical_alerts},
          {"warning_alerts", summary.warning_alerts},
          {"hottest_rack_id", opt_string(summary.hottest_rack_id)},
          {"hottest_rack_name", opt_string(summary.hottest_rack_name)},
          {"hottest_site_name", opt_string(summary.hottest_site_name)},
          {"hottest_inlet_c", opt_number(summary.hottest_inlet_c)},
          {"shortest_runtime_device_id", opt_string(summary.shortest_runtime_device_id)},
          {"shortest_runtime_name", opt_string(summary.shortest_runtime_name)},
          {"shortest_runtime_min", opt_number(summary.shortest_runtime_min)}};
}

nlohmann::json to_json(const SiteCard& site) {
  nlohmann::json rooms = nlohmann::json::array();
  for (const RoomCard& room : site.rooms) rooms.push_back(room_json(room));
  return {{"id", site.id},
          {"name", site.name},
          {"location", site.location},
          {"climate_note", site.climate_note},
          {"active_alerts", site.active_alerts},
          {"critical_alerts", site.critical_alerts},
          {"rooms", std::move(rooms)}};
}

nlohmann::json to_json(const FleetSnapshot& snapshot) {
  nlohmann::json sites = nlohmann::json::array();
  nlohmann::json alerts = nlohmann::json::array();
  for (const SiteCard& site : snapshot.sites) sites.push_back(to_json(site));
  for (const Alert& alert : snapshot.alerts) alerts.push_back(to_json(alert));
  return {{"generated_at", format_time(snapshot.generated_at)},
          {"tick", snapshot.tick},
          {"tick_seconds", snapshot.tick_seconds},
          {"summary", to_json(snapshot.summary)},
          {"sites", std::move(sites)},
          {"alerts", std::move(alerts)}};
}

nlohmann::json to_json(const DeviceDetail& detail) {
  nlohmann::json outlets = nlohmann::json::array();
  nlohmann::json alerts = nlohmann::json::array();
  for (const Outlet& outlet : detail.outlets) outlets.push_back(outlet_json(outlet));
  for (const Alert& alert : detail.alerts) alerts.push_back(to_json(alert));
  return {{"id", detail.id},
          {"name", detail.name},
          {"kind", to_string(detail.kind)},
          {"subtype", detail.subtype ? nlohmann::json(to_string(*detail.subtype)) : nlohmann::json(nullptr)},
          {"site_id", detail.site_id},
          {"site_name", detail.site_name},
          {"room_id", detail.room_id},
          {"room_name", detail.room_name},
          {"rack_id", opt_string(detail.rack_id)},
          {"rack_name", opt_string(detail.rack_name)},
          {"rated_kw", opt_number(detail.rated_kw)},
          {"setpoint_c", opt_number(detail.setpoint_c)},
          {"fed_by", opt_string(detail.fed_by)},
          {"fed_by_name", opt_string(detail.fed_by_name)},
          {"metrics", metrics_json(detail.metrics)},
          {"outlets", std::move(outlets)},
          {"series", series_json(detail.series)},
          {"thresholds", thresholds_json(detail.thresholds)},
          {"history", history_json(detail.history)},
          {"alerts", std::move(alerts)}};
}

nlohmann::json to_json(const RackDetail& detail) {
  nlohmann::json cooling = nlohmann::json::array();
  nlohmann::json alerts = nlohmann::json::array();
  for (const NamedLink& link : detail.cooling) cooling.push_back({{"id", link.id}, {"name", link.name}});
  for (const Alert& alert : detail.alerts) alerts.push_back(to_json(alert));
  return {{"id", detail.id},
          {"name", detail.name},
          {"site_id", detail.site_id},
          {"site_name", detail.site_name},
          {"room_id", detail.room_id},
          {"room_name", detail.room_name},
          {"inlet_temp_c", detail.inlet_temp_c},
          {"power_kw", detail.power_kw},
          {"pdu", pdu_json(detail.pdu)},
          {"ups_id", detail.ups_id},
          {"ups_name", detail.ups_name},
          {"cooling", std::move(cooling)},
          {"series", series_json(detail.series)},
          {"thresholds", thresholds_json(detail.thresholds)},
          {"history", history_json(detail.history)},
          {"alerts", std::move(alerts)}};
}

}  // namespace heliospan
