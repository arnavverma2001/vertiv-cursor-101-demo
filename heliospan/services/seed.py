"""Static topology for the two-site demo fleet.

UPS load is not a free-floating random walk. Each tick the simulator sets it
from the PDUs that UPS feeds, plus a small auxiliary draw, so the nameplate
percentage and the rack meters stay in the same conversation.

Row-level UPS modules (about 20–60 kW) keep the fleet small enough to read
on a laptop while the percentages still look like a real room.
"""

from heliospan.models.domain import CoolingSubtype, Device, Rack, Room, Site

OutletBaselines = dict[str, list[float]]
MetricBaselines = dict[str, dict[str, float]]


def build_sites() -> list[Site]:
    return [
        Site(
            id="ashford",
            name="Ashford Campus",
            location="Columbus, Ohio",
            climate_note="Temperate. Mechanical plant is on the roof.",
        ),
        Site(
            id="meridian",
            name="Meridian Hall",
            location="Phoenix, Arizona",
            climate_note="Hot-dry. Cooling units carry a higher base load.",
        ),
    ]


def build_rooms() -> list[Room]:
    return [
        Room(id="hall-a", site_id="ashford", name="Hall A", role="Compute"),
        Room(id="hall-b", site_id="ashford", name="Hall B", role="Network and storage"),
        Room(id="room-1", site_id="meridian", name="Room 1", role="Compute"),
        Room(id="room-2", site_id="meridian", name="Room 2", role="High-density compute"),
    ]


def build_racks() -> list[Rack]:
    rows = [
        ("a01", "ashford", "hall-a", "A01"),
        ("a02", "ashford", "hall-a", "A02"),
        ("a03", "ashford", "hall-a", "A03"),
        ("b01", "ashford", "hall-b", "B01"),
        ("b02", "ashford", "hall-b", "B02"),
        ("m01", "meridian", "room-1", "M01"),
        ("m02", "meridian", "room-1", "M02"),
        ("m03", "meridian", "room-1", "M03"),
        ("h01", "meridian", "room-2", "H01"),
        ("h02", "meridian", "room-2", "H02"),
    ]
    return [Rack(id=rid, site_id=site, room_id=room, name=name) for rid, site, room, name in rows]


def build_devices() -> list[Device]:
    ups = [
        _ups("ash-ups-a", "UPS-A", "ashford", "hall-a", rated_kw=36, auxiliary_kw=1.2, runtime=12),
        _ups("ash-ups-b", "UPS-B", "ashford", "hall-b", rated_kw=20, auxiliary_kw=0.4, runtime=15),
        _ups("mer-ups-1", "UPS-1", "meridian", "room-1", rated_kw=55, auxiliary_kw=1.0, runtime=10),
        _ups("mer-ups-2", "UPS-2", "meridian", "room-2", rated_kw=62, auxiliary_kw=1.4, runtime=14),
    ]
    cooling = [
        _cooling("ash-crac-a", "CRAC-A", "crac", "ashford", "hall-a", setpoint_c=18.0),
        _cooling("ash-crah-b", "CRAH-B", "crah", "ashford", "hall-b", setpoint_c=18.0),
        _cooling("mer-crac-1", "CRAC-1", "crac", "meridian", "room-1", setpoint_c=18.0),
        _cooling("mer-crac-2", "CRAC-2", "crac", "meridian", "room-2", setpoint_c=18.0),
    ]
    pdus = [
        _pdu("pdu-a01", "PDU-A01", "a01", "ash-ups-a", "ashford", "hall-a"),
        _pdu("pdu-a02", "PDU-A02", "a02", "ash-ups-a", "ashford", "hall-a"),
        _pdu("pdu-a03", "PDU-A03", "a03", "ash-ups-a", "ashford", "hall-a"),
        _pdu("pdu-b01", "PDU-B01", "b01", "ash-ups-b", "ashford", "hall-b"),
        _pdu("pdu-b02", "PDU-B02", "b02", "ash-ups-b", "ashford", "hall-b"),
        _pdu("pdu-m01", "PDU-M01", "m01", "mer-ups-1", "meridian", "room-1"),
        _pdu("pdu-m02", "PDU-M02", "m02", "mer-ups-1", "meridian", "room-1"),
        _pdu("pdu-m03", "PDU-M03", "m03", "mer-ups-1", "meridian", "room-1"),
        _pdu("pdu-h01", "PDU-H01", "h01", "mer-ups-2", "meridian", "room-2"),
        _pdu("pdu-h02", "PDU-H02", "h02", "mer-ups-2", "meridian", "room-2"),
    ]
    return ups + cooling + pdus


