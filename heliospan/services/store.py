"""In-memory fleet: topology, latest readings, history, and open alerts."""

from __future__ import annotations

from collections import defaultdict, deque
from datetime import datetime, timezone

from heliospan.config import HISTORY_LIMIT, TICK_SECONDS
from heliospan.models.domain import (
    Alert,
    CoolingCard,
    Device,
    DeviceDetail,
    FleetSnapshot,
    HistoryPoint,
    HistoryRow,
    PduCard,
    Rack,
    RackCard,
    RackDetail,
    NamedLink,
    Reading,
    Room,
    RoomCard,
    SeriesSpec,
    Site,
    SiteCard,
    Summary,
    UpsCard,
)
from heliospan.services.rules import chart_thresholds, evaluate_readings, reconcile_alerts
from heliospan.services.seed import (
    build_devices,
    build_racks,
    build_rooms,
    build_sites,
    metric_baselines,
    outlet_baselines,
)
from heliospan.services.simulator import simulate_tick

_STATUS_RANK = {"ok": 0, "warning": 1, "critical": 2}

_SERIES: dict[str, list[SeriesSpec]] = {
    "ups": [
        SeriesSpec(key="load_pct", label="Load", unit="%"),
        SeriesSpec(key="output_kw", label="Output", unit="kW"),
        SeriesSpec(key="battery_health_pct", label="Battery health", unit="%"),
        SeriesSpec(key="runtime_minutes", label="Runtime", unit="min"),
        SeriesSpec(key="input_voltage_v", label="Input voltage", unit="V"),
        SeriesSpec(key="output_voltage_v", label="Output voltage", unit="V"),
    ],
    "pdu": [
        SeriesSpec(key="total_kw", label="Total power", unit="kW"),
        SeriesSpec(key="max_outlet_kw", label="Hottest outlet", unit="kW"),
    ],
    "cooling": [
        SeriesSpec(key="supply_temp_c", label="Supply air", unit="°C"),
        SeriesSpec(key="return_temp_c", label="Return air", unit="°C"),
        SeriesSpec(key="fan_speed_pct", label="Fan speed", unit="%"),
    ],
    "rack": [
        SeriesSpec(key="inlet_temp_c", label="Inlet temperature", unit="°C"),
        SeriesSpec(key="power_kw", label="Rack power", unit="kW"),
    ],
}


