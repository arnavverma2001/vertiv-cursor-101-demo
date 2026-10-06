"""Rule engine coverage: bands, exact thresholds, and alert identity."""

from datetime import timedelta

from heliospan.models.domain import Outlet, Severity
from heliospan.services.rules import (
    BATTERY_HEALTH_CRITICAL_PCT,
    BATTERY_HEALTH_WARNING_PCT,
    FAN_WARNING_PCT,
    OUTLET_WARNING_KW,
    RACK_INLET_CRITICAL_C,
    RACK_INLET_WARNING_C,
    RUNTIME_WARNING_MIN,
    SUPPLY_DELTA_WARNING_C,
    UPS_LOAD_CRITICAL_PCT,
    UPS_LOAD_WARNING_PCT,
    evaluate_readings,
    reconcile_alerts,
    sort_alerts,
)
from tests.conftest import reading


def _ids(alerts):
    return {(alert.rule_id, alert.severity) for alert in alerts}


def test_ups_load_warning_and_critical_are_mutually_exclusive(now):
    warning = evaluate_readings(
        [reading(metrics={"load_pct": UPS_LOAD_WARNING_PCT + 1})],
        now=now,
    )
    assert ("ups-load-high", Severity.warning) in _ids(warning)
    assert ("ups-load-high", Severity.critical) not in _ids(warning)

    critical = evaluate_readings(
        [reading(metrics={"load_pct": UPS_LOAD_CRITICAL_PCT + 1})],
        now=now,
    )
    load_alerts = [alert for alert in critical if alert.rule_id == "ups-load-high"]
    assert len(load_alerts) == 1
    assert load_alerts[0].severity is Severity.critical


def test_ups_load_at_exact_warning_threshold_is_quiet(now):
    alerts = evaluate_readings(
        [reading(metrics={"load_pct": UPS_LOAD_WARNING_PCT})],
        now=now,
    )
    assert not any(alert.rule_id == "ups-load-high" for alert in alerts)


def test_healthy_ups_is_quiet(now):
    alerts = evaluate_readings([reading()], now=now)
    assert alerts == []


def test_battery_health_bands(now):
    warning = evaluate_readings(
        [reading(metrics={"battery_health_pct": BATTERY_HEALTH_WARNING_PCT - 1})],
        now=now,
    )
    assert ("ups-battery-low", Severity.warning) in _ids(warning)

    at_warning = evaluate_readings(
        [reading(metrics={"battery_health_pct": BATTERY_HEALTH_WARNING_PCT})],
        now=now,
    )
    assert not any(alert.rule_id == "ups-battery-low" for alert in at_warning)

    critical = evaluate_readings(
        [reading(metrics={"battery_health_pct": BATTERY_HEALTH_CRITICAL_PCT - 0.1})],
        now=now,
    )
    battery = [alert for alert in critical if alert.rule_id == "ups-battery-low"]
    assert len(battery) == 1
    assert battery[0].severity is Severity.critical


def test_runtime_below_warning(now):
    alerts = evaluate_readings(
        [reading(metrics={"runtime_minutes": RUNTIME_WARNING_MIN - 0.5})],
        now=now,
    )
    assert ("ups-runtime-low", Severity.warning) in _ids(alerts)
    quiet = evaluate_readings(
        [reading(metrics={"runtime_minutes": RUNTIME_WARNING_MIN})],
        now=now,
    )
    assert not any(alert.rule_id == "ups-runtime-low" for alert in quiet)


def test_output_voltage_outside_warning_band(now):
    alerts = evaluate_readings(
        [reading(metrics={"output_voltage_v": 190.0})],
        now=now,
    )
    voltage = [alert for alert in alerts if alert.rule_id == "ups-output-voltage"]
    assert len(voltage) == 1
    assert voltage[0].severity is Severity.critical
    assert "208" in voltage[0].message


