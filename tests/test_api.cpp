#include "heliospan/rules.hpp"
#include "heliospan/server.hpp"
#include "heliospan/store.hpp"

#include <gtest/gtest.h>
#include <httplib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>

namespace {

class ApiFixture : public testing::Test {
 protected:
  void SetUp() override {
    store = std::make_unique<heliospan::FleetStore>(heliospan::FleetStore::seed());
    store->tick();
    const int port = server.bind_to_any_port("127.0.0.1");
    ASSERT_GT(port, 0);
    heliospan::install_routes(server, *store, mu, false, heliospan::web_dir_from_env_or_default());
    thread = std::thread([this] { server.listen_after_bind(); });
    client = std::make_unique<httplib::Client>("127.0.0.1", port);
    client->set_connection_timeout(2, 0);
    client->set_read_timeout(2, 0);
  }

  void TearDown() override {
    server.stop();
    if (thread.joinable()) thread.join();
  }

  nlohmann::json get_json(const std::string& path, int* status = nullptr) {
    auto response = client->Get(path);
    EXPECT_TRUE(response);
    if (!response) return nullptr;
    if (status) *status = response->status;
    if (response->body.empty()) return nullptr;
    return nlohmann::json::parse(response->body);
  }

  std::unique_ptr<heliospan::FleetStore> store;
  std::mutex mu;
  httplib::Server server;
  std::thread thread;
  std::unique_ptr<httplib::Client> client;
};

TEST_F(ApiFixture, Health) {
  const auto body = get_json("/api/health");
  EXPECT_EQ(body["status"], "ok");
  EXPECT_FALSE(body["telemetry_running"]);
  EXPECT_EQ(body["devices"], 18);
  EXPECT_EQ(body["racks"], 10);
  EXPECT_EQ(body["tick"], 1);
}

TEST_F(ApiFixture, FleetListsBothSitesAndMatchesSummary) {
  const auto fleet = get_json("/api/fleet");
  ASSERT_EQ(fleet["sites"].size(), 2u);
  EXPECT_EQ(fleet["sites"][0]["id"], "ashford");
  EXPECT_EQ(fleet["sites"][1]["id"], "meridian");
  double rack_sum = 0;
  int racks = 0;
  for (const auto& site : fleet["sites"]) {
    for (const auto& room : site["rooms"]) {
      for (const auto& rack : room["racks"]) {
        rack_sum += rack["power_kw"].get<double>();
        ++racks;
      }
    }
  }
  EXPECT_EQ(racks, 10);
  const double rounded = std::round(rack_sum * 100.0) / 100.0;
  EXPECT_DOUBLE_EQ(fleet["summary"]["it_load_kw"].get<double>(), rounded);
  EXPECT_EQ(fleet["summary"]["rack_count"], 10);
  EXPECT_EQ(fleet["summary"]["active_alerts"], fleet["alerts"].size());
}

TEST_F(ApiFixture, SummaryRouteMatchesFleet) {
  const auto fleet = get_json("/api/fleet")["summary"];
  const auto summary = get_json("/api/summary");
  EXPECT_EQ(summary, fleet);
}

TEST_F(ApiFixture, SeededConditionsRaiseExpectedAlerts) {
  std::set<std::tuple<std::string, std::string, std::string>> found;
  for (const auto& alert : get_json("/api/alerts")) {
    found.emplace(alert["rule_id"], alert["subject_id"], alert["severity"]);
  }
  EXPECT_TRUE(found.count({"rack-inlet-high", "h01", "critical"}));
  EXPECT_TRUE(found.count({"ups-battery-low", "mer-ups-1", "critical"}));
  EXPECT_TRUE(found.count({"ups-load-high", "mer-ups-2", "warning"}));
  EXPECT_TRUE(found.count({"pdu-outlet-power", "pdu-h02", "warning"}));
  EXPECT_TRUE(found.count({"cooling-supply-high", "mer-crac-2", "warning"}));
  EXPECT_TRUE(found.count({"cooling-fan-high", "mer-crac-2", "warning"}));
}

TEST_F(ApiFixture, AlertsAreSortedCriticalFirst) {
  const auto alerts = get_json("/api/alerts");
  ASSERT_FALSE(alerts.empty());
  int last = -1;
  for (const auto& alert : alerts) {
    const int rank = alert["severity"] == "critical" ? 0 : 1;
    EXPECT_GE(rank, last);
    last = rank;
  }
}

TEST_F(ApiFixture, RulesCatalogMatchesEngineConstants) {
  const auto body = get_json("/api/rules");
  std::set<std::string> ids;
  nlohmann::json load;
  for (const auto& rule : body) {
    ids.insert(rule["id"]);
    if (rule["id"] == "ups-load-high") load = rule;
  }
  std::set<std::string> expected;
  for (const auto& rule : heliospan::rule_catalog()) expected.insert(rule.id);
  EXPECT_EQ(ids, expected);
  EXPECT_DOUBLE_EQ(load["warning_threshold"].get<double>(), heliospan::UPS_LOAD_WARNING_PCT);
  EXPECT_NE(load["description"].get<std::string>().find("nameplate"), std::string::npos);
}

TEST_F(ApiFixture, DeviceDetailAndHistory) {
  int status = 0;
  auto missing = client->Get("/api/devices/not-a-device");
  ASSERT_TRUE(missing);
  EXPECT_EQ(missing->status, 404);

  const auto body = get_json("/api/devices/mer-ups-1", &status);
  EXPECT_EQ(status, 200);
  EXPECT_EQ(body["kind"], "ups");
  EXPECT_EQ(body["site_id"], "meridian");
  EXPECT_EQ(body["history"].size(), 1u);
  EXPECT_TRUE(body["history"][0].contains("load_pct"));
  bool battery = false;
  for (const auto& alert : body["alerts"]) {
    if (alert["rule_id"] == "ups-battery-low") battery = true;
  }
  EXPECT_TRUE(battery);

  store->tick();
  const auto again = get_json("/api/devices/mer-ups-1");
  EXPECT_EQ(again["history"].size(), 2u);
}

TEST_F(ApiFixture, RackDetailIncludesHotOutlet) {
  auto missing = client->Get("/api/racks/nope");
  ASSERT_TRUE(missing);
  EXPECT_EQ(missing->status, 404);

  const auto body = get_json("/api/racks/h02");
  EXPECT_EQ(body["name"], "H02");
  EXPECT_EQ(body["pdu"]["id"], "pdu-h02");
  EXPECT_EQ(body["ups_id"], "mer-ups-2");
  bool has_four = false;
  double hottest = 0;
  for (const auto& outlet : body["pdu"]["outlets"]) {
    if (outlet["label"] == "4") has_four = true;
    hottest = std::max(hottest, outlet["kw"].get<double>());
  }
  EXPECT_TRUE(has_four);
  EXPECT_GT(hottest, 6.0);
}

TEST_F(ApiFixture, SiteRouteAndUnknownSite) {
  const auto ashford = get_json("/api/sites/ashford");
  EXPECT_EQ(ashford["location"], "Columbus, Ohio");
  ASSERT_EQ(ashford["rooms"].size(), 2u);
  EXPECT_EQ(ashford["rooms"][0]["id"], "hall-a");
  EXPECT_EQ(ashford["rooms"][1]["id"], "hall-b");
  auto missing = client->Get("/api/sites/nowhere");
  ASSERT_TRUE(missing);
  EXPECT_EQ(missing->status, 404);
}

TEST_F(ApiFixture, IndexServesTheDashboard) {
  auto response = client->Get("/");
  ASSERT_TRUE(response);
  EXPECT_EQ(response->status, 200);
  EXPECT_NE(response->body.find("HelioSpan"), std::string::npos);
}

}  // namespace
