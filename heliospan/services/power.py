"""Small electrical helpers shared by the simulator.

Runtime here is an estimate for the demo, not a battery-discharge model.
At constant battery health, runtime scales inversely with load: a unit at
half load runs about twice as long as it would at full nameplate load.
"""


def estimate_runtime_minutes(
    *,
    battery_health_pct: float,
    load_pct: float,
    full_load_runtime_min: float,
) -> float:
    """Minutes of support remaining at the current load and battery health."""

    safe_load = max(load_pct, 1.0)
    health = max(battery_health_pct, 0.0) / 100.0
    return full_load_runtime_min * health * (100.0 / safe_load)
