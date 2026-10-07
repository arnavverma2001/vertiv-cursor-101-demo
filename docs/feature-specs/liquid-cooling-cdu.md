# Liquid cooling: CDU monitoring and leak detection

Status: not started. This is the next operations feature for HelioSpan Monitor. Do not treat this document as an implementation.

## Problem

Meridian Hall Room 2 is the high-density pod. Racks H01 and H02 already run far hotter and harder than the rest of the fleet: H01 inlet sits past the ASHRAE A1 allowable limit, H02 is above the recommended 27°C band, PDU-H02 has a branch over the outlet warning, and CRAC-2 is missing its supply setpoint with the fan held high.

Those signals are all air-side or electrical. They cannot tell an operator whether coolant is actually moving, whether the coolant distribution unit (CDU) supply temperature is in band, or whether a leak sensor has gone wet. Before Room 2 can be operated as a liquid-cooled pod, the console has to show the CDU next to the CRAC, with leak detection that pages as critical.

Ashford and Meridian Room 1 stay on air. This feature does not convert them.

## Goals

- One CDU per high-density room, modeled like the other devices: seeded, simulated, ruled, returned by the existing device API, and drawn in the console.
- Leak detection is visible on the room and on the device, not only as a row buried in the alert list.
- Operators can trend coolant supply temperature, return temperature, flow, differential pressure, and pump speed the same way they trend UPS load or CRAC supply air.

## Non-goals

- Changing setpoints, opening valves, or any other write path. The API stays read-only.
- Per-server cold plates, manifolds per rack, or a floor plan.
- Paging, email, or webhooks. An alert in the existing alert list is enough.
- Reworking the air-cooling rules or the UPS load threshold.
- A second product name, logo, or screen. This is another device kind inside HelioSpan Monitor.

## Users

- The floor operator, who needs to know if Room 2 is safe to leave in liquid mode during a shift.
- The thermal engineer, who compares CDU flow and temperatures with rack inlet and CRAC behavior.
- The on-call facilities engineer, who needs a critical leak to be unmistakable.

## User stories

1. As a floor operator, I see the Room 2 CDU on the fleet and site views, with flow, supply and return coolant temperature, pump speed, and a leak state, so I do not have to infer liquid health from CRAC-2.
2. As a floor operator, a wet leak sensor or a wet secondary drip tray opens a critical alert immediately and stays critical until the sensor reads dry again.
3. As a thermal engineer, I open the CDU and chart flow, temperatures, differential pressure, and pump speed from the same history the other devices use.
4. As a thermal engineer, I get a warning when flow falls below the room's required flow, and a critical when it falls below 70% of that requirement.
5. As an on-call engineer, high coolant supply temperature and out-of-band differential pressure show up as alerts with the same shape as today's rules (value, threshold, severity, room).
6. As an operator comparing rooms, Room 1 has a healthy CDU so Room 2's stress is obvious rather than "the only CDU we have."

## Recommended defaults for open questions

- Alerts clear on their own when the reading recovers, same as every current rule. No acknowledgement workflow in this version.
- One CDU serves the whole room. Per-rack flow is out of scope.
- Leak inputs are two discrete states on the CDU: primary sensor and secondary tray, each `dry` or `wet`. Either `wet` is critical. Do not invent a third "unknown" state in this version; a missing reading is a simulator bug, not an alert type.

## Data model

Add device kind `cdu`. Do not hang this off `cooling_subtype`. CRAC and CRAH share air metrics. A CDU does not.

Suggested identity fields, alongside the fields every device already has:

| Field | Meaning |
| --- | --- |
| `rated_flow_lpm` | Nameplate flow the room was designed around |
| `required_flow_lpm` | Minimum flow operations will accept (at or below this starts the warning) |

Suggested metrics on each reading:

| Metric | Unit | Notes |
| --- | --- | --- |
| `coolant_supply_temp_c` | °C | Fluid leaving the CDU toward the racks |
| `coolant_return_temp_c` | °C | Fluid coming back |
| `flow_lpm` | L/min | Loop flow |
| `differential_pressure_kpa` | kPa | Across the CDU |
| `pump_speed_pct` | % | 0–100 |
| `leak_primary` | `dry` or `wet` | Not a float. Keep it beside the numeric metrics in a way the rule engine can read. |
| `leak_secondary` | `dry` or `wet` | Drip tray |

Seed two units:

- **CDU-1** on Meridian Room 1. Healthy. Flow comfortably above required, temperatures steady, both sensors dry. This is the comparison unit.
- **CDU-2** on Meridian Room 2. Flow sits in the warning band (below required, above 70% of required). Supply temperature may sit near the warning line but should not be critical at startup. Both sensors dry at tick 0. A later tick drives the primary sensor wet for a short, deterministic window, then returns it to dry, so a live console shows a critical leak open and clear. Mirror the way UPS-B's input sag is scheduled from the tick counter.

