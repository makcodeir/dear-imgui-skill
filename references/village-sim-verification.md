# Village-Sim Verification Hardening (villageGame build, Sep 2026)

Context: 2D village sim (4 autonomous villagers: gather wood/stone/food, craft
axes/pickaxes with durability, build hut → family house → communal hall, eat,
rest, age). Engine in `village_sim.h` (pure, fixed 0.1 s tick, xorshift RNG,
FNV state hash), `selftest.cpp` (19 headless asserts), `main.cpp` (GLFW+OpenGL3
app with the UDS harness embedded). Verified with the full gauntlet: selftest →
render histogram → layout capture → frames grid → closed loop.

This file records what the closed loop caught that generic guidance does not.

## 1. Numeric motion proof (replaces eyeballing contact sheets)

Problem: a 4-frame contact sheet at ~460 px/tile shows villagers as 2-3 px
dots — the vision model judged the RUNNING sim "frozen, not even a pixel".
Counters at 0 were plausible at that sim age (first delivery takes ~20 sim-s),
so only positions could disprove "frozen" — and dots are unreadable.

Recipe:
- App: mirror one or two entity positions into stable statics, registered once:
  `Harness::VarFloat("vx", &g_h_vx);` refreshed every frame in a
  `sync_harness_mirrors()` helper (also day/wood/stone/food/meals/beds/axes).
- Driver: sample `get vx` / `get vy` twice N seconds apart at speed 5; assert
  the pair changed.
- Pixel gate: `compare -metric RMSE shotA.png shotB.png null:` — running pair
  > 0 (measured 0.0135 and 0.0220), paused pair **exactly 0** (bit-identical
  frames two seconds apart — the strongest freeze/pause assertion available,
  stronger than any overlay text or state line).

## 2. Coincident entities break nearest-wins hit tests

Two villagers targeting the same node end at the identical world position. A
strict `dd < best*best` click test then resolves by float round-trip noise
(w2s→s2w): selection flipped between runs (0 in one run, 2 in another) with
the SAME seed, same state, same registered click point.

Fix (canvas click hit-test):

```cpp
float best_sq = 14.0f * 14.0f;               // hit radius squared
for (size_t i = 0; i < villagers.size(); i++) {
    float dx = villagers[i].x - mw.x, dy = villagers[i].y - mw.y;
    float dd = dx * dx + dy * dy;
    // lowest index wins among visually coincident entities
    if (dd < best_sq - 1e-4f) { best_sq = dd; pick_v = (int)i; }
}
```

The epsilon (≈ 0.01 world units, squared) is far below one rendered pixel, so
it cannot change perceptible behaviour — it only removes the tie race. Apply
the same guard to hover tooltips.

Debugging note: the probes that resolved this were `register_dump` (rect
centers) plus a world-coordinate mirror for the villager — dump BOTH the rect
space and the world space at the failure moment before theorizing. Two
isolated probes (exact `clickat` at dumped coords, then `press villager0`) both
selected correctly, which is what isolated the coincidence case.

## 3. Overflow layout: fill-height item + appended panel

Center column built as:

```cpp
InvisibleButton("##canvas", GetContentRegionAvail());  // takes ALL height
draw_log(log_h);                                       // appended BELOW
```

Content = avail + log_h → the child silently overflows: a vertical scrollbar
appears and the log sits below the fold — zero errors, exit 0. The vision
model described both symptoms in the FIRST capture ("a vertical scrollbar on
the right side of the center canvas", "the Event Log is integrated as the
final section of the Right Dashboard") and the lead was dismissed as a
description quirk. It was a real layout bug.

Worse second-order effect: when the scrollbar state oscillates between frames,
`GetContentRegionAvail()` changes → the letterbox view rescales per frame →
harness-registered `ActionAt` points shift → closed-loop selection flakes with
no code change. One layout bug, three different symptoms.

Fix (explicit vertical split, Pitfall 16's vertical twin):

```cpp
float log_h   = ImGui::GetFrameHeightWithSpacing() * 4.0f + 10.0f;
float canvas_h = ImGui::GetContentRegionAvail().y - log_h
               - ImGui::GetStyle().ItemSpacing.y;
draw_canvas(&view, canvas_h);   // InvisibleButton((avail.x, canvas_h))
draw_log(log_h);                // content now == avail exactly
```

## 4. Shared-display hazards for driven loops

On a shared automation display (:1, sibling agent sessions active):
- Sibling keystrokes reach the focused window: the sim's Space=pause shortcut
  toggled mid-loop with no command sent (observed twice: `paused` flipped
  1→0 between captures).
- `xdotool windowactivate --sync` blocked ~13 s behind focus-stealing
  protection — visible as a frame-counter jump (1617→3452) with the app
  itself healthy.

Guards (bash around a drive.py driver):

```bash
cur() { python3 drive.py "$SOCK" get "$1" | sed "s/.*$1=//"; }
ensure_paused()  { [ "$(cur paused)" = "1" ] || { echo "(env flip: re-assert)"; \
                   python3 drive.py "$SOCK" set paused 1 >/dev/null; }; }
ensure_running() { [ "$(cur paused)" = "0" ] || { echo "(env flip: re-assert)"; \
                   python3 drive.py "$SOCK" set paused 0 >/dev/null; }; }
```

Call `ensure_*` before and after capture steps; use model `set` for state
setup and reserve `press` for steps where exercising the real hit-test IS the
point. Never make keyboard shortcuts a dependency of the loop's pass criteria.
Read frame-counter gaps as external blocking evidence, not app stalls.

## 5. Selftests vs steady state

`tool_breaks` failed at 18/19 while everything else passed: the economy had
reached steady state — stocks at reserves → `need_wood` false → nobody chops →
tool durability never drains → the break condition never fires. An assert on a
mechanic needs the world arranged so the mechanic actually fires:

```cpp
check(run_until(s, cond_dense_mined, 60000), "dense_deposit_mined");
s.stock_wood = 0; s.stock_stone = 0;   // drain reserves: force harvesting
check(run_until(s, cond_tool_broke, 80000), "tool_breaks");
```

General rule: steady-state economies lull mechanics to sleep; drain or spend
the relevant stock in-test before asserting wear/break/upgrade paths.

Related UI honesty: Step must advance enough sim-time to be observable in the
state readout (1 sim-sec wasn't; 5 is) and be labelled truthfully
("Step 5s") — Pitfall 15 applied to a verification button.

## Session ledger (approximate)

- Full gauntlet (selftest + histogram + layout capture + frames grid + closed
  loop) ≈ 6-8 min wall; one GUI build + closed-loop cycle ≈ 2-3 min.
- The flaky-selection hunt cost ~4 probe cycles; the two-space dump (rect +
  world coords) ended it in one. Dump both spaces first next time.
- New pitfalls fed back to SKILL.md: 36-40 above.
