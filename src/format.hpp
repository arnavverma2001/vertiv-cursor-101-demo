#pragma once

#include "heliospan/domain.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

namespace heliospan {

inline double round_n(double value, int digits) {
  const double scale = std::pow(10.0, digits);
  return std::round(value * scale) / scale;
}

inline double clamp(double value, double low, double high) { return std::min(high, std::max(low, value)); }

inline std::string fixed(double value, int digits) {
  std::ostringstream out;
  out.setf(std::ios::fixed);
  out << std::setprecision(digits) << value;
  return out.str();
}

inline std::string format_unit(double value, const std::string& unit) {
  if (unit == "%") return fixed(value, 1) + "%";
  if (unit == "°C") return fixed(value, 1) + "°C";
  if (unit == "kW") return fixed(value, 2) + " kW";
  if (unit == "V") return fixed(value, 0) + " V";
  if (unit == "min") return fixed(value, 1) + " min";
  return fixed(value, 1) + " " + unit;
}

inline std::string format_percent_ratio(double ratio) { return fixed(ratio * 100.0, 0) + "%"; }

inline std::string format_time(TimePoint tp) {
  const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch());
  const std::time_t seconds = static_cast<std::time_t>(millis.count() / 1000);
  int ms = static_cast<int>(millis.count() % 1000);
  if (ms < 0) ms += 1000;
  std::tm tm{};
  gmtime_r(&seconds, &tm);
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                tm.tm_hour, tm.tm_min, tm.tm_sec, ms);
  return buf;
}

}  // namespace heliospan
