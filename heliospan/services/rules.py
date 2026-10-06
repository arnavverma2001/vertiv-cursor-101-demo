"""Threshold rules for power and thermal telemetry.

The simulator records a reading per subject each tick. ``evaluate_readings``
returns the alerts those readings currently justify. ``reconcile_alerts``
keeps ``opened_at`` stable while a condition stays true and drops alerts that
have cleared.

Each metric emits at most one alert: a critical breach suppresses the warning
for the same rule. Unknown subject kinds produce no alerts.
"""

from datetime import datetime

from heliospan.models.domain import Alert, ChartThreshold, Reading, RuleInfo, Severity

UPS_LOAD_WARNING_PCT = 85.0
UPS_LOAD_CRITICAL_PCT = 95.0

BATTERY_HEALTH_WARNING_PCT = 70.0
BATTERY_HEALTH_CRITICAL_PCT = 50.0

RUNTIME_WARNING_MIN = 10.0
RUNTIME_CRITICAL_MIN = 5.0

INPUT_VOLTAGE_NOMINAL_V = 480.0
OUTPUT_VOLTAGE_NOMINAL_V = 208.0
VOLTAGE_WARNING_BAND = 0.05
VOLTAGE_CRITICAL_BAND = 0.08

# ASHRAE TC 9.9 class A1: 18–27°C recommended, up to 32°C allowable.
RACK_INLET_WARNING_C = 27.0
RACK_INLET_CRITICAL_C = 32.0

OUTLET_WARNING_KW = 6.0
OUTLET_CRITICAL_KW = 8.0

SUPPLY_DELTA_WARNING_C = 2.5
SUPPLY_DELTA_CRITICAL_C = 5.0

FAN_WARNING_PCT = 90.0
FAN_CRITICAL_PCT = 97.0

_SEVERITY_RANK = {Severity.critical: 0, Severity.warning: 1}


def evaluate_readings(readings: list[Reading], *, now: datetime) -> list[Alert]:
    alerts: list[Alert] = []
    for reading in readings:
        alerts.extend(_alerts_for(reading, now))
    return alerts


def reconcile_alerts(previous: list[Alert], current: list[Alert]) -> list[Alert]:
    """Keep the original open time when the same condition is still true."""

    prior = {alert.id: alert for alert in previous}
    merged: list[Alert] = []
    for alert in current:
        old = prior.get(alert.id)
        if old is not None:
            alert = alert.model_copy(update={"opened_at": old.opened_at})
        merged.append(alert)
    return sort_alerts(merged)


def sort_alerts(alerts: list[Alert]) -> list[Alert]:
    return sorted(
        alerts,
        key=lambda alert: (
            _SEVERITY_RANK[alert.severity],
            alert.opened_at,
            alert.subject_name,
            alert.rule_id,
        ),
    )


