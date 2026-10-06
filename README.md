# HelioSpan Monitor

HelioSpan Monitor is a facility operations console for critical power and computer-room cooling. It watches a small fleet of UPS modules, rack PDUs, and CRAC/CRAH units, raises threshold alerts, and keeps a short history of each reading.

The fleet is fictional: Ashford Campus in Columbus and Meridian Hall in Phoenix. Nothing here talks to a live building, and the product name is not a Vertiv brand. This repository is the codebase for a Cursor 101 workshop with engineers who work on data center power and thermal systems.

## Architecture

```
browser  heliospan/static
   |  polls /api/fleet, /api/alerts, /api/rules, /api/devices, /api/racks
FastAPI  heliospan/api/routes.py
   |
FleetStore  heliospan/services/store.py
   |  every 2 seconds
   |- simulator.py   writes the next reading
   |- rules.py       evaluates thresholds and keeps alerts open
   \- seed.py        two sites, four rooms, ten racks
```

Readings stay in memory. Restarting the process reseeds the fleet and clears alert age. There is no database, message bus, or API key.

UPS load percent is derived from the PDUs on that module plus a small auxiliary draw, then turned into output kW from the nameplate. Battery runtime is estimated from health and load in `heliospan/services/power.py`. Rack inlet temperature is its own series.

Alert evaluation is a pure function of the latest readings. `reconcile_alerts` keeps `opened_at` stable while a condition remains true. Each metric emits at most one severity: critical suppresses the warning for that same rule.

Device kinds today are `ups`, `pdu`, and `cooling` (`crac` or `crah`). Racks are subjects too, because inlet temperature belongs to the cabinet. A new kind has to be handled in the seed, the simulator, the snapshot, and the rule engine.

## Run it

Requires Python 3.11 or newer.

```bash
./scripts/run.sh
```

Then open http://127.0.0.1:8000. The same process serves the API and the UI. Interactive API docs are at http://127.0.0.1:8000/docs.

The script creates `.venv` on first run, installs `requirements.txt`, and starts Uvicorn with reload limited to the `heliospan` package. Python edits restart the process (and the in-memory fleet). UI edits are uncached; refresh the browser.

Equivalent commands, if you prefer not to use the script:

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
uvicorn heliospan.main:app --reload --reload-dir heliospan --host 127.0.0.1 --port 8000
```

## Tests

```bash
./scripts/test.sh
```

Or, with the virtualenv already active: `pytest`.

The suite covers the rule engine (bands, exact thresholds, alert identity) and the main HTTP routes. Tests build the app with the background sampler paused, then tick the store themselves, so results do not depend on wall-clock timing.

## Layout

| Path | What it is |
| --- | --- |
| `heliospan/models/domain.py` | Sites, devices, readings, alerts |
| `heliospan/services/seed.py` | The two-site fleet |
| `heliospan/services/simulator.py` | Per-tick telemetry |
| `heliospan/services/rules.py` | Thresholds and the rule catalog |
| `heliospan/services/store.py` | History, snapshots, device and rack detail |
| `heliospan/api/routes.py` | JSON routes |
| `heliospan/static/` | The operations console (no build step) |
| `docs/feature-specs/` | Specs that are not implemented yet |
| `DEMO.md` | Cursor 101 walkthrough for this repo |

## Workshop note

`DEMO.md` is the field-engineer script. `docs/feature-specs/liquid-cooling-cdu.md` is the unimplemented feature used for Plan Mode. Cursor project rules live in `.cursor/rules/`.
