"""Telemetry simulator.

Each tick jitters baselines with a slow wave plus a little noise, then derives
the values that should not be sampled independently:

- PDU total kW is the sum of its outlets
- rack power is that PDU total
- UPS output and load percent follow the PDUs on that UPS, plus auxiliary draw
- battery runtime follows health and load

An input-voltage sag on UPS-B opens and clears on its own so the board is not
frozen. The sag is deterministic from the tick counter.
"""

from __future__ import annotations

import hashlib
import math
import random
from datetime import datetime
from typing import TYPE_CHECKING

from heliospan.models.domain import Outlet
from heliospan.services.power import estimate_runtime_minutes

if TYPE_CHECKING:
    from heliospan.services.store import FleetStore

_OUTLET_NOISE = 0.01
_OUTLET_DRIFT = 0.01


def simulate_tick(store: FleetStore, now: datetime) -> None:
    tick = store.tick_index
    for device in store.devices:
        if device.kind == "ups":
            continue
        if device.kind == "cooling":
            store.record(device.id, now, _cooling_metrics(store, device.id, tick))
        elif device.kind == "pdu":
            outlets = _outlets(store, device.id, tick)
            store.latest_outlets[device.id] = outlets
            powers = [outlet.kw for outlet in outlets]
            store.record(
                device.id,
                now,
                {
                    "total_kw": round(sum(powers), 2),
                    "max_outlet_kw": round(max(powers), 2) if powers else 0.0,
                },
            )
        else:
            raise ValueError(f"Unsupported device kind: {device.kind}")

    for rack in store.racks:
        pdu = store.pdu_for_rack(rack.id)
        outlets = store.latest_outlets[pdu.id]
        inlet = _vary(
            store.baselines[rack.id]["inlet_temp_c"],
            rack.id,
            "inlet_temp_c",
            tick,
            noise=0.05,
            drift=0.08,
            period=28,
        )
        store.record(
            rack.id,
            now,
            {
                "inlet_temp_c": round(_clamp(inlet, 12.0, 45.0), 1),
                "power_kw": round(sum(outlet.kw for outlet in outlets), 2),
            },
        )

    for device in store.devices:
        if device.kind != "ups":
            continue
        store.record(device.id, now, _ups_metrics(store, device.id, tick))


def _cooling_metrics(store: FleetStore, device_id: str, tick: int) -> dict[str, float]:
    device = store.devices_by_id[device_id]
    base = store.baselines[device_id]
    supply = _vary(base["supply_temp_c"], device_id, "supply_temp_c", tick, noise=0.06, drift=0.08, period=26)
    return_air = _vary(base["return_temp_c"], device_id, "return_temp_c", tick, noise=0.08, drift=0.12, period=30)
    fan = _vary(base["fan_speed_pct"], device_id, "fan_speed_pct", tick, noise=0.35, drift=0.4, period=18)
    supply = _clamp(supply, 12.0, 35.0)
    if return_air < supply + 4:
        return_air = supply + 4
    setpoint = device.setpoint_c if device.setpoint_c is not None else 18.0
    return {
        "supply_temp_c": round(supply, 1),
        "return_temp_c": round(return_air, 1),
        "fan_speed_pct": round(_clamp(fan, 0.0, 100.0), 1),
        "setpoint_c": setpoint,
    }


def _outlets(store: FleetStore, pdu_id: str, tick: int) -> list[Outlet]:
    outlets: list[Outlet] = []
    for index, baseline in enumerate(store.outlet_baselines[pdu_id], start=1):
        kw = _vary(
            baseline,
            pdu_id,
            f"outlet_{index}",
            tick,
            noise=_OUTLET_NOISE,
            drift=_OUTLET_DRIFT,
            period=24,
        )
        outlets.append(Outlet(id=str(index), label=str(index), kw=round(_clamp(kw, 0.0, 12.0), 2)))
    return outlets


def _ups_metrics(store: FleetStore, device_id: str, tick: int) -> dict[str, float]:
    device = store.devices_by_id[device_id]
    base = store.baselines[device_id]
    if device.rated_kw is None or device.full_load_runtime_min is None:
        raise ValueError(f"UPS {device_id} is missing nameplate data")

    it_kw = 0.0
    for pdu in store.pdus_for_ups(device_id):
        it_kw += store.latest[pdu.id]["total_kw"]
    output_kw = it_kw + device.auxiliary_kw
    load_pct = output_kw / device.rated_kw * 100.0
    load_pct += 0.35 * math.sin((tick / 22.0) * math.tau)
    load_pct = _clamp(load_pct, 0.0, 100.0)

    battery = _vary(
        base["battery_health_pct"],
        device_id,
        "battery_health_pct",
        tick,
        noise=0.08,
        drift=0.05,
        period=40,
    )
    battery = _clamp(battery, 0.0, 100.0)
    runtime = estimate_runtime_minutes(
        battery_health_pct=battery,
        load_pct=load_pct,
        full_load_runtime_min=device.full_load_runtime_min,
    )
    input_v = _vary(base["input_voltage_v"], device_id, "input_voltage_v", tick, noise=0.5, drift=0.4, period=20)
    output_v = _vary(
        base["output_voltage_v"],
        device_id,
        "output_voltage_v",
        tick,
        noise=0.25,
        drift=0.2,
        period=16,
    )
    # A few ticks of sag on the network-room UPS, then it recovers.
    if device_id == "ash-ups-b" and tick >= 30 and tick % 30 < 5:
        input_v = 438.0

    output_kw = device.rated_kw * load_pct / 100.0
    return {
        "load_pct": round(load_pct, 1),
        "battery_health_pct": round(battery, 1),
        "runtime_minutes": round(runtime, 1),
        "input_voltage_v": round(_clamp(input_v, 350.0, 560.0), 1),
        "output_voltage_v": round(_clamp(output_v, 180.0, 240.0), 1),
        "output_kw": round(output_kw, 2),
    }


def _vary(
    baseline: float,
    subject_id: str,
    metric: str,
    tick: int,
    *,
    noise: float,
    drift: float,
    period: float,
) -> float:
    phase = _phase(subject_id, metric)
    wave = math.sin((tick / period) * math.tau + phase) * drift
    jitter = random.Random(f"{subject_id}:{metric}:{tick}").uniform(-noise, noise)
    return baseline + wave + jitter


def _phase(subject_id: str, metric: str) -> float:
    digest = hashlib.sha256(f"{subject_id}:{metric}".encode()).digest()
    return int.from_bytes(digest[:2], "big") / 65535 * math.tau


def _clamp(value: float, low: float, high: float) -> float:
    return min(high, max(low, value))