def rule_catalog() -> list[RuleInfo]:
    """Operator-facing catalog. Threshold text is built from the constants."""

    return [
        RuleInfo(
            id="ups-load-high",
            name="UPS load high",
            description=(
                f"Output load above {UPS_LOAD_WARNING_PCT:.0f}% of nameplate, "
                f"critical above {UPS_LOAD_CRITICAL_PCT:.0f}%."
            ),
            subject_kind="ups",
            comparator="above",
            unit="%",
            warning_threshold=UPS_LOAD_WARNING_PCT,
            critical_threshold=UPS_LOAD_CRITICAL_PCT,
        ),
        RuleInfo(
            id="ups-battery-low",
            name="Battery health low",
            description=(
                f"Battery health below {BATTERY_HEALTH_WARNING_PCT:.0f}%, "
                f"critical below {BATTERY_HEALTH_CRITICAL_PCT:.0f}%."
            ),
            subject_kind="ups",
            comparator="below",
            unit="%",
            warning_threshold=BATTERY_HEALTH_WARNING_PCT,
            critical_threshold=BATTERY_HEALTH_CRITICAL_PCT,
        ),
        RuleInfo(
            id="ups-runtime-low",
            name="Runtime low",
            description=(
                f"Estimated runtime below {RUNTIME_WARNING_MIN:.0f} min, "
                f"critical below {RUNTIME_CRITICAL_MIN:.0f} min."
            ),
            subject_kind="ups",
            comparator="below",
            unit="min",
            warning_threshold=RUNTIME_WARNING_MIN,
            critical_threshold=RUNTIME_CRITICAL_MIN,
        ),
        RuleInfo(
            id="ups-input-voltage",
            name="Input voltage out of band",
            description=(
                f"Input voltage outside ±{VOLTAGE_WARNING_BAND:.0%} of "
                f"{INPUT_VOLTAGE_NOMINAL_V:.0f} V, critical outside "
                f"±{VOLTAGE_CRITICAL_BAND:.0%}."
            ),
            subject_kind="ups",
            comparator="outside_band",
            unit="V",
            warning_threshold=VOLTAGE_WARNING_BAND,
            critical_threshold=VOLTAGE_CRITICAL_BAND,
        ),
        RuleInfo(
            id="ups-output-voltage",
            name="Output voltage out of band",
            description=(
                f"Output voltage outside ±{VOLTAGE_WARNING_BAND:.0%} of "
                f"{OUTPUT_VOLTAGE_NOMINAL_V:.0f} V, critical outside "
                f"±{VOLTAGE_CRITICAL_BAND:.0%}."
            ),
            subject_kind="ups",
            comparator="outside_band",
            unit="V",
            warning_threshold=VOLTAGE_WARNING_BAND,
            critical_threshold=VOLTAGE_CRITICAL_BAND,
        ),
        RuleInfo(
            id="rack-inlet-high",
            name="Rack inlet high",
            description=(
                f"Rack inlet above the ASHRAE recommended {RACK_INLET_WARNING_C:.0f}°C, "
                f"critical above the A1 allowable {RACK_INLET_CRITICAL_C:.0f}°C."
            ),
            subject_kind="rack",
            comparator="above",
            unit="°C",
            warning_threshold=RACK_INLET_WARNING_C,
            critical_threshold=RACK_INLET_CRITICAL_C,
        ),
        RuleInfo(
            id="pdu-outlet-power",
            name="Outlet power high",
            description=(
                f"Any outlet above {OUTLET_WARNING_KW:.1f} kW, "
                f"critical above {OUTLET_CRITICAL_KW:.1f} kW."
            ),
            subject_kind="pdu",
            comparator="above",
            unit="kW",
            warning_threshold=OUTLET_WARNING_KW,
            critical_threshold=OUTLET_CRITICAL_KW,
        ),
        RuleInfo(
            id="cooling-supply-high",
            name="Supply air high",
            description=(
                f"Supply air more than {SUPPLY_DELTA_WARNING_C:.1f}°C above setpoint, "
                f"critical above {SUPPLY_DELTA_CRITICAL_C:.1f}°C."
            ),
            subject_kind="cooling",
            comparator="above",
            unit="°C",
            warning_threshold=SUPPLY_DELTA_WARNING_C,
            critical_threshold=SUPPLY_DELTA_CRITICAL_C,
        ),
        RuleInfo(
            id="cooling-fan-high",
            name="Fan speed high",
            description=(
                f"Cooling fan speed above {FAN_WARNING_PCT:.0f}%, "
                f"critical above {FAN_CRITICAL_PCT:.0f}%."
            ),
            subject_kind="cooling",
            comparator="above",
            unit="%",
            warning_threshold=FAN_WARNING_PCT,
            critical_threshold=FAN_CRITICAL_PCT,
        ),
    ]