def metric_baselines() -> MetricBaselines:
    """Starting points the simulator jitters. Not the live reading."""

    return {
        "ash-ups-a": {"battery_health_pct": 98.6, "input_voltage_v": 480.0, "output_voltage_v": 208.0},
        "ash-ups-b": {"battery_health_pct": 99.1, "input_voltage_v": 481.0, "output_voltage_v": 207.6},
        "mer-ups-1": {"battery_health_pct": 46.5, "input_voltage_v": 479.0, "output_voltage_v": 208.4},
        "mer-ups-2": {"battery_health_pct": 97.4, "input_voltage_v": 478.5, "output_voltage_v": 207.2},
        "ash-crac-a": {"supply_temp_c": 18.3, "return_temp_c": 28.0, "fan_speed_pct": 55.0},
        "ash-crah-b": {"supply_temp_c": 17.7, "return_temp_c": 25.5, "fan_speed_pct": 41.0},
        "mer-crac-1": {"supply_temp_c": 19.0, "return_temp_c": 30.2, "fan_speed_pct": 67.0},
        "mer-crac-2": {"supply_temp_c": 22.4, "return_temp_c": 33.6, "fan_speed_pct": 93.0},
        "a01": {"inlet_temp_c": 23.2},
        "a02": {"inlet_temp_c": 23.8},
        "a03": {"inlet_temp_c": 22.6},
        "b01": {"inlet_temp_c": 21.4},
        "b02": {"inlet_temp_c": 21.1},
        "m01": {"inlet_temp_c": 24.2},
        "m02": {"inlet_temp_c": 24.8},
        "m03": {"inlet_temp_c": 24.0},
        "h01": {"inlet_temp_c": 33.0},
        "h02": {"inlet_temp_c": 28.4},
    }


def outlet_baselines() -> OutletBaselines:
    """Per-outlet kW. PDU-H02 outlet 4 sits above the outlet power rule."""

    return {
        "pdu-a01": [2.10, 1.90, 1.70, 1.50, 1.30, 1.00],
        "pdu-a02": [2.20, 2.00, 1.80, 1.50, 1.20, 0.80],
        "pdu-a03": [2.00, 1.80, 1.70, 1.60, 1.40, 1.00],
        "pdu-b01": [1.00, 0.80, 0.60, 0.50, 0.40, 0.30],
        "pdu-b02": [0.90, 0.70, 0.50, 0.40, 0.30, 0.20],
        "pdu-m01": [2.00, 1.80, 1.60, 1.40, 1.20, 0.80],
        "pdu-m02": [2.40, 2.20, 2.00, 1.60, 1.40, 1.00],
        "pdu-m03": [2.10, 1.90, 1.70, 1.50, 1.20, 0.90],
        "pdu-h01": [3.40, 3.30, 3.20, 3.20, 3.10, 3.00, 2.90, 2.80],
        "pdu-h02": [3.50, 3.40, 3.30, 6.55, 3.20, 3.10, 3.00, 2.80],
    }


def _ups(
    device_id: str,
    name: str,
    site_id: str,
    room_id: str,
    *,
    rated_kw: float,
    auxiliary_kw: float,
    runtime: float,
) -> Device:
    return Device(
        id=device_id,
        name=name,
        kind="ups",
        site_id=site_id,
        room_id=room_id,
        rated_kw=rated_kw,
        auxiliary_kw=auxiliary_kw,
        full_load_runtime_min=runtime,
    )


def _cooling(
    device_id: str,
    name: str,
    subtype: CoolingSubtype,
    site_id: str,
    room_id: str,
    *,
    setpoint_c: float,
) -> Device:
    return Device(
        id=device_id,
        name=name,
        kind="cooling",
        site_id=site_id,
        room_id=room_id,
        cooling_subtype=subtype,
        setpoint_c=setpoint_c,
    )


def _pdu(
    device_id: str,
    name: str,
    rack_id: str,
    ups_id: str,
    site_id: str,
    room_id: str,
) -> Device:
    return Device(
        id=device_id,
        name=name,
        kind="pdu",
        site_id=site_id,
        room_id=room_id,
        rack_id=rack_id,
        fed_by=ups_id,
    )