def test_input_voltage_mild_sag_is_warning(now):
    # 5% of 480 V is 24 V. 450 V is outside 5% and inside 8%.
    alerts = evaluate_readings(
        [reading(metrics={"input_voltage_v": 450.0})],
        now=now,
    )
    voltage = [alert for alert in alerts if alert.rule_id == "ups-input-voltage"]
    assert len(voltage) == 1
    assert voltage[0].severity is Severity.warning


def test_rack_inlet_follows_ashrae_bands(now):
    recommended = evaluate_readings(
        [reading(kind="rack", subject_id="h02", name="Rack H02", metrics={"inlet_temp_c": RACK_INLET_WARNING_C + 0.4})],
        now=now,
    )
    assert ("rack-inlet-high", Severity.warning) in _ids(recommended)

    allowable = evaluate_readings(
        [reading(kind="rack", subject_id="h01", name="Rack H01", metrics={"inlet_temp_c": RACK_INLET_CRITICAL_C + 0.4})],
        now=now,
    )
    inlet = [alert for alert in allowable if alert.rule_id == "rack-inlet-high"]
    assert len(inlet) == 1
    assert inlet[0].severity is Severity.critical

    at_recommended = evaluate_readings(
        [reading(kind="rack", metrics={"inlet_temp_c": RACK_INLET_WARNING_C})],
        now=now,
    )
    assert not any(alert.rule_id == "rack-inlet-high" for alert in at_recommended)


def test_pdu_reports_the_worst_outlet_only(now):
    outlets = [
        Outlet(id="1", label="1", kw=1.2),
        Outlet(id="4", label="4", kw=OUTLET_WARNING_KW + 0.4),
        Outlet(id="5", label="5", kw=1.0),
    ]
    alerts = evaluate_readings(
        [reading(kind="pdu", subject_id="pdu-h02", name="PDU-H02", outlets=outlets)],
        now=now,
    )
    assert len(alerts) == 1
    assert alerts[0].rule_id == "pdu-outlet-power"
    assert alerts[0].severity is Severity.warning
    assert "Outlet 4" in alerts[0].message


def test_pdu_with_no_outlets_does_not_alert(now):
    alerts = evaluate_readings(
        [reading(kind="pdu", subject_id="pdu-empty", outlets=[])],
        now=now,
    )
    assert alerts == []


def test_cooling_supply_and_fan(now):
    alerts = evaluate_readings(
        [
            reading(
                kind="cooling",
                subject_id="mer-crac-2",
                name="CRAC-2",
                metrics={
                    "supply_temp_c": 18.0 + SUPPLY_DELTA_WARNING_C + 0.4,
                    "setpoint_c": 18.0,
                    "fan_speed_pct": FAN_WARNING_PCT + 1,
                },
            )
        ],
        now=now,
    )
    assert ("cooling-supply-high", Severity.warning) in _ids(alerts)
    assert ("cooling-fan-high", Severity.warning) in _ids(alerts)


def test_reconcile_keeps_opened_at_and_drops_cleared_alerts(now):
    hot = evaluate_readings([reading(metrics={"load_pct": UPS_LOAD_WARNING_PCT + 2})], now=now)
    opened = reconcile_alerts([], hot)
    assert opened[0].opened_at == now

    later = now + timedelta(seconds=4)
    still = evaluate_readings([reading(metrics={"load_pct": UPS_LOAD_WARNING_PCT + 3})], now=later)
    kept = reconcile_alerts(opened, still)
    assert len(kept) == 1
    assert kept[0].opened_at == now
    assert kept[0].last_seen == later

    calm = evaluate_readings([reading(metrics={"load_pct": 40})], now=later)
    assert reconcile_alerts(kept, calm) == []


def test_sort_alerts_puts_critical_before_warning(now):
    battery = evaluate_readings(
        [reading(subject_id="ups-z", name="UPS-Z", metrics={"battery_health_pct": 40})],
        now=now,
    )
    load = evaluate_readings(
        [reading(subject_id="ups-a", name="UPS-A", metrics={"load_pct": UPS_LOAD_WARNING_PCT + 1})],
        now=now,
    )
    ordered = sort_alerts(load + battery)
    assert [alert.severity for alert in ordered] == [Severity.critical, Severity.warning]
