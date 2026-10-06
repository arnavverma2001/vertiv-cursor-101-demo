#include "heliospan/simulator.hpp"

#include "format.hpp"
#include "heliospan/power.hpp"
#include "heliospan/store.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace heliospan {
namespace {

constexpr double kTau = 6.28318530717958647692;
constexpr double kOutletNoise = 0.01;
constexpr double kOutletDrift = 0.01;

uint64_t fnv1a(const std::string& text) {
  uint64_t hash = 14695981039346656037ULL;
  for (unsigned char ch : text) {
    hash ^= ch;
    hash *= 1099511628211ULL;
  }
  return hash;
}

double phase_of(const std::string& subject_id, const std::string& metric) {
  const uint64_t hash = fnv1a(subject_id + ":" + metric);
  const double unit = static_cast<double>(hash % 65536) / 65535.0;
  return unit * kTau;
}

double jitter(const std::string& subject_id, const std::string& metric, int tick, double noise) {
  const uint64_t hash = fnv1a(subject_id + ":" + metric + ":" + std::to_string(tick));
  const double unit = static_cast<double>(hash >> 11) / 9007199254740992.0;
  return (unit * 2.0 - 1.0) * noise;
}

double vary(double baseline, const std::string& subject_id, const std::string& metric, int tick, double noise,
            double drift, double period) {
  const double wave = std::sin((static_cast<double>(tick) / period) * kTau + phase_of(subject_id, metric)) * drift;
  return baseline + wave + jitter(subject_id, metric, tick, noise);
}

Metrics cooling_metrics(FleetStore& store, const std::string& device_id, int tick) {
  const Device& device = store.devices_by_id.at(device_id);
  const Metrics& base = store.baselines.at(device_id);
  double supply = vary(base.at("supply_temp_c"), device_id, "supply_temp_c", tick, 0.06, 0.08, 26);
  double return_air = vary(base.at("return_temp_c"), device_id, "return_temp_c", tick, 0.08, 0.12, 30);
  double fan = vary(base.at("fan_speed_pct"), device_id, "fan_speed_pct", tick, 0.35, 0.4, 18);
  supply = clamp(supply, 12.0, 35.0);
  if (return_air < supply + 4.0) return_air = supply + 4.0;
  const double setpoint = device.setpoint_c.value_or(18.0);
  return {
      {"supply_temp_c", round_n(supply, 1)},
      {"return_temp_c", round_n(return_air, 1)},
      {"fan_speed_pct", round_n(clamp(fan, 0.0, 100.0), 1)},
      {"setpoint_c", setpoint},
  };
}

std::vector<Outlet> outlets_for(FleetStore& store, const std::string& pdu_id, int tick) {
  std::vector<Outlet> outlets;
  const auto& baselines = store.outlet_baselines.at(pdu_id);
  outlets.reserve(baselines.size());
  for (std::size_t index = 0; index < baselines.size(); ++index) {
    const std::string label = std::to_string(index + 1);
    const double kw = vary(baselines[index], pdu_id, "outlet_" + label, tick, kOutletNoise, kOutletDrift, 24);
    outlets.push_back(Outlet{label, label, round_n(clamp(kw, 0.0, 12.0), 2)});
  }
  return outlets;
}

Metrics ups_metrics(FleetStore& store, const std::string& device_id, int tick) {
  const Device& device = store.devices_by_id.at(device_id);
  const Metrics& base = store.baselines.at(device_id);
  if (!device.rated_kw || !device.full_load_runtime_min) {
    throw std::runtime_error("UPS " + device_id + " is missing nameplate data");
  }
  double it_kw = 0;
  for (const Device* pdu : store.pdus_for_ups(device_id)) {
    it_kw += store.latest.at(pdu->id).at("total_kw");
  }
  double load_pct = (it_kw + device.auxiliary_kw) / *device.rated_kw * 100.0;
  load_pct += 0.35 * std::sin((static_cast<double>(tick) / 22.0) * kTau);
  load_pct = clamp(load_pct, 0.0, 100.0);

  double battery = vary(base.at("battery_health_pct"), device_id, "battery_health_pct", tick, 0.08, 0.05, 40);
  battery = clamp(battery, 0.0, 100.0);
  const double runtime = estimate_runtime_minutes(battery, load_pct, *device.full_load_runtime_min);
  double input_v = vary(base.at("input_voltage_v"), device_id, "input_voltage_v", tick, 0.5, 0.4, 20);
  double output_v = vary(base.at("output_voltage_v"), device_id, "output_voltage_v", tick, 0.25, 0.2, 16);
  // A few ticks of sag on the network-room UPS, then it recovers.
  if (device_id == "ash-ups-b" && tick >= 30 && tick % 30 < 5) input_v = 438.0;
  const double output_kw = *device.rated_kw * load_pct / 100.0;
  return {
      {"load_pct", round_n(load_pct, 1)},
      {"battery_health_pct", round_n(battery, 1)},
      {"runtime_minutes", round_n(runtime, 1)},
      {"input_voltage_v", round_n(clamp(input_v, 350.0, 560.0), 1)},
      {"output_voltage_v", round_n(clamp(output_v, 180.0, 240.0), 1)},
      {"output_kw", round_n(output_kw, 2)},
  };
}

}  // namespace

void simulate_tick(FleetStore& store, TimePoint now) {
  const int tick = store.tick_index;
  for (const Device& device : store.devices) {
    if (device.kind == DeviceKind::Ups) continue;
    if (device.kind == DeviceKind::Cooling) {
      store.record(device.id, now, cooling_metrics(store, device.id, tick));
    } else if (device.kind == DeviceKind::Pdu) {
      auto outlets = outlets_for(store, device.id, tick);
      double total = 0;
      double hottest = 0;
      for (const Outlet& outlet : outlets) {
        total += outlet.kw;
        hottest = std::max(hottest, outlet.kw);
      }
      store.latest_outlets[device.id] = std::move(outlets);
      store.record(device.id, now, {{"total_kw", round_n(total, 2)}, {"max_outlet_kw", round_n(hottest, 2)}});
    } else {
      throw std::invalid_argument(std::string("Unsupported device kind: ") + to_string(device.kind));
    }
  }

  for (const Rack& rack : store.racks) {
    const Device& pdu = store.pdu_for_rack(rack.id);
    const auto& outlets = store.latest_outlets.at(pdu.id);
    double power = 0;
    for (const Outlet& outlet : outlets) power += outlet.kw;
    const double inlet =
        vary(store.baselines.at(rack.id).at("inlet_temp_c"), rack.id, "inlet_temp_c", tick, 0.05, 0.08, 28);
    store.record(rack.id, now,
                 {{"inlet_temp_c", round_n(clamp(inlet, 12.0, 45.0), 1)}, {"power_kw", round_n(power, 2)}});
  }

  for (const Device& device : store.devices) {
    if (device.kind != DeviceKind::Ups) continue;
    store.record(device.id, now, ups_metrics(store, device.id, tick));
  }
}

}  // namespace heliospan