class FleetStore:
    def __init__(
        self,
        sites: list[Site],
        rooms: list[Room],
        racks: list[Rack],
        devices: list[Device],
        baselines: dict[str, dict[str, float]],
        outlets: dict[str, list[float]],
    ) -> None:
        self.sites = sites
        self.rooms = rooms
        self.racks = racks
        self.devices = devices
        self.baselines = baselines
        self.outlet_baselines = outlets
        self.sites_by_id = {site.id: site for site in sites}
        self.rooms_by_id = {room.id: room for room in rooms}
        self.racks_by_id = {rack.id: rack for rack in racks}
        self.devices_by_id = {device.id: device for device in devices}
        self.latest: dict[str, dict[str, float]] = {}
        self.latest_outlets: dict[str, list] = {}
        self.history: dict[str, deque[HistoryPoint]] = defaultdict(lambda: deque(maxlen=HISTORY_LIMIT))
        self.alerts: list[Alert] = []
        self.tick_index = 0

    @classmethod
    def seed(cls) -> FleetStore:
        return cls(
            build_sites(),
            build_rooms(),
            build_racks(),
            build_devices(),
            metric_baselines(),
            outlet_baselines(),
        )

    def tick(self, now: datetime | None = None) -> None:
        self.tick_index += 1
        moment = now or datetime.now(timezone.utc)
        simulate_tick(self, moment)
        drafts = evaluate_readings(self.readings(), now=moment)
        self.alerts = reconcile_alerts(self.alerts, drafts)

    def record(self, subject_id: str, now: datetime, metrics: dict[str, float]) -> None:
        self.latest[subject_id] = metrics
        self.history[subject_id].append(HistoryPoint(ts=now, metrics=dict(metrics)))

    def pdus_for_ups(self, ups_id: str) -> list[Device]:
        return [device for device in self.devices if device.kind == "pdu" and device.fed_by == ups_id]

    def pdu_for_rack(self, rack_id: str) -> Device:
        matches = [device for device in self.devices if device.kind == "pdu" and device.rack_id == rack_id]
        if len(matches) != 1:
            raise ValueError(f"Expected one PDU on rack {rack_id}, found {len(matches)}")
        return matches[0]

    def readings(self) -> list[Reading]:
        rows: list[Reading] = []
        for device in self.devices:
            metrics = self.latest.get(device.id)
            if metrics is None:
                continue
            site = self.sites_by_id[device.site_id]
            room = self.rooms_by_id[device.room_id]
            rows.append(
                Reading(
                    subject_id=device.id,
                    subject_name=device.name,
                    kind=device.kind,
                    site_id=site.id,
                    site_name=site.name,
                    room_id=room.id,
                    room_name=room.name,
                    rack_id=device.rack_id,
                    metrics=metrics,
                    outlets=list(self.latest_outlets.get(device.id, [])),
                )
            )
        for rack in self.racks:
            metrics = self.latest.get(rack.id)
            if metrics is None:
                continue
            site = self.sites_by_id[rack.site_id]
            room = self.rooms_by_id[rack.room_id]
            rows.append(
                Reading(
                    subject_id=rack.id,
                    subject_name=f"Rack {rack.name}",
                    kind="rack",
                    site_id=site.id,
                    site_name=site.name,
                    room_id=room.id,
                    room_name=room.name,
                    rack_id=rack.id,
                    metrics=metrics,
                )
            )
        return rows

    def snapshot(self, *, now: datetime | None = None) -> FleetSnapshot:
        moment = now or datetime.now(timezone.utc)
        sites = [self._site_card(site) for site in self.sites]
        return FleetSnapshot(
            generated_at=moment,
            tick=self.tick_index,
            tick_seconds=TICK_SECONDS,
            summary=self._summary(),
            sites=sites,
            alerts=self.alerts,
        )

    def device_detail(self, device_id: str) -> DeviceDetail | None:
        device = self.devices_by_id.get(device_id)
        if device is None or device_id not in self.latest:
            return None
        site = self.sites_by_id[device.site_id]
        room = self.rooms_by_id[device.room_id]
        rack = self.racks_by_id.get(device.rack_id) if device.rack_id else None
        fed_by_name = None
        if device.fed_by:
            upstream = self.devices_by_id.get(device.fed_by)
            fed_by_name = upstream.name if upstream else device.fed_by
        return DeviceDetail(
            id=device.id,
            name=device.name,
            kind=device.kind,
            subtype=device.cooling_subtype,
            site_id=site.id,
            site_name=site.name,
            room_id=room.id,
            room_name=room.name,
            rack_id=rack.id if rack else None,
            rack_name=rack.name if rack else None,
            rated_kw=device.rated_kw,
            setpoint_c=device.setpoint_c,
            fed_by=device.fed_by,
            fed_by_name=fed_by_name,
            metrics=self.latest[device.id],
            outlets=list(self.latest_outlets.get(device.id, [])),
            series=_SERIES[device.kind],
            thresholds=chart_thresholds(device.kind, setpoint_c=device.setpoint_c),
            history=self._history_rows(device.id),
            alerts=[alert for alert in self.alerts if alert.subject_id == device.id],
        )

    def rack_detail(self, rack_id: str) -> RackDetail | None:
        rack = self.racks_by_id.get(rack_id)
        if rack is None or rack_id not in self.latest:
            return None
        site = self.sites_by_id[rack.site_id]
        room = self.rooms_by_id[rack.room_id]
        pdu = self._pdu_card(self.pdu_for_rack(rack.id))
        ups = self.devices_by_id[pdu.fed_by]
        cooling = [
            NamedLink(id=device.id, name=device.name)
            for device in self.devices
            if device.kind == "cooling" and device.room_id == rack.room_id
        ]
        metrics = self.latest[rack.id]
        rack_alerts = [
            alert
            for alert in self.alerts
            if alert.subject_id in {rack.id, pdu.id}
        ]
        return RackDetail(
            id=rack.id,
            name=rack.name,
            site_id=site.id,
            site_name=site.name,
            room_id=room.id,
            room_name=room.name,
            inlet_temp_c=metrics["inlet_temp_c"],
            power_kw=metrics["power_kw"],
            pdu=pdu,
            ups_id=ups.id,
            ups_name=ups.name,
            cooling=cooling,
            series=_SERIES["rack"],
            thresholds=chart_thresholds("rack"),
            history=self._history_rows(rack.id),
            alerts=rack_alerts,
        )

    def site_card(self, site_id: str) -> SiteCard | None:
        site = self.sites_by_id.get(site_id)
        if site is None:
            return None
        return self._site_card(site)

    def _summary(self) -> Summary:
        rack_metrics = [self.latest[rack.id] for rack in self.racks if rack.id in self.latest]
        it_load = round(sum(metrics["power_kw"] for metrics in rack_metrics), 2)
        ups_output = round(
            sum(self.latest[device.id]["output_kw"] for device in self.devices if device.kind == "ups"),
            2,
        )
        critical = sum(1 for alert in self.alerts if alert.severity.value == "critical")
        warning = sum(1 for alert in self.alerts if alert.severity.value == "warning")
        hottest = max(self.racks, key=lambda rack: self.latest[rack.id]["inlet_temp_c"])
        ups_devices = [device for device in self.devices if device.kind == "ups"]
        shortest = min(ups_devices, key=lambda device: self.latest[device.id]["runtime_minutes"])
        return Summary(
            site_count=len(self.sites),
            room_count=len(self.rooms),
            rack_count=len(self.racks),
            device_count=len(self.devices),
            it_load_kw=it_load,
            ups_output_kw=ups_output,
            active_alerts=len(self.alerts),
            critical_alerts=critical,
            warning_alerts=warning,
            hottest_rack_id=hottest.id,
            hottest_rack_name=hottest.name,
            hottest_site_name=self.sites_by_id[hottest.site_id].name,
            hottest_inlet_c=self.latest[hottest.id]["inlet_temp_c"],
            shortest_runtime_device_id=shortest.id,
            shortest_runtime_name=shortest.name,
            shortest_runtime_min=self.latest[shortest.id]["runtime_minutes"],
        )

    def _site_card(self, site: Site) -> SiteCard:
        site_alerts = [alert for alert in self.alerts if alert.site_id == site.id]
        rooms = [self._room_card(room) for room in self.rooms if room.site_id == site.id]
        return SiteCard(
            id=site.id,
            name=site.name,
            location=site.location,
            climate_note=site.climate_note,
            active_alerts=len(site_alerts),
            critical_alerts=sum(1 for alert in site_alerts if alert.severity.value == "critical"),
            rooms=rooms,
        )

    def _room_card(self, room: Room) -> RoomCard:
        devices = [device for device in self.devices if device.room_id == room.id]
        racks = [rack for rack in self.racks if rack.room_id == room.id]
        return RoomCard(
            id=room.id,
            name=room.name,
            role=room.role,
            ups=[self._ups_card(device) for device in devices if device.kind == "ups"],
            cooling=[self._cooling_card(device) for device in devices if device.kind == "cooling"],
            pdus=[self._pdu_card(device) for device in devices if device.kind == "pdu"],
            racks=[self._rack_card(rack) for rack in racks],
        )

    def _ups_card(self, device: Device) -> UpsCard:
        metrics = self.latest[device.id]
        if device.rated_kw is None:
            raise ValueError(f"UPS {device.id} is missing a nameplate")
        return UpsCard(
            id=device.id,
            name=device.name,
            rated_kw=device.rated_kw,
            load_pct=metrics["load_pct"],
            output_kw=metrics["output_kw"],
            battery_health_pct=metrics["battery_health_pct"],
            runtime_minutes=metrics["runtime_minutes"],
            input_voltage_v=metrics["input_voltage_v"],
            output_voltage_v=metrics["output_voltage_v"],
            status=self.status_for(device.id),
        )

    def _cooling_card(self, device: Device) -> CoolingCard:
        metrics = self.latest[device.id]
        if device.cooling_subtype is None or device.setpoint_c is None:
            raise ValueError(f"Cooling unit {device.id} is missing subtype or setpoint")
        return CoolingCard(
            id=device.id,
            name=device.name,
            subtype=device.cooling_subtype,
            supply_temp_c=metrics["supply_temp_c"],
            return_temp_c=metrics["return_temp_c"],
            fan_speed_pct=metrics["fan_speed_pct"],
            setpoint_c=device.setpoint_c,
            status=self.status_for(device.id),
        )

    def _pdu_card(self, device: Device) -> PduCard:
        metrics = self.latest[device.id]
        if device.rack_id is None or device.fed_by is None:
            raise ValueError(f"PDU {device.id} is missing rack or upstream UPS")
        return PduCard(
            id=device.id,
            name=device.name,
            rack_id=device.rack_id,
            fed_by=device.fed_by,
            total_kw=metrics["total_kw"],
            max_outlet_kw=metrics["max_outlet_kw"],
            outlets=list(self.latest_outlets.get(device.id, [])),
            status=self.status_for(device.id),
        )

    def _rack_card(self, rack: Rack) -> RackCard:
        metrics = self.latest[rack.id]
        pdu = self.pdu_for_rack(rack.id)
        return RackCard(
            id=rack.id,
            name=rack.name,
            inlet_temp_c=metrics["inlet_temp_c"],
            power_kw=metrics["power_kw"],
            pdu_id=pdu.id,
            max_outlet_kw=self.latest[pdu.id]["max_outlet_kw"],
            status=self.status_for(rack.id, pdu.id),
        )

    def status_for(self, *subject_ids: str) -> str:
        current = "ok"
        for alert in self.alerts:
            if alert.subject_id in subject_ids and _STATUS_RANK[alert.severity.value] > _STATUS_RANK[current]:
                current = alert.severity.value
        return current

    def _history_rows(self, subject_id: str) -> list[HistoryRow]:
        rows: list[HistoryRow] = []
        for point in self.history[subject_id]:
            rows.append(HistoryRow(ts=point.ts, **point.metrics))
        return rows
