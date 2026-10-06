# Cursor 101 demo script

Use this with [HelioSpan Monitor](README.md) running at http://127.0.0.1:8000. The room is looking at a power and cooling console, not a tutorial app. Keep this file on your own screen. It is the only place the intentional rough edges are written down.

Suggested pace: questions, a small edit, an agent task, Plan Mode, then a pull request.

## 0. Start

```bash
./scripts/run.sh
```

Open the console, then http://127.0.0.1:8000/docs if you want to show FastAPI's generated API. Refresh the browser after static edits. Python edits reload Uvicorn and reset in-memory alert age.

Leave the process running. About once a minute, UPS-B in Hall B takes a short input-voltage sag so the room can watch an alert open and clear on its own.

## 1. Codebase questions

Paste these into chat one at a time. Let the answer land before the next.

1. How are alerts triggered?
2. How is UPS runtime estimated, and how does UPS load relate to the rack PDUs?
3. Where is the fleet seeded, and which devices are supposed to be unhealthy when the process starts?
4. How does the web UI stay current?

Then the one that sets up the edit:

5. UPS-A on Ashford Hall A sits around 82% load and the number is amber, but its status pill stays Normal and there is no load alert. UPS-2 at Meridian does alert. Why?

## 2. Rough edges (only in this script)

Do not mention these until you want the room to fix them.

**Load threshold.** `UPS_LOAD_WARNING_PCT` in `heliospan/services/rules.py` is `85.0`. The alerts page lede and `TONE.loadWarningPct` in `heliospan/static/app.js` both say 80. Hall A load is seeded to stay between those two numbers, so the tile looks hot and the rule stays quiet. Meridian UPS-2 sits near 89%, so that alert does fire. The comparator is strict `>` ("above"), which matches the word in the lede. Tests compare against the constant, not the literal 85, so changing the constant to `80.0` keeps pytest green. After the reload, UPS-A should appear under Alerts.

**Alert order in the console.** `orderedAlerts` in `heliospan/static/app.js` sorts by device name. `GET /api/alerts` already returns critical first, and `tests/test_api.py` locks that in. On the Alerts page, CRAC-2 warnings sit above the UPS-1 battery critical. The fix is in the browser sort, not the API.

## 3. Tab and inline edit

**Inline edit.** Open `heliospan/services/rules.py`. Change `UPS_LOAD_WARNING_PCT` from `85.0` to `80.0`. Save, let Uvicorn reload, and show UPS-A join the alert list. Run `./scripts/test.sh` so the room sees the suite stay green.

**Tab.** In `heliospan/static/app.js`, put the cursor inside `inletTone` and start a cold-aisle branch:

```javascript
function inletTone(value) {
  if (value < 18) return "warning";
  return above(value, TONE.inletWarningC, TONE.inletCriticalC);
}
```

Type `if (value < 18)` and let Tab complete the return. This is display-only until the agent task below adds the real rule. A second Tab spot is `rule_catalog` in `rules.py`: the entries are the same shape, so a new `RuleInfo` completes from the previous one.

## 4. Agent task

Ask for this and nothing else, so the agent does not also "clean up" unrelated copy:

> Add a warning when rack inlet temperature drops below 18°C (cold aisle, condensation risk). Follow the existing rack inlet rule: constant, evaluate path, rule catalog entry, and a pytest for just-below and exactly-at the threshold. In `heliospan/static/app.js`, teach `inletTone` about that low bound. Do not change `UPS_LOAD_WARNING_PCT` or the alerts-page sort.

Review the diff with the room. The rule engine, catalog, UI tone, and tests should each have moved, which is the point.

Optional follow-up, if you want a second small agent moment:

> On the Alerts page, list critical alerts before warnings. The API already does this. `orderedAlerts` in `app.js` sorts by name and undoes it. Don't change the API.

## 5. Plan Mode

Do not ask the agent to implement this during the talk. Switch to Plan Mode and point it at the spec:

> Read `docs/feature-specs/liquid-cooling-cdu.md` and write an implementation plan for HelioSpan Monitor. Name the files you would touch across models, seed data, the simulator, the rule engine, the API snapshot, the UI, and tests. Call out anything the spec leaves open. Do not write code.

A good plan mentions Meridian Room 2 (racks H01 and H02, CRAC-2 already hot), a new `cdu` device kind rather than a cooling subtype, leak severity, and tests that inject readings instead of waiting on the sampler.

If the plan is thin, ask it to expand the alert rules and the room-panel UI only.

## 6. Pull request and Bugbot

Ship the threshold edit, the cold-aisle rule, or both, as a real branch. Open a pull request against `main` and let Bugbot review it.

What you want the room to notice:

- The diff crosses `rules.py`, `app.js`, and `tests/`, which is the right shape for this codebase.
- Bugbot should be able to comment on naming, the boundary test, and whether critical still suppresses warning.
- The alphabetical alert sort is easy to miss in a Python-only review. If Bugbot skips it, use the follow-up prompt above and push another commit.

`./scripts/test.sh` should be green before you ask for review.