def chart_thresholds(kind: str, *, setpoint_c: float | None = None) -> list[ChartThreshold]:
    """Threshold lines a device or rack chart can draw.

    Both warning and critical are included. The chart decides which lines sit
    close enough to the series to be worth showing.
    """

    if kind == "ups":
        return [
            _line("load_pct", UPS_LOAD_WARNING_PCT, Severity.warning, f"{UPS_LOAD_WARNING_PCT:.0f}% warning"),
            _line("load_pct", UPS_LOAD_CRITICAL_PCT, Severity.critical, f"{UPS_LOAD_CRITICAL_PCT:.0f}% critical"),
            _line(
                "battery_health_pct",
                BATTERY_HEALTH_WARNING_PCT,
                Severity.warning,
                f"{BATTERY_HEALTH_WARNING_PCT:.0f}% warning",
            ),
            _line(
                "battery_health_pct",
                BATTERY_HEALTH_CRITICAL_PCT,
                Severity.critical,
                f"{BATTERY_HEALTH_CRITICAL_PCT:.0f}% critical",
            ),
            _line("runtime_minutes", RUNTIME_WARNING_MIN, Severity.warning, f"{RUNTIME_WARNING_MIN:.0f} min warning"),
            _line("runtime_minutes", RUNTIME_CRITICAL_MIN, Severity.critical, f"{RUNTIME_CRITICAL_MIN:.0f} min critical"),
            _line(
                "input_voltage_v",
                INPUT_VOLTAGE_NOMINAL_V * (1 - VOLTAGE_WARNING_BAND),
                Severity.warning,
                "input low",
            ),
            _line(
                "input_voltage_v",
                INPUT_VOLTAGE_NOMINAL_V * (1 + VOLTAGE_WARNING_BAND),
                Severity.warning,
                "input high",
            ),
            _line(
                "output_voltage_v",
                OUTPUT_VOLTAGE_NOMINAL_V * (1 - VOLTAGE_WARNING_BAND),
                Severity.warning,
                "output low",
            ),
            _line(
                "output_voltage_v",
                OUTPUT_VOLTAGE_NOMINAL_V * (1 + VOLTAGE_WARNING_BAND),
                Severity.warning,
                "output high",
            ),
        ]
    if kind == "rack":
        return [
            _line("inlet_temp_c", RACK_INLET_WARNING_C, Severity.warning, f"{RACK_INLET_WARNING_C:.0f}°C recommended"),
            _line("inlet_temp_c", RACK_INLET_CRITICAL_C, Severity.critical, f"{RACK_INLET_CRITICAL_C:.0f}°C allowable"),
        ]
    if kind == "cooling" and setpoint_c is not None:
        return [
            _line(
                "supply_temp_c",
                setpoint_c + SUPPLY_DELTA_WARNING_C,
                Severity.warning,
                f"+{SUPPLY_DELTA_WARNING_C:.1f}°C",
            ),
            _line(
                "supply_temp_c",
                setpoint_c + SUPPLY_DELTA_CRITICAL_C,
                Severity.critical,
                f"+{SUPPLY_DELTA_CRITICAL_C:.1f}°C",
            ),
            _line("fan_speed_pct", FAN_WARNING_PCT, Severity.warning, f"{FAN_WARNING_PCT:.0f}% warning"),
            _line("fan_speed_pct", FAN_CRITICAL_PCT, Severity.critical, f"{FAN_CRITICAL_PCT:.0f}% critical"),
        ]
    if kind == "pdu":
        return [
            _line("total_kw", OUTLET_WARNING_KW, Severity.warning, f"{OUTLET_WARNING_KW:.0f} kW outlet"),
            _line("max_outlet_kw", OUTLET_WARNING_KW, Severity.warning, f"{OUTLET_WARNING_KW:.0f} kW warning"),
            _line("max_outlet_kw", OUTLET_CRITICAL_KW, Severity.critical, f"{OUTLET_CRITICAL_KW:.0f} kW critical"),
        ]
    if kind not in {"ups", "pdu", "cooling", "rack"}:
        raise ValueError(f"Unsupported subject kind: {kind}")
    return []


def _line(metric: str, value: float, severity: Severity, label: str) -> ChartThreshold:
    return ChartThreshold(metric=metric, value=value, severity=severity, label=label)


def _alerts_for(reading: Reading, now: datetime) -> list[Alert]:
    if reading.kind == "ups":
        return _ups_alerts(reading, now)
    if reading.kind == "pdu":
        return _pdu_alerts(reading, now)
    if reading.kind == "cooling":
        return _cooling_alerts(reading, now)
    if reading.kind == "rack":
        return _rack_alerts(reading, now)
    raise ValueError(f"Unsupported subject kind: {reading.kind}")


