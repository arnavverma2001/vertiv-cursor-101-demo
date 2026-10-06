"""A few HTTP routes over a seeded, simulator-paused fleet."""

from heliospan.services.rules import (
    UPS_LOAD_WARNING_PCT,
    rule_catalog,
)


def test_health(client):
    body = client.get("/api/health").json()
    assert body["status"] == "ok"
    assert body["telemetry_running"] is False
    assert body["devices"] == 18
    assert body["racks"] == 10
    assert body["tick"] == 1


def test_fleet_lists_both_sites_and_matches_summary(client):
    fleet = client.get("/api/fleet").json()
    assert [site["id"] for site in fleet["sites"]] == ["ashford", "meridian"]
    rack_power = [
        rack["power_kw"]
        for site in fleet["sites"]
        for room in site["rooms"]
        for rack in room["racks"]
    ]
    assert len(rack_power) == 10
    assert fleet["summary"]["it_load_kw"] == round(sum(rack_power), 2)
    assert fleet["summary"]["rack_count"] == 10
    assert fleet["summary"]["active_alerts"] == len(fleet["alerts"])


def test_summary_route_matches_fleet(client):
    fleet = client.get("/api/fleet").json()["summary"]
    summary = client.get("/api/summary").json()
    assert summary == fleet


def test_seeded_conditions_raise_expected_alerts(client):
    found = {(alert["rule_id"], alert["subject_id"], alert["severity"]) for alert in client.get("/api/alerts").json()}
    assert ("rack-inlet-high", "h01", "critical") in found
    assert ("ups-battery-low", "mer-ups-1", "critical") in found
    assert ("ups-load-high", "mer-ups-2", "warning") in found
    assert ("pdu-outlet-power", "pdu-h02", "warning") in found
    assert ("cooling-supply-high", "mer-crac-2", "warning") in found
    assert ("cooling-fan-high", "mer-crac-2", "warning") in found


def test_alerts_are_sorted_critical_first(client):
    severities = [alert["severity"] for alert in client.get("/api/alerts").json()]
    assert severities
    assert severities == sorted(severities, key=lambda severity: 0 if severity == "critical" else 1)


def test_rules_catalog_matches_engine_constants(client):
    body = client.get("/api/rules").json()
    expected = {rule.id for rule in rule_catalog()}
    assert {rule["id"] for rule in body} == expected
    load = next(rule for rule in body if rule["id"] == "ups-load-high")
    assert load["warning_threshold"] == UPS_LOAD_WARNING_PCT
    assert "nameplate" in load["description"]


def test_device_detail_and_history(client):
    missing = client.get("/api/devices/not-a-device")
    assert missing.status_code == 404

    body = client.get("/api/devices/mer-ups-1").json()
    assert body["kind"] == "ups"
    assert body["site_id"] == "meridian"
    assert len(body["history"]) == 1
    assert "load_pct" in body["history"][0]
    assert any(alert["rule_id"] == "ups-battery-low" for alert in body["alerts"])

    store = client.app.state.store
    store.tick()
    again = client.get("/api/devices/mer-ups-1").json()
    assert len(again["history"]) == 2


def test_rack_detail_includes_hot_outlet(client):
    missing = client.get("/api/racks/nope")
    assert missing.status_code == 404

    body = client.get("/api/racks/h02").json()
    assert body["name"] == "H02"
    assert body["pdu"]["id"] == "pdu-h02"
    assert body["ups_id"] == "mer-ups-2"
    labels = [outlet["label"] for outlet in body["pdu"]["outlets"]]
    assert "4" in labels
    assert max(outlet["kw"] for outlet in body["pdu"]["outlets"]) > 6


def test_site_route_and_unknown_site(client):
    ashford = client.get("/api/sites/ashford").json()
    assert ashford["location"] == "Columbus, Ohio"
    assert [room["id"] for room in ashford["rooms"]] == ["hall-a", "hall-b"]
    assert client.get("/api/sites/nowhere").status_code == 404


def test_index_serves_the_dashboard(client):
    response = client.get("/")
    assert response.status_code == 200
    assert "HelioSpan" in response.text
