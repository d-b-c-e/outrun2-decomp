# Rival drift score source qualification — November 2, 2026

ROADMAP item 7 / upstream issue 3 reports a rival drift score staying at zero. The current source contains the producer/consumer chain below. A bounded fixture passes it; no defect or natural-mission reproduction was established, so AI, scoring rules and defaults were not changed.

| Step | Existing native consumer |
|---|---|
| `474D30` -> `474990` -> `474CDB` | Steering trim or sufficient absolute body yaw sets car `+0C` bit `0x100000`; straightening clears it. The fixture runs the real trim entry with verified EXE constants, zero trim and chosen yaw. |
| `477090` | A linked racer reads its car through `799B38 + carIndex * 0x3C`, follows course progress and copies car `+0C` bit 20 to racer `+71` bit 1. It also queries mission selection and the fast-score condition (racer bit 3). |
| `477B60`, called by `496A30` | For mission type 2, selected mission, activity bit 1 set, and racer `+6C == 0`, adds 10 per tick to racer `+98`, or 20 with the fast-score bit. Type 3 uses activity bit 0 instead. Inactive/stopped/other-activity cases do not gain drift score. |

`tests/test_rival_drift_score.cpp` executes these actual native functions together over an inert car and the same racer record. Course-progress, selected-mission and fast-score service answers are controlled boundaries; there is no AI/controller/scheduler or natural mission run. The test covers straight, positive/negative yaw, fast scoring, drift end, stopped/inactive guards, another mission type, matrix-stack balance and corrupt-count refusal. **28 checks pass**; CTest `rival_drift_score|c2c_goal_sequence` **2/2 pass**, named Release desktop builds. The original `477090` bridge and `477B60` consumer are exposed through the existing oracle/parts interfaces; production control calls the same consumer. No score math was changed. Set `OR2_EXE` to the verified game cache; absent data skips with code 77. Private logs: `work/validation-20261102/rival-drift-{build,chain,ctest}.log`.

Remaining diagnosis needs a selected type-2 mission's actual state: mode `78026C`, selection `836374`, record type, racer count, linked car index, car `+0C/+162/+C2C`, racer `+71/+6C/+98` before and after traffic and mission control, and any first missing/faulted PC. An owner-attended muted disposable run can collect those values for at most 600 race ticks, alongside the visible score and a deliberate left/right drift. This separates absent drift production, a missing racer/car link, an inactive/stopped mission and a HUD readout issue. Neither this fixture nor a passing score increment qualifies a visible rival drift or closes issue 3. No game, GPU, device, owner-state write or rig interval was used.

The independent [native-wrapper projection fix](MISSION-MARKER-PROJECTION.md) supplies the previously missing chase-marker matrix stack. Its27 actual-wrapper checks include repeated type-2 player/rival increments, but not natural drift production or HUD pixels; issue3 remains open.

The [bounded opt-in trace](MISSION-TRACE.md) now records the actual controller's before/after score/racer/selection state for up to600 eligible calls. It does not replace linked-car/AI or HUD observation.