def _ups_alerts(reading: Reading, now: datetime) -> list[Alert]:
    alerts: list[Alert] = []
    band = _above(
        reading,
        now,
        metric="load_pct",
        warning=UPS_LOAD_WARNING_PCT,
        critical=UPS_LOAD_CRITICAL_PCT,
        rule_id="ups-load-high",
        rule_name="UPS load high",
        unit="%",
        noun="Load",
    )
    if band:
        alerts.append(band)
    band = _below(
        reading,
        now,
        metric="battery_health_pct",
        warning=BATTERY_HEALTH_WARNING_PCT,
        critical=BATTERY_HEALTH_CRITICAL_PCT,
        rule_id="ups-battery-low",
        rule_name="Battery health low",
        unit="%",
        noun="Battery health",
    )
    if band:
        alerts.append(band)
    band = _below(
        reading,
        now,
        metric="runtime_minutes",
        warning=RUNTIME_WARNING_MIN,
        critical=RUNTIME_CRITICAL_MIN,
        rule_id="ups-runtime-low",
        rule_name="Runtime low",
        unit="min",
        noun="Estimated runtime",
    )
    if band:
        alerts.append(band)
    band = _voltage(
        reading,
        now,
        metric="input_voltage_v",
        nominal=INPUT_VOLTAGE_NOMINAL_V,
        rule_id="ups-input-voltage",
        rule_name="Input voltage out of band",
        noun="Input voltage",
    )
    if band:
        alerts.append(band)
    band = _voltage(
        reading,
        now,
        metric="output_voltage_v",
        nominal=OUTPUT_VOLTAGE_NOMINAL_V,
        rule_id="ups-output-voltage",
        rule_name="Output voltage out of band",
        noun="Output voltage",
    )
    if band:
        alerts.append(band)
    return alerts


def _pdu_alerts(reading: Reading, now: datetime) -> list[Alert]:
    if not reading.outlets:
        return []
    worst = max(reading.outlets, key=lambda outlet: outlet.kw)
    if worst.kw > OUTLET_CRITICAL_KW:
        severity = Severity.critical
        threshold = OUTLET_CRITICAL_KW
    elif worst.kw > OUTLET_WARNING_KW:
        severity = Severity.warning
        threshold = OUTLET_WARNING_KW
    else:
        return []
    message = (
        f"Outlet {worst.label} is drawing {_fmt(worst.kw, 'kW')} "
        f"({severity.value} above {_fmt(threshold, 'kW')})."
    )
    return [
        _make(
            reading,
            now,
            rule_id="pdu-outlet-power",
            rule_name="Outlet power high",
            severity=severity,
            metric="max_outlet_kw",
            value=worst.kw,
            threshold=threshold,
            unit="kW",
            message=message,
        )
    ]


def _cooling_alerts(reading: Reading, now: datetime) -> list[Alert]:
    alerts: list[Alert] = []
    supply = reading.metrics["supply_temp_c"]
    setpoint = reading.metrics["setpoint_c"]
    delta = supply - setpoint
    if delta > SUPPLY_DELTA_CRITICAL_C:
        severity = Severity.critical
        threshold = SUPPLY_DELTA_CRITICAL_C
    elif delta > SUPPLY_DELTA_WARNING_C:
        severity = Severity.warning
        threshold = SUPPLY_DELTA_WARNING_C
    else:
        severity = None
        threshold = 0.0
    if severity is not None:
        alerts.append(
            _make(
                reading,
                now,
                rule_id="cooling-supply-high",
                rule_name="Supply air high",
                severity=severity,
                metric="supply_temp_c",
                value=round(supply, 1),
                threshold=round(setpoint + threshold, 1),
                unit="°C",
                message=(
                    f"Supply air is {_fmt(supply, '°C')}, {_fmt(delta, '°C')} above the "
                    f"{_fmt(setpoint, '°C')} setpoint ({severity.value} above {_fmt(threshold, '°C')})."
                ),
            )
        )
    band = _above(
        reading,
        now,
        metric="fan_speed_pct",
        warning=FAN_WARNING_PCT,
        critical=FAN_CRITICAL_PCT,
        rule_id="cooling-fan-high",
        rule_name="Fan speed high",
        unit="%",
        noun="Fan speed",
    )
    if band:
        alerts.append(band)
    return alerts


