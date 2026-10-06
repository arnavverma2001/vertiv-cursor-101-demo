"""Simulator stays inside the seeded operating bands for a short run."""

from datetime import datetime, timezone

from heliospan.config import HISTORY_LIMIT
from heliospan.services.power import estimate_runtime_minutes
from heliospan.services.store import FleetStore


def test_runtime_scales_inversely_with_load():
    full = estimate_runtime_minutes(battery_health_pct=100, load_pct=100, full_load_runtime_min=10)
    half = estimate_runtime_minutes(battery_health_pct=100, load_pct=50, full_load_runtime_min=10)
    worn = estimate_runtime_minutes(battery_health_pct=50, load_pct=50, full_load_runtime_min=10)
    assert full == 10
    assert half == 20
    assert worn == 10


def test_many_ticks_stay_in_the_seeded_bands():
    store = FleetStore.seed()
    now = datetime(2026, 10, 6, 15, 0, tzinfo=timezone.utc)
    input_voltages: list[float] = []
    for _ in range(70):
        store.tick(now)
        ash_load = store.latest["ash-ups-a"]["load_pct"]
        mer_load = store.latest["mer-ups-2"]["load_pct"]
        h01 = store.latest["h01"]["inlet_temp_c"]
        h02 = store.latest["h02"]["inlet_temp_c"]
        battery = store.latest["mer-ups-1"]["battery_health_pct"]
        assert 80.5 < ash_load < 84.5
        assert 86.0 < mer_load < 94.0
        assert h01 > 32.0
        assert 27.5 < h02 < 31.5
        assert battery < 50.0
        input_voltages.append(store.latest["ash-ups-b"]["input_voltage_v"])
        assert store.latest["pdu-h02"]["max_outlet_kw"] > 6.0

    assert min(input_voltages) < 450
    assert max(input_voltages) > 470
    assert len(store.history["ash-ups-a"]) == HISTORY_LIMIT
