#include "heliospan/seed.hpp"

namespace heliospan {
namespace {

Device ups(const std::string& id, const std::string& name, const std::string& site, const std::string& room,
           double rated_kw, double auxiliary_kw, double runtime) {
  Device device;
  device.id = id;
  device.name = name;
  device.kind = DeviceKind::Ups;
  device.site_id = site;
  device.room_id = room;
  device.rated_kw = rated_kw;
  device.auxiliary_kw = auxiliary_kw;
  device.full_load_runtime_min = runtime;
  return device;
}

Device cooling(const std::string& id, const std::string& name, CoolingSubtype subtype, const std::string& site,
               const std::string& room, double setpoint_c) {
  Device device;
  device.id = id;
  device.name = name;
  device.kind = DeviceKind::Cooling;
  device.site_id = site;
  device.room_id = room;
  device.cooling_subtype = subtype;
  device.setpoint_c = setpoint_c;
  return device;
}

Device pdu(const std::string& id, const std::string& name, const std::string& rack, const std::string& ups_id,
           const std::string& site, const std::string& room) {
  Device device;
  device.id = id;
  device.name = name;
  device.kind = DeviceKind::Pdu;
  device.site_id = site;
  device.room_id = room;
  device.rack_id = rack;
  device.fed_by = ups_id;
  return device;
}

}  // namespace

std::vector<Site> build_sites() {
  return {
      {"ashford", "Ashford Campus", "Columbus, Ohio", "Temperate. Mechanical plant is on the roof."},
      {"meridian", "Meridian Hall", "Phoenix, Arizona", "Hot-dry. Cooling units carry a higher base load."},
  };
}

std::vector<Room> build_rooms() {
  return {
      {"hall-a", "ashford", "Hall A", "Compute"},
      {"hall-b", "ashford", "Hall B", "Network and storage"},
      {"room-1", "meridian", "Room 1", "Compute"},
      {"room-2", "meridian", "Room 2", "High-density compute"},
  };
}

std::vector<Rack> build_racks() {
  return {
      {"a01", "ashford", "hall-a", "A01"}, {"a02", "ashford", "hall-a", "A02"}, {"a03", "ashford", "hall-a", "A03"},
      {"b01", "ashford", "hall-b", "B01"}, {"b02", "ashford", "hall-b", "B02"}, {"m01", "meridian", "room-1", "M01"},
      {"m02", "meridian", "room-1", "M02"}, {"m03", "meridian", "room-1", "M03"}, {"h01", "meridian", "room-2", "H01"},
      {"h02", "meridian", "room-2", "H02"},
  };
}

std::vector<Device> build_devices() {
  return {
      ups("ash-ups-a", "UPS-A", "ashford", "hall-a", 36, 1.2, 12),
      ups("ash-ups-b", "UPS-B", "ashford", "hall-b", 20, 0.4, 15),
      ups("mer-ups-1", "UPS-1", "meridian", "room-1", 55, 1.0, 10),
      ups("mer-ups-2", "UPS-2", "meridian", "room-2", 62, 1.4, 14),
      cooling("ash-crac-a", "CRAC-A", CoolingSubtype::Crac, "ashford", "hall-a", 18),
      cooling("ash-crah-b", "CRAH-B", CoolingSubtype::Crah, "ashford", "hall-b", 18),
      cooling("mer-crac-1", "CRAC-1", CoolingSubtype::Crac, "meridian", "room-1", 18),
      cooling("mer-crac-2", "CRAC-2", CoolingSubtype::Crac, "meridian", "room-2", 18),
      pdu("pdu-a01", "PDU-A01", "a01", "ash-ups-a", "ashford", "hall-a"),
      pdu("pdu-a02", "PDU-A02", "a02", "ash-ups-a", "ashford", "hall-a"),
      pdu("pdu-a03", "PDU-A03", "a03", "ash-ups-a", "ashford", "hall-a"),
      pdu("pdu-b01", "PDU-B01", "b01", "ash-ups-b", "ashford", "hall-b"),
      pdu("pdu-b02", "PDU-B02", "b02", "ash-ups-b", "ashford", "hall-b"),
      pdu("pdu-m01", "PDU-M01", "m01", "mer-ups-1", "meridian", "room-1"),
      pdu("pdu-m02", "PDU-M02", "m02", "mer-ups-1", "meridian", "room-1"),
      pdu("pdu-m03", "PDU-M03", "m03", "mer-ups-1", "meridian", "room-1"),
      pdu("pdu-h01", "PDU-H01", "h01", "mer-ups-2", "meridian", "room-2"),
      pdu("pdu-h02", "PDU-H02", "h02", "mer-ups-2", "meridian", "room-2"),
  };
}

std::map<std::string, Metrics> metric_baselines() {
  return {
      {"ash-ups-a", {{"battery_health_pct", 98.6}, {"input_voltage_v", 480.0}, {"output_voltage_v", 208.0}}},
      {"ash-ups-b", {{"battery_health_pct", 99.1}, {"input_voltage_v", 481.0}, {"output_voltage_v", 207.6}}},
      {"mer-ups-1", {{"battery_health_pct", 46.5}, {"input_voltage_v", 479.0}, {"output_voltage_v", 208.4}}},
      {"mer-ups-2", {{"battery_health_pct", 97.4}, {"input_voltage_v", 478.5}, {"output_voltage_v", 207.2}}},
      {"ash-crac-a", {{"supply_temp_c", 18.3}, {"return_temp_c", 28.0}, {"fan_speed_pct", 55.0}}},
      {"ash-crah-b", {{"supply_temp_c", 17.7}, {"return_temp_c", 25.5}, {"fan_speed_pct", 41.0}}},
      {"mer-crac-1", {{"supply_temp_c", 19.0}, {"return_temp_c", 30.2}, {"fan_speed_pct", 67.0}}},
      {"mer-crac-2", {{"supply_temp_c", 22.4}, {"return_temp_c", 33.6}, {"fan_speed_pct", 93.0}}},
      {"a01", {{"inlet_temp_c", 23.2}}},
      {"a02", {{"inlet_temp_c", 23.8}}},
      {"a03", {{"inlet_temp_c", 22.6}}},
      {"b01", {{"inlet_temp_c", 21.4}}},
      {"b02", {{"inlet_temp_c", 21.1}}},
      {"m01", {{"inlet_temp_c", 24.2}}},
      {"m02", {{"inlet_temp_c", 24.8}}},
      {"m03", {{"inlet_temp_c", 24.0}}},
      {"h01", {{"inlet_temp_c", 33.0}}},
      {"h02", {{"inlet_temp_c", 28.4}}},
  };
}

std::map<std::string, std::vector<double>> build_outlet_baselines() {
  // PDU-H02 outlet 4 sits above the outlet power rule.
  return {
      {"pdu-a01", {2.10, 1.90, 1.70, 1.50, 1.30, 1.00}},
      {"pdu-a02", {2.20, 2.00, 1.80, 1.50, 1.20, 0.80}},
      {"pdu-a03", {2.00, 1.80, 1.70, 1.60, 1.40, 1.00}},
      {"pdu-b01", {1.00, 0.80, 0.60, 0.50, 0.40, 0.30}},
      {"pdu-b02", {0.90, 0.70, 0.50, 0.40, 0.30, 0.20}},
      {"pdu-m01", {2.00, 1.80, 1.60, 1.40, 1.20, 0.80}},
      {"pdu-m02", {2.40, 2.20, 2.00, 1.60, 1.40, 1.00}},
      {"pdu-m03", {2.10, 1.90, 1.70, 1.50, 1.20, 0.90}},
      {"pdu-h01", {3.40, 3.30, 3.20, 3.20, 3.10, 3.00, 2.90, 2.80}},
      {"pdu-h02", {3.50, 3.40, 3.30, 6.55, 3.20, 3.10, 3.00, 2.80}},
  };
}

}  // namespace heliospan
