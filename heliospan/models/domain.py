"""Pydantic models for the fleet, telemetry, and alerts.

Device metrics are flat dictionaries keyed by measurement name. A new device
kind (for example a coolant distribution unit) can add keys without a
migration; the simulator, rule engine, snapshot, and device page each
dispatch on ``kind``.
"""

from datetime import datetime
from enum import Enum
from typing import Literal

from pydantic import BaseModel, ConfigDict, Field

DeviceKind = Literal["ups", "pdu", "cooling"]
CoolingSubtype = Literal["crac", "crah"]
SubjectKind = Literal["ups", "pdu", "cooling", "rack"]


class Severity(str, Enum):
    """Alert severity. Critical outranks warning when the API sorts alerts."""

    critical = "critical"
    warning = "warning"


class Outlet(BaseModel):
    id: str
    label: str
    kw: float


class Site(BaseModel):
    id: str
    name: str
    location: str
    climate_note: str


class Room(BaseModel):
    id: str
    site_id: str
    name: str
    role: str


class Rack(BaseModel):
    id: str
    site_id: str
    room_id: str
    name: str


class Device(BaseModel):
    id: str
    name: str
    kind: DeviceKind
    site_id: str
    room_id: str
    rack_id: str | None = None
    rated_kw: float | None = None
    auxiliary_kw: float = 0.0
    full_load_runtime_min: float | None = None
    cooling_subtype: CoolingSubtype | None = None
    setpoint_c: float | None = None
    fed_by: str | None = None


class HistoryPoint(BaseModel):
    ts: datetime
    metrics: dict[str, float]


class HistoryRow(BaseModel):
    """One chart sample. Metric keys are added beside ``ts``."""

    model_config = ConfigDict(extra="allow")

    ts: datetime


class Alert(BaseModel):
    id: str
    rule_id: str
    rule_name: str
    severity: Severity
    subject_id: str
    subject_name: str
    subject_kind: SubjectKind
    site_id: str
    site_name: str
    room_id: str
    room_name: str
    message: str
    metric: str
    value: float
    threshold: float
    unit: str
    opened_at: datetime
    last_seen: datetime


class Reading(BaseModel):
    """One subject's latest metrics, detached from the fleet graph.

    Rules evaluate readings and nothing else, so they stay pure functions.
    """

    subject_id: str
    subject_name: str
    kind: SubjectKind
    site_id: str
    site_name: str
    room_id: str
    room_name: str
    rack_id: str | None = None
    metrics: dict[str, float]
    outlets: list[Outlet] = Field(default_factory=list)


class ChartThreshold(BaseModel):
    metric: str
    value: float
    severity: Severity
    label: str


class RuleInfo(BaseModel):
    id: str
    name: str
    description: str
    subject_kind: SubjectKind
    comparator: Literal["above", "below", "outside_band"]
    unit: str
    warning_threshold: float | None = None
    critical_threshold: float | None = None


class SeriesSpec(BaseModel):
    key: str
    label: str
    unit: str


class UpsCard(BaseModel):
    id: str
    name: str
    rated_kw: float
    load_pct: float
    output_kw: float
    battery_health_pct: float
    runtime_minutes: float
    input_voltage_v: float
    output_voltage_v: float
    status: str


class CoolingCard(BaseModel):
    id: str
    name: str
    subtype: CoolingSubtype
    supply_temp_c: float
    return_temp_c: float
    fan_speed_pct: float
    setpoint_c: float
    status: str


class PduCard(BaseModel):
    id: str
    name: str
    rack_id: str
    fed_by: str
    total_kw: float
    max_outlet_kw: float
    outlets: list[Outlet]
    status: str


class RackCard(BaseModel):
    id: str
    name: str
    inlet_temp_c: float
    power_kw: float
    pdu_id: str
    max_outlet_kw: float
    status: str


class RoomCard(BaseModel):
    id: str
    name: str
    role: str
    ups: list[UpsCard]
    cooling: list[CoolingCard]
    pdus: list[PduCard]
    racks: list[RackCard]


class SiteCard(BaseModel):
    id: str
    name: str
    location: str
    climate_note: str
    active_alerts: int
    critical_alerts: int
    rooms: list[RoomCard]


class Summary(BaseModel):
    site_count: int
    room_count: int
    rack_count: int
    device_count: int
    it_load_kw: float
    ups_output_kw: float
    active_alerts: int
    critical_alerts: int
    warning_alerts: int
    hottest_rack_id: str | None
    hottest_rack_name: str | None
    hottest_site_name: str | None
    hottest_inlet_c: float | None
    shortest_runtime_device_id: str | None
    shortest_runtime_name: str | None
    shortest_runtime_min: float | None


class FleetSnapshot(BaseModel):
    generated_at: datetime
    tick: int
    tick_seconds: float
    summary: Summary
    sites: list[SiteCard]
    alerts: list[Alert]


class DeviceDetail(BaseModel):
    id: str
    name: str
    kind: DeviceKind
    subtype: CoolingSubtype | None = None
    site_id: str
    site_name: str
    room_id: str
    room_name: str
    rack_id: str | None = None
    rack_name: str | None = None
    rated_kw: float | None = None
    setpoint_c: float | None = None
    fed_by: str | None = None
    fed_by_name: str | None = None
    metrics: dict[str, float]
    outlets: list[Outlet]
    series: list[SeriesSpec]
    thresholds: list[ChartThreshold]
    history: list[HistoryRow]
    alerts: list[Alert]


class NamedLink(BaseModel):
    id: str
    name: str


class RackDetail(BaseModel):
    id: str
    name: str
    site_id: str
    site_name: str
    room_id: str
    room_name: str
    inlet_temp_c: float
    power_kw: float
    pdu: PduCard
    ups_id: str
    ups_name: str
    cooling: list[NamedLink]
    series: list[SeriesSpec]
    thresholds: list[ChartThreshold]
    history: list[HistoryRow]
    alerts: list[Alert]


class HealthStatus(BaseModel):
    status: Literal["ok"]
    tick: int
    telemetry_running: bool
    devices: int
    racks: int
