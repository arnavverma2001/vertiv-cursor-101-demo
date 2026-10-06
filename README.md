# HelioSpan Monitor

HelioSpan Monitor is a facility operations console for critical power and computer-room cooling. It watches a small fleet of UPS modules, rack PDUs, and CRAC/CRAH units, raises threshold alerts, and keeps a short history of each reading.

The fleet is fictional: Ashford Campus in Columbus and Meridian Hall in Phoenix. Nothing here talks to a live building, and the product name is not a Vertiv brand. This repository is the codebase for a Cursor 101 workshop with firmware engineers who work on data center power and thermal systems.

## Architecture

```
browser  web/
   |  polls /api/fleet, /api/alerts, /api/rules, /api/devices, /api/racks
C++ server  src/server.cpp  (cpp-httplib + nlohmann/json)
   |
FleetStore  src/store.cpp
   |  every 2 seconds
   |- src/simulator.cpp   writes the next reading
   |- src/rules.cpp       evaluates thresholds and keeps alerts open
   \- src/seed.cpp        two sites, four rooms, ten racks
```

The service is C++17. Headers live in `include/heliospan/`. Readings stay in memory. Restarting the process reseeds the fleet and clears alert age. There is no database, message bus, or API key.

UPS load percent is derived from the PDUs on that module plus a small auxiliary draw, then turned into output kW from the nameplate. Battery runtime is estimated from health and load in `src/power.cpp`. Rack inlet temperature is its own series.

Alert evaluation is a pure function of the latest readings. `reconcile_alerts` keeps `opened_at` stable while a condition remains true. Each metric emits at most one severity: critical suppresses the warning for that same rule.

Device kinds today are `ups`, `pdu`, and `cooling` (`crac` or `crah`). Racks are subjects too, because inlet temperature belongs to the cabinet. A new kind has to be handled in the seed, the simulator, the snapshot, and the rule engine.

## Run it

Requires a C++17 compiler (Xcode clang or g++) and CMake 3.16 or newer. The first configure downloads cpp-httplib, nlohmann/json, and GoogleTest. Nothing else is installed on the system.

```bash
./scripts/run.sh
```

Then open http://127.0.0.1:8000. The same process serves the API and the UI.

C++ edits need a rebuild. `./scripts/run.sh` configures and builds before it listens. UI edits are uncached; refresh the browser. Restarting the process resets in-memory alert age.

## Tests

```bash
./scripts/test.sh
```

That builds the Debug tree and runs CTest. The suite covers the rule engine (bands, exact thresholds, alert identity), the telemetry bands, and the HTTP routes. Route tests start the server with the background sampler paused, then tick the store themselves.

## Layout

| Path | What it is |
| --- | --- |
| `include/heliospan/domain.hpp` | Sites, devices, readings, alerts |
| `include/heliospan/rules.hpp` | Threshold constants and the rule API |
| `src/seed.cpp` | The two-site fleet |
| `src/simulator.cpp` | Per-tick telemetry |
| `src/rules.cpp` | Threshold evaluation and the rule catalog |
| `src/power.cpp` | Runtime estimate |
| `src/store.cpp` | History, snapshots, device and rack detail |
| `src/server.cpp` | JSON routes and static files |
| `web/` | The operations console (no frontend build step) |
| `docs/feature-specs/` | Specs that are not implemented yet |
| `DEMO.md` | Cursor 101 walkthrough for this repo |

## Workshop note

`DEMO.md` is the field-engineer script. `docs/feature-specs/liquid-cooling-cdu.md` is the unimplemented feature used for Plan Mode. Cursor project rules live in `.cursor/rules/`.
