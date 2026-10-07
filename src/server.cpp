#include "heliospan/server.hpp"

#include "heliospan/json_codec.hpp"
#include "heliospan/rules.hpp"

#include <httplib.h>

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace heliospan {
namespace {

void json_response(httplib::Response& res, const nlohmann::json& body, int status = 200) {
  res.status = status;
  res.set_content(body.dump(), "application/json");
}

void not_found(httplib::Response& res, const char* detail) {
  json_response(res, {{"detail", detail}}, 404);
}

std::string read_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return {};
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

void send_file(httplib::Response& res, const std::string& path, const char* content_type) {
  const std::string body = read_file(path);
  if (body.empty() && !std::ifstream(path)) {
    res.status = 404;
    res.set_content("Not found", "text/plain");
    return;
  }
  res.set_content(body, content_type);
  res.set_header("Cache-Control", "no-cache");
}

}  // namespace

void install_routes(httplib::Server& server, FleetStore& store, std::mutex& store_mu, bool telemetry_running,
                    const std::string& web_dir) {
  server.Get("/api/health", [&store, &store_mu, telemetry_running](const httplib::Request&, httplib::Response& res) {
    std::lock_guard<std::mutex> lock(store_mu);
    json_response(res, {{"status", "ok"},
                        {"tick", store.tick_index},
                        {"telemetry_running", telemetry_running},
                        {"devices", store.devices.size()},
                        {"racks", store.racks.size()}});
  });

  server.Get("/api/fleet", [&store, &store_mu](const httplib::Request&, httplib::Response& res) {
    std::lock_guard<std::mutex> lock(store_mu);
    json_response(res, to_json(store.snapshot()));
  });

  server.Get("/api/summary", [&store, &store_mu](const httplib::Request&, httplib::Response& res) {
    std::lock_guard<std::mutex> lock(store_mu);
    json_response(res, to_json(store.snapshot().summary));
  });

  server.Get("/api/alerts", [&store, &store_mu](const httplib::Request&, httplib::Response& res) {
    std::lock_guard<std::mutex> lock(store_mu);
    nlohmann::json body = nlohmann::json::array();
    for (const Alert& alert : store.alerts) body.push_back(to_json(alert));
    json_response(res, body);
  });

  server.Get("/api/rules", [](const httplib::Request&, httplib::Response& res) {
    nlohmann::json body = nlohmann::json::array();
    for (const RuleInfo& rule : rule_catalog()) body.push_back(to_json(rule));
    json_response(res, body);
  });

  server.Get(R"(/api/sites/([^/]+))", [&store, &store_mu](const httplib::Request& req, httplib::Response& res) {
    std::lock_guard<std::mutex> lock(store_mu);
    const auto card = store.site_card(req.matches[1]);
    if (!card) {
      not_found(res, "Site not found");
      return;
    }
    json_response(res, to_json(*card));
  });

  server.Get(R"(/api/racks/([^/]+))", [&store, &store_mu](const httplib::Request& req, httplib::Response& res) {
    std::lock_guard<std::mutex> lock(store_mu);
    const auto detail = store.rack_detail(req.matches[1]);
    if (!detail) {
      not_found(res, "Rack not found");
      return;
    }
    json_response(res, to_json(*detail));
  });

  server.Get(R"(/api/devices/([^/]+))", [&store, &store_mu](const httplib::Request& req, httplib::Response& res) {
    std::lock_guard<std::mutex> lock(store_mu);
    const auto detail = store.device_detail(req.matches[1]);
    if (!detail) {
      not_found(res, "Device not found");
      return;
    }
    json_response(res, to_json(*detail));
  });

  server.Get("/", [web_dir](const httplib::Request&, httplib::Response& res) {
    send_file(res, web_dir + "/index.html", "text/html; charset=utf-8");
  });
  server.Get("/favicon.svg", [web_dir](const httplib::Request&, httplib::Response& res) {
    send_file(res, web_dir + "/favicon.svg", "image/svg+xml");
  });
  server.Get("/assets/app.js", [web_dir](const httplib::Request&, httplib::Response& res) {
    send_file(res, web_dir + "/app.js", "text/javascript; charset=utf-8");
  });
  server.Get("/assets/styles.css", [web_dir](const httplib::Request&, httplib::Response& res) {
    send_file(res, web_dir + "/styles.css", "text/css; charset=utf-8");
  });
}

std::string web_dir_from_env_or_default() {
  if (const char* env = std::getenv("HELIOSPAN_WEB_DIR")) {
    if (env[0] != '\0') return env;
  }
#ifdef HELIOSPAN_WEB_DIR
  return HELIOSPAN_WEB_DIR;
#else
  return "web";
#endif
}

}  // namespace heliospan