Product thresholds (training-product choices, not a citation of a particular liquid-cooling standard):

| Rule id | Warning | Critical |
| --- | --- | --- |
| `cdu-leak` | — | either sensor `wet` |
| `cdu-flow-low` | `flow_lpm` below `required_flow_lpm` | below 70% of `required_flow_lpm` |
| `cdu-supply-temp-high` | supply above 40°C | supply above 45°C |
| `cdu-pressure-out-of-band` | outside 40–120 kPa | outside 30–140 kPa |

Pump speed is charted and shown. It does not alert in this version.

Pick nameplate numbers so CDU-2's steady flow is inside the warning band with the same noise margin the air-side seed uses, and CDU-1 never crosses a threshold during a 70-tick run.

## Simulator

Extend the existing tick. Do not add a second loop or a new process.

- Numeric channels use the same wave-plus-noise helper as the other metrics.
- Leak state is not jittered. It is a deterministic schedule on CDU-2 only.
- Return temperature stays above supply temperature.
- Flow, pressure, and pump speed stay inside physical clamps so a bad random draw cannot draw a negative flow.

## Rules

Add a `cdu` branch next to the UPS, PDU, cooling, and rack branches. Unknown kinds should keep failing loudly.

- Leak is critical only. There is no warning severity for a wet sensor.
- Flow uses the device's `required_flow_lpm`, not a single global constant. The 70% critical factor can be a named constant.
- Supply temperature and pressure use the bands in the table above.
- One alert per rule per CDU. Critical suppresses warning on that same rule, matching the current engine.
- Catalog text is generated from those thresholds so the Alerts page rule reference stays accurate.
- Chart thresholds for the numeric series come from the same constants.

## API

No new service and no new base path.

- Fleet and site snapshots include the CDU on the room, the way they include the UPS and the CRAC.
- `GET /api/devices/{id}` returns the CDU with history, series, thresholds, and its open alerts.
- `GET /api/rules` includes the four rules.
- `GET /api/alerts` includes leak and flow alerts with `subject_kind` of `cdu`.

## UI

The device page is already metric-driven. Coolant series should chart once the API lists them. Two things still need a real panel, because a generic metric tile is not enough for a leak:

- On the Room 2 (and Room 1) card, a CDU strip: name, flow versus required, supply → return, pump speed, and a leak state that is obviously bad when wet.
- On the CDU device page, a leak banner above the chart when either sensor is wet. The banner names the sensor (primary or secondary tray) and links into the alert.

Tones for the new metrics belong with the other client tones. Do not hardcode a second, conflicting set of numbers in the lede.

Ashford's rooms gain nothing. An empty CDU section on an air room is noise. Omit it.

## Tests

Add cases beside the current rule tests. Inject readings. Do not sleep on the sampler.

- Primary wet, secondary dry → one critical `cdu-leak`, message names the primary sensor.
- Both dry → no leak alert.
- Flow just below required, and at 70% minus a hair, including mutual exclusion of warning and critical.
- Flow exactly at required → no flow alert (same strict comparison as the other "below" rules, unless you deliberately choose `<=` and test that).
- Supply temperature and pressure on each side of both bands.
- A healthy CDU reading produces no alerts.
- API: after one tick, `GET /api/devices` for CDU-2 returns `flow_lpm` and both leak fields, and the fleet snapshot for Meridian Room 2 includes that CDU.
- Simulator: across 70 ticks, CDU-1 never alerts, CDU-2's steady flow stays in the warning band, and the primary sensor is wet on at least one tick and dry on another.

## Where this touches the current code

Likely files, because each kind is dispatched explicitly today:

- `include/heliospan/domain.hpp` — device kind, CDU fields, room snapshot card
- `src/seed.cpp` — CDU-1 and CDU-2
- `src/simulator.cpp` — channels and the leak schedule
- `include/heliospan/rules.hpp` and `src/rules.cpp` — constants, branch, catalog, chart thresholds
- `src/store.cpp` and `src/json_codec.cpp` — room card, device detail, JSON field names
- `web/app.js` and `web/styles.css` — room strip, leak banner, tones
- `tests/test_rules.cpp`, `tests/test_api.cpp`, `tests/test_simulator.cpp`

## Constraints

- C++17, the current cpp-httplib process, no new services and no new credentials.
- In-memory only. History length stays the existing ring buffer.
- Do not change existing rule thresholds or the seeded behavior of UPS, PDU, CRAC, CRAH, or rack inlet tests.
- Keep JSON field names in snake_case.
- Operator-facing copy should read like the rest of the console.
- No new system packages. Stay with the libraries CMake already fetches.

## Acceptance check

A reviewer can start the app, open Meridian Hall, see CDU-1 healthy and CDU-2 on a flow warning, wait for the scheduled leak (or inspect the tick condition in the simulator and the tests), open CDU-2 and read the leak banner plus a flow chart, and run `./scripts/test.sh` green.
