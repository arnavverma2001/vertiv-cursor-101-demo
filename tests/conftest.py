"""Shared fixtures. The simulator loop stays off so tests are deterministic."""

from datetime import datetime, timezone

import pytest
from fastapi.testclient import TestClient

from heliospan.main import create_app
from heliospan.models.domain import Outlet, Reading
from heliospan.services.store import FleetStore


@pytest.fixture
def now() -> datetime:
    return datetime(2026, 10, 6, 15, 0, tzinfo=timezone.utc)


@pytest.fixture
def client():
    app = create_app(run_simulator=False)
    with TestClient(app) as test_client:
        yield test_client


@pytest.fixture
def store() -> FleetStore:
    fleet = FleetStore.seed()
    fleet.tick(datetime(2026, 10, 6, 15, 0, tzinfo=timezone.utc))
    return fleet


def reading(
    *,
    kind: str = "ups",
    subject_id: str = "ups-test",
    name: str = "UPS-Test",
    metrics: dict[str, float] | None = None,
    outlets: list[Outlet] | None = None,
) -> Reading:
    base = {
        "load_pct": 40.0,
        "battery_health_pct": 98.0,
        "runtime_minutes": 25.0,
        "input_voltage_v": 480.0,
        "output_voltage_v": 208.0,
        "output_kw": 20.0,
        "supply_temp_c": 18.2,
        "return_temp_c": 28.0,
        "fan_speed_pct": 50.0,
        "setpoint_c": 18.0,
        "inlet_temp_c": 23.0,
        "total_kw": 5.0,
        "max_outlet_kw": 1.5,
        "power_kw": 5.0,
    }
    if metrics:
        base.update(metrics)
    return Reading(
        subject_id=subject_id,
        subject_name=name,
        kind=kind,  # type: ignore[arg-type]
        site_id="ashford",
        site_name="Ashford Campus",
        room_id="hall-a",
        room_name="Hall A",
        metrics=base,
        outlets=outlets or [],
    )
