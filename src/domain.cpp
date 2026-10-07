#include "heliospan/domain.hpp"

#include <stdexcept>

namespace heliospan {

const char* to_string(DeviceKind kind) {
  switch (kind) {
    case DeviceKind::Ups:
      return "ups";
    case DeviceKind::Pdu:
      return "pdu";
    case DeviceKind::Cooling:
      return "cooling";
  }
  throw std::invalid_argument("Unsupported device kind");
}

const char* to_string(CoolingSubtype subtype) {
  switch (subtype) {
    case CoolingSubtype::Crac:
      return "crac";
    case CoolingSubtype::Crah:
      return "crah";
  }
  throw std::invalid_argument("Unsupported cooling subtype");
}

const char* to_string(SubjectKind kind) {
  switch (kind) {
    case SubjectKind::Ups:
      return "ups";
    case SubjectKind::Pdu:
      return "pdu";
    case SubjectKind::Cooling:
      return "cooling";
    case SubjectKind::Rack:
      return "rack";
  }
  throw std::invalid_argument("Unsupported subject kind");
}

const char* to_string(Severity severity) {
  switch (severity) {
    case Severity::Critical:
      return "critical";
    case Severity::Warning:
      return "warning";
  }
  throw std::invalid_argument("Unsupported severity");
}

DeviceKind device_kind_from(const std::string& text) {
  if (text == "ups") return DeviceKind::Ups;
  if (text == "pdu") return DeviceKind::Pdu;
  if (text == "cooling") return DeviceKind::Cooling;
  throw std::invalid_argument("Unsupported device kind: " + text);
}

SubjectKind subject_kind_from(const std::string& text) {
  if (text == "ups") return SubjectKind::Ups;
  if (text == "pdu") return SubjectKind::Pdu;
  if (text == "cooling") return SubjectKind::Cooling;
  if (text == "rack") return SubjectKind::Rack;
  throw std::invalid_argument("Unsupported subject kind: " + text);
}

}  // namespace heliospan
