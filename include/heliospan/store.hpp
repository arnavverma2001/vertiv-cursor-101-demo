#pragma once

#include "heliospan/domain.hpp"

#include <deque>
#include <initializer_list>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace heliospan {

struct UpsCard {
  std::string id;
  std::string name;
  double rated_kw = 0;
  double load_pct = 0;
  double output_kw = 0;
  double battery_health_pct = 0;
  double runtime_minutes = 0;
  double input_voltage_v = 0;
  double output_voltage_v = 0;
  std::string status;
};

struct CoolingCard {
  std::string id;
  std::string name;
  CoolingSubtype subtype = CoolingSubtype::Crac;
  double supply_temp_c = 0;
  double return_temp_c = 0;
  double fan_speed_pct = 0;
  double setpoint_c = 0;
  std::string status;
};

struct PduCard {
  std::string id;
  std::string name;
  std::string rack_id;
  std::string fed_by;
  double total_kw = 0;
  double max_outlet_kw = 0;
  std::vector<Outlet> outlets;
  std::string status;
};

struct RackCard {
  std::string id;
  std::string name;
  double inlet_temp_c = 0;
  double power_kw = 0;
  std::string pdu_id;
  double max_outlet_kw = 0;
  std::string status;
};

struct RoomCard {
  std::string id;
  std::string name;
  std::string role;
  std::vector<UpsCard> ups;
  std::vector<CoolingCard> cooling;
  std::vector<PduCard> pdus;
  std::vector<RackCard> racks;
};

struct SiteCard {
  std::string id;
  std::string name;
  std::string location;
  std::string climate_note;
  int active_alerts = 0;
  int critical_alerts = 0;
  std::vector<RoomCard> rooms;
};

struct Summary {
  int site_count = 0;
  int room_count = 0;
  int rack_count = 0;
  int device_count = 0;
  double it_load_kw = 0;
  double ups_output_kw = 0;
  int active_alerts = 0;
  int critical_alerts = 0;
  int warning_alerts = 0;
  std::optional<std::string> hottest_rack_id;
  std::optional<std::string> hottest_rack_name;
  std::optional<std::string> hottest_site_name;
  std::optional<double> hottest_inlet_c;
  std::optional<std::string> shortest_runtime_device_id;
  std::optional<std::string> shortest_runtime_name;
  std::optional<double> shortest_runtime_min;
};

struct FleetSnapshot {
  TimePoint generated_at;
  int tick = 0;
  double tick_seconds = 0;
  Summary summary;
  std::vector<SiteCard> sites;
  std::vector<Alert> alerts;
};

struct DeviceDetail {
  std::string id;
  std::string name;
  DeviceKind kind = DeviceKind::Ups;
  std::optional<CoolingSubtype> subtype;
  std::string site_id;
  std::string site_name;
  std::string room_id;
  std::string room_name;
  std::optional<std::string> rack_id;
  std::optional<std::string> rack_name;
  std::optional<double> rated_kw;
  std::optional<double> setpoint_c;
  std::optional<std::string> fed_by;
  std::optional<std::string> fed_by_name;
  Metrics metrics;
  std::vector<Outlet> outlets;
  std::vector<SeriesSpec> series;
  std::vector<ChartThreshold> thresholds;
  std::vector<HistoryPoint> history;
  std::vector<Alert> alerts;
};

struct RackDetail {
  std::string id;
  std::string name;
  std::string site_id;
  std::string site_name;
  std::string room_id;
  std::string room_name;
  double inlet_temp_c = 0;
  double power_kw = 0;
  PduCard pdu;
  std::string ups_id;
  std::string ups_name;
  std::vector<NamedLink> cooling;
  std::vector<SeriesSpec> series;
  std::vector<ChartThreshold> thresholds;
  std::vector<HistoryPoint> history;
  std::vector<Alert> alerts;
};

// In-memory fleet. Not thread-safe; the HTTP server locks around it.
class FleetStore {
 public:
  FleetStore(std::vector<Site> sites, std::vector<Room> rooms, std::vector<Rack> racks,
             std::vector<Device> devices, std::map<std::string, Metrics> baselines,
             std::map<std::string, std::vector<double>> outlet_baselines);

  static FleetStore seed();

  void tick(std::optional<TimePoint> now = std::nullopt);
  void record(const std::string& subject_id, TimePoint now, Metrics metrics);

  std::vector<const Device*> pdus_for_ups(const std::string& ups_id) const;
  const Device& pdu_for_rack(const std::string& rack_id) const;
  std::vector<Reading> readings() const;

  FleetSnapshot snapshot(std::optional<TimePoint> now = std::nullopt) const;
  std::optional<DeviceDetail> device_detail(const std::string& device_id) const;
  std::optional<RackDetail> rack_detail(const std::string& rack_id) const;
  std::optional<SiteCard> site_card(const std::string& site_id) const;

  std::string status_for(std::initializer_list<std::string> subject_ids) const;

  std::vector<Site> sites;
  std::vector<Room> rooms;
  std::vector<Rack> racks;
  std::vector<Device> devices;
  std::map<std::string, Metrics> baselines;
  std::map<std::string, std::vector<double>> outlet_baselines;
  std::unordered_map<std::string, Site> sites_by_id;
  std::unordered_map<std::string, Room> rooms_by_id;
  std::unordered_map<std::string, Rack> racks_by_id;
  std::unordered_map<std::string, Device> devices_by_id;
  std::unordered_map<std::string, Metrics> latest;
  std::unordered_map<std::string, std::vector<Outlet>> latest_outlets;
  std::unordered_map<std::string, std::deque<HistoryPoint>> history;
  std::vector<Alert> alerts;
  int tick_index = 0;

 private:
  Summary summary() const;
  SiteCard site_card_of(const Site& site) const;
  RoomCard room_card(const Room& room) const;
  UpsCard ups_card(const Device& device) const;
  CoolingCard cooling_card(const Device& device) const;
  PduCard pdu_card(const Device& device) const;
  RackCard rack_card(const Rack& rack) const;
  std::vector<SeriesSpec> series_for(const std::string& kind) const;
};

}  // namespace heliospan