def _rack_alerts(reading: Reading, now: datetime) -> list[Alert]:
    band = _above(
        reading,
        now,
        metric="inlet_temp_c",
        warning=RACK_INLET_WARNING_C,
        critical=RACK_INLET_CRITICAL_C,
        rule_id="rack-inlet-high",
        rule_name="Rack inlet high",
        unit="°C",
        noun="Rack inlet",
    )
    return [band] if band else []


def _above(
    reading: Reading,
    now: datetime,
    *,
    metric: str,
    warning: float,
    critical: float,
    rule_id: str,
    rule_name: str,
    unit: str,
    noun: str,
) -> Alert | None:
    value = reading.metrics[metric]
    if value > critical:
        severity = Severity.critical
        threshold = critical
    elif value > warning:
        severity = Severity.warning
        threshold = warning
    else:
        return None
    return _make(
        reading,
        now,
        rule_id=rule_id,
        rule_name=rule_name,
        severity=severity,
        metric=metric,
        value=value,
        threshold=threshold,
        unit=unit,
        message=f"{noun} is {_fmt(value, unit)} ({severity.value} above {_fmt(threshold, unit)}).",
    )


def _below(
    reading: Reading,
    now: datetime,
    *,
    metric: str,
    warning: float,
    critical: float,
    rule_id: str,
    rule_name: str,
    unit: str,
    noun: str,
) -> Alert | None:
    value = reading.metrics[metric]
    if value < critical:
        severity = Severity.critical
        threshold = critical
    elif value < warning:
        severity = Severity.warning
        threshold = warning
    else:
        return None
    return _make(
        reading,
        now,
        rule_id=rule_id,
        rule_name=rule_name,
        severity=severity,
        metric=metric,
        value=value,
        threshold=threshold,
        unit=unit,
        message=f"{noun} is {_fmt(value, unit)} ({severity.value} below {_fmt(threshold, unit)}).",
    )


def _voltage(
    reading: Reading,
    now: datetime,
    *,
    metric: str,
    nominal: float,
    rule_id: str,
    rule_name: str,
    noun: str,
) -> Alert | None:
    value = reading.metrics[metric]
    deviation = abs(value - nominal) / nominal
    if deviation > VOLTAGE_CRITICAL_BAND:
        severity = Severity.critical
        band = VOLTAGE_CRITICAL_BAND
    elif deviation > VOLTAGE_WARNING_BAND:
        severity = Severity.warning
        band = VOLTAGE_WARNING_BAND
    else:
        return None
    if value >= nominal:
        threshold = nominal * (1 + band)
    else:
        threshold = nominal * (1 - band)
    message = (
        f"{noun} is {_fmt(value, 'V')}, outside ±{band:.0%} of {_fmt(nominal, 'V')} "
        f"({severity.value})."
    )
    return _make(
        reading,
        now,
        rule_id=rule_id,
        rule_name=rule_name,
        severity=severity,
        metric=metric,
        value=value,
        threshold=round(threshold, 1),
        unit="V",
        message=message,
    )


def _make(
    reading: Reading,
    now: datetime,
    *,
    rule_id: str,
    rule_name: str,
    severity: Severity,
    metric: str,
    value: float,
    threshold: float,
    unit: str,
    message: str,
) -> Alert:
    return Alert(
        id=f"{rule_id}:{reading.subject_id}",
        rule_id=rule_id,
        rule_name=rule_name,
        severity=severity,
        subject_id=reading.subject_id,
        subject_name=reading.subject_name,
        subject_kind=reading.kind,
        site_id=reading.site_id,
        site_name=reading.site_name,
        room_id=reading.room_id,
        room_name=reading.room_name,
        message=message,
        metric=metric,
        value=round(value, 2) if unit == "kW" else round(value, 1),
        threshold=threshold,
        unit=unit,
        opened_at=now,
        last_seen=now,
    )


def _fmt(value: float, unit: str) -> str:
    if unit == "%":
        return f"{value:.1f}%"
    if unit == "°C":
        return f"{value:.1f}°C"
    if unit == "kW":
        return f"{value:.2f} kW"
    if unit == "V":
        return f"{value:.0f} V"
    if unit == "min":
        return f"{value:.1f} min"
    return f"{value:.1f} {unit}"
