#include "heliospan/store.hpp"

#include "format.hpp"
#include "heliospan/config.hpp"
#include "heliospan/rules.hpp"
#include "heliospan/seed.hpp"
#include "heliospan/simulator.hpp"

#include <algorithm>
#include <stdexcept>

namespace heliospan {
namespace {

int status_rank(const std::string& status) {
  if (status == "critical") return 2;
  if (status == "warning") return 1;
  return 0;
}

}  // namespace

FleetStore::FleetStore(std::vector<Site> sites_in, std::vector<Room> rooms_in, std::vector<Rack> racks_in,
                       std::vector<Device> devices_in, std::map<std::string, Metrics> baselines_in,
                       std::map<std::string, std::vector<double>> outlet_baselines_in)
    : sites(std::move(sites_in)),
      rooms(std::move(rooms_in)),
      racks(std::move(racks_in)),
      devices(std::move(devices_in)),
      baselines(std::move(baselines_in)),
      outlet_baselines(std::move(outlet_baselines_in)) {
  for (const Site& site : sites) sites_by_id.emplace(site.id, site);
  for (const Room& room : rooms) rooms_by_id.emplace(room.id, room);
  for (const Rack& rack : racks) racks_by_id.emplace(rack.id, rack);
  for (const Device& device : devices) devices_by_id.emplace(device.id, device);
}

FleetStore FleetStore::seed() {
  return FleetStore(build_sites(), build_rooms(), build_racks(), build_devices(), metric_baselines(),
                    build_outlet_baselines());
}

void FleetStore::tick(std::optional<TimePoint> now) {
  ++tick_index;
  const TimePoint moment = now.value_or(std::chrono::system_clock::now());
  simulate_tick(*this, moment);
  alerts = reconcile_alerts(alerts, evaluate_readings(readings(), moment));
}

void FleetStore::record(const std::string& subject_id, TimePoint now, Metrics metrics) {
  auto& ring = history[subject_id];
  ring.push_back(HistoryPoint{now, metrics});
  while (static_cast<int>(ring.size()) > kHistoryLimit) ring.pop_front();
  latest[subject_id] = std::move(metrics);
}

std::vector<const Device*> FleetStore::pdus_for_ups(const std::string& ups_id) const {
  std::vector<const Device*> found;
  for (const Device& device : devices) {
    if (device.kind == DeviceKind::Pdu && device.fed_by && *device.fed_by == ups_id) found.push_back(&device);
  }
  return found;
}

const Device& FleetStore::pdu_for_rack(const std::string& rack_id) const {
  const Device* found = nullptr;
  int count = 0;
  for (const Device& device : devices) {
    if (device.kind == DeviceKind::Pdu && device.rack_id && *device.rack_id == rack_id) {
      found = &device;
      ++count;
    }
  }
  if (count != 1 || found == nullptr) {
    throw std::runtime_error("Expected one PDU on rack " + rack_id);
  }
  return *found;
}

std::vector<Reading> FleetStore::readings() const {
  std::vector<Reading> rows;
  for (const Device& device : devices) {
    const auto metrics = latest.find(device.id);
    if (metrics == latest.end()) continue;
    const Site& site = sites_by_id.at(device.site_id);
    const Room& room = rooms_by_id.at(device.room_id);
    Reading reading;
    reading.subject_id = device.id;
    reading.subject_name = device.name;
    reading.kind = device.kind == DeviceKind::Pdu       ? SubjectKind::Pdu
                   : device.kind == DeviceKind::Cooling ? SubjectKind::Cooling
                                                        : SubjectKind::Ups;
    reading.site_id = site.id;
    reading.site_name = site.name;
    reading.room_id = room.id;
    reading.room_name = room.name;
    reading.rack_id = device.rack_id;
    reading.metrics = metrics->second;
    const auto outlets = latest_outlets.find(device.id);
    if (outlets != latest_outlets.end()) reading.outlets = outlets->second;
    rows.push_back(std::move(reading));
  }
  for (const Rack& rack : racks) {
    const auto metrics = latest.find(rack.id);
    if (metrics == latest.end()) continue;
    const Site& site = sites_by_id.at(rack.site_id);
    const Room& room = rooms_by_id.at(rack.room_id);
    Reading reading;
    reading.subject_id = rack.id;
    reading.subject_name = "Rack " + rack.name;
    reading.kind = SubjectKind::Rack;
    reading.site_id = site.id;
    reading.site_name = site.name;
    reading.room_id = room.id;
    reading.room_name = room.name;
    reading.rack_id = rack.id;
    reading.metrics = metrics->second;
    rows.push_back(std::move(reading));
  }
  return rows;
}

FleetSnapshot FleetStore::snapshot(std::optional<TimePoint> now) const {
  FleetSnapshot snap;
  snap.generated_at = now.value_or(std::chrono::system_clock::now());
  snap.tick = tick_index;
  snap.tick_seconds = kTickSeconds;
  snap.summary = summary();
  snap.alerts = alerts;
  for (const Site& site : sites) snap.sites.push_back(site_card_of(site));
  return snap;
}

std::optional<DeviceDetail> FleetStore::device_detail(const std::string& device_id) const {
  const auto device_it = devices_by_id.find(device_id);
  if (device_it == devices_by_id.end() || latest.find(device_id) == latest.end()) return std::nullopt;
  const Device& device = device_it->second;
  const Site& site = sites_by_id.at(device.site_id);
  const Room& room = rooms_by_id.at(device.room_id);
  DeviceDetail detail;
  detail.id = device.id;
  detail.name = device.name;
  detail.kind = device.kind;
  detail.subtype = device.cooling_subtype;
  detail.site_id = site.id;
  detail.site_name = site.name;
  detail.room_id = room.id;
  detail.room_name = room.name;
  if (device.rack_id) {
    const Rack& rack = racks_by_id.at(*device.rack_id);
    detail.rack_id = rack.id;
    detail.rack_name = rack.name;
  }
  detail.rated_kw = device.rated_kw;
  detail.setpoint_c = device.setpoint_c;
  detail.fed_by = device.fed_by;
  if (device.fed_by) {
    const auto upstream = devices_by_id.find(*device.fed_by);
    detail.fed_by_name = upstream == devices_by_id.end() ? *device.fed_by : upstream->second.name;
  }
  detail.metrics = latest.at(device.id);
  const auto outlets = latest_outlets.find(device.id);
  if (outlets != latest_outlets.end()) detail.outlets = outlets->second;
  detail.series = series_for(to_string(device.kind));
  detail.thresholds = chart_thresholds(to_string(device.kind), device.setpoint_c);
  const auto ring = history.find(device.id);
  if (ring != history.end()) detail.history.assign(ring->second.begin(), ring->second.end());
  for (const Alert& alert : alerts) {
    if (alert.subject_id == device.id) detail.alerts.push_back(alert);
  }
  return detail;
}

std::optional<RackDetail> FleetStore::rack_detail(const std::string& rack_id) const {
  const auto rack_it = racks_by_id.find(rack_id);
  if (rack_it == racks_by_id.end() || latest.find(rack_id) == latest.end()) return std::nullopt;
  const Rack& rack = rack_it->second;
  const Site& site = sites_by_id.at(rack.site_id);
  const Room& room = rooms_by_id.at(rack.room_id);
  const PduCard pdu = pdu_card(pdu_for_rack(rack.id));
  const Device& ups = devices_by_id.at(pdu.fed_by);
  RackDetail detail;
  detail.id = rack.id;
  detail.name = rack.name;
  detail.site_id = site.id;
  detail.site_name = site.name;
  detail.room_id = room.id;
  detail.room_name = room.name;
  detail.inlet_temp_c = latest.at(rack.id).at("inlet_temp_c");
  detail.power_kw = latest.at(rack.id).at("power_kw");
  detail.pdu = pdu;
  detail.ups_id = ups.id;
  detail.ups_name = ups.name;
  for (const Device& device : devices) {
    if (device.kind == DeviceKind::Cooling && device.room_id == rack.room_id) {
      detail.cooling.push_back(NamedLink{device.id, device.name});
    }
  }
  detail.series = series_for("rack");
  detail.thresholds = chart_thresholds("rack", std::nullopt);
  const auto ring = history.find(rack.id);
  if (ring != history.end()) detail.history.assign(ring->second.begin(), ring->second.end());
  for (const Alert& alert : alerts) {
    if (alert.subject_id == rack.id || alert.subject_id == pdu.id) detail.alerts.push_back(alert);
  }
  return detail;
}

std::optional<SiteCard> FleetStore::site_card(const std::string& site_id) const {
  const auto found = sites_by_id.find(site_id);
  if (found == sites_by_id.end()) return std::nullopt;
  return site_card_of(found->second);
}

std::string FleetStore::status_for(std::initializer_list<std::string> subject_ids) const {
  std::string current = "ok";
  for (const Alert& alert : alerts) {
    const bool mine = std::find(subject_ids.begin(), subject_ids.end(), alert.subject_id) != subject_ids.end();
    if (mine && status_rank(to_string(alert.severity)) > status_rank(current)) current = to_string(alert.severity);
  }
  return current;
}

Summary FleetStore::summary() const {
  double it_load = 0;
  for (const Rack& rack : racks) {
    const auto metrics = latest.find(rack.id);
    if (metrics != latest.end()) it_load += metrics->second.at("power_kw");
  }
  double ups_output = 0;
  const Device* shortest = nullptr;
  for (const Device& device : devices) {
    if (device.kind != DeviceKind::Ups) continue;
    ups_output += latest.at(device.id).at("output_kw");
    if (shortest == nullptr ||
        latest.at(device.id).at("runtime_minutes") < latest.at(shortest->id).at("runtime_minutes")) {
      shortest = &device;
    }
  }
  const Rack* hottest = nullptr;
  for (const Rack& rack : racks) {
    if (hottest == nullptr || latest.at(rack.id).at("inlet_temp_c") > latest.at(hottest->id).at("inlet_temp_c")) {
      hottest = &rack;
    }
  }
  int critical = 0;
  int warning = 0;
  for (const Alert& alert : alerts) {
    if (alert.severity == Severity::Critical) ++critical;
    if (alert.severity == Severity::Warning) ++warning;
  }
  Summary out;
  out.site_count = static_cast<int>(sites.size());
  out.room_count = static_cast<int>(rooms.size());
  out.rack_count = static_cast<int>(racks.size());
  out.device_count = static_cast<int>(devices.size());
  out.it_load_kw = round_n(it_load, 2);
  out.ups_output_kw = round_n(ups_output, 2);
  out.active_alerts = static_cast<int>(alerts.size());
  out.critical_alerts = critical;
  out.warning_alerts = warning;
  if (hottest != nullptr) {
    out.hottest_rack_id = hottest->id;
    out.hottest_rack_name = hottest->name;
    out.hottest_site_name = sites_by_id.at(hottest->site_id).name;
    out.hottest_inlet_c = latest.at(hottest->id).at("inlet_temp_c");
  }
  if (shortest != nullptr) {
    out.shortest_runtime_device_id = shortest->id;
    out.shortest_runtime_name = shortest->name;
    out.shortest_runtime_min = latest.at(shortest->id).at("runtime_minutes");
  }
  return out;
}

SiteCard FleetStore::site_card_of(const Site& site) const {
  SiteCard card;
  card.id = site.id;
  card.name = site.name;
  card.location = site.location;
  card.climate_note = site.climate_note;
  for (const Alert& alert : alerts) {
    if (alert.site_id != site.id) continue;
    ++card.active_alerts;
    if (alert.severity == Severity::Critical) ++card.critical_alerts;
  }
  for (const Room& room : rooms) {
    if (room.site_id == site.id) card.rooms.push_back(room_card(room));
  }
  return card;
}

RoomCard FleetStore::room_card(const Room& room) const {
  RoomCard card;
  card.id = room.id;
  card.name = room.name;
  card.role = room.role;
  for (const Device& device : devices) {
    if (device.room_id != room.id) continue;
    if (device.kind == DeviceKind::Ups) card.ups.push_back(ups_card(device));
    if (device.kind == DeviceKind::Cooling) card.cooling.push_back(cooling_card(device));
    if (device.kind == DeviceKind::Pdu) card.pdus.push_back(pdu_card(device));
  }
  for (const Rack& rack : racks) {
    if (rack.room_id == room.id) card.racks.push_back(rack_card(rack));
  }
  return card;
}

UpsCard FleetStore::ups_card(const Device& device) const {
  const Metrics& metrics = latest.at(device.id);
  if (!device.rated_kw) throw std::runtime_error("UPS " + device.id + " is missing a nameplate");
  return UpsCard{device.id,
                 device.name,
                 *device.rated_kw,
                 metrics.at("load_pct"),
                 metrics.at("output_kw"),
                 metrics.at("battery_health_pct"),
                 metrics.at("runtime_minutes"),
                 metrics.at("input_voltage_v"),
                 metrics.at("output_voltage_v"),
                 status_for({device.id})};
}

CoolingCard FleetStore::cooling_card(const Device& device) const {
  const Metrics& metrics = latest.at(device.id);
  if (!device.cooling_subtype || !device.setpoint_c) {
    throw std::runtime_error("Cooling unit " + device.id + " is missing subtype or setpoint");
  }
  return CoolingCard{device.id,
                     device.name,
                     *device.cooling_subtype,
                     metrics.at("supply_temp_c"),
                     metrics.at("return_temp_c"),
                     metrics.at("fan_speed_pct"),
                     *device.setpoint_c,
                     status_for({device.id})};
}

PduCard FleetStore::pdu_card(const Device& device) const {
  const Metrics& metrics = latest.at(device.id);
  if (!device.rack_id || !device.fed_by) throw std::runtime_error("PDU " + device.id + " is missing rack or upstream UPS");
  PduCard card;
  card.id = device.id;
  card.name = device.name;
  card.rack_id = *device.rack_id;
  card.fed_by = *device.fed_by;
  card.total_kw = metrics.at("total_kw");
  card.max_outlet_kw = metrics.at("max_outlet_kw");
  const auto outlets = latest_outlets.find(device.id);
  if (outlets != latest_outlets.end()) card.outlets = outlets->second;
  card.status = status_for({device.id});
  return card;
}

RackCard FleetStore::rack_card(const Rack& rack) const {
  const Metrics& metrics = latest.at(rack.id);
  const Device& pdu = pdu_for_rack(rack.id);
  return RackCard{rack.id,
                  rack.name,
                  metrics.at("inlet_temp_c"),
                  metrics.at("power_kw"),
                  pdu.id,
                  latest.at(pdu.id).at("max_outlet_kw"),
                  status_for({rack.id, pdu.id})};
}

std::vector<SeriesSpec> FleetStore::series_for(const std::string& kind) const {
  if (kind == "ups") {
    return {{"load_pct", "Load", "%"},
            {"output_kw", "Output", "kW"},
            {"battery_health_pct", "Battery health", "%"},
            {"runtime_minutes", "Runtime", "min"},
            {"input_voltage_v", "Input voltage", "V"},
            {"output_voltage_v", "Output voltage", "V"}};
  }
  if (kind == "pdu") {
    return {{"total_kw", "Total power", "kW"}, {"max_outlet_kw", "Hottest outlet", "kW"}};
  }
  if (kind == "cooling") {
    return {{"supply_temp_c", "Supply air", "°C"},
            {"return_temp_c", "Return air", "°C"},
            {"fan_speed_pct", "Fan speed", "%"}};
  }
  if (kind == "rack") {
    return {{"inlet_temp_c", "Inlet temperature", "°C"}, {"power_kw", "Rack power", "kW"}};
  }
  throw std::invalid_argument("Unsupported subject kind: " + kind);
}

}  // namespace heliospan
