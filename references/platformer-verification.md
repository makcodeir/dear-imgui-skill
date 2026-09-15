# Deterministic Game Sim + Replay + AI-Judge Verification
# (worked example: skills/imgui/platformer/, Mario-style platformer)

Patterns for building a game/simulation whose correctness an agent can verify
WITHOUT playing it: deterministic fixed-timestep sim, scripted input replay,
telemetry serialization, headless selftests, and an optional AI "judge"
endpoint. All field-tested in the platformer session (2026-09-12).

## 1. Determinism is the whole game

- Fixed timestep (`dt = 1/60f`) integrated in whole steps - never tie sim
  progress to render frames or wall-clock.
- Input as data, not events: `struct FrameInput { int frame; bool left, right,
  jump, run; }` and a replay runner `RunReplay(level, script, config) ->
  ReplayResult`. The replay runner is the headless core the GUI feeds live
  input into.
- Assert bit-identical replays in the selftest: run the same script twice,
  compare frame count, final position, score. One float non-determinism
  (unstable iteration order, reading wall clock in the sim) breaks every
  downstream verification.
- Outcomes are enums (`REACHED_GOAL`, `FALLEN_INTO_PIT`, `STOMPED_ENEMY`,
  `BLOCKED_BY_WALL`, ...), not strings scattered through the sim.

## 2. Headless selftest covers what the GUI cannot

Compile the engine sources with a plain `main()` (no GLFW/GL) and assert:
level integrity (spawn on solid ground, goal reachable), baseline script
reaches the goal (frame count + score), determinism (two replays identical),
idle stability (no input -> no drift), friction stop, telemetry JSON shape,
verdict-JSON extraction from noisy LLM text, and pathological paths (running
into a wall forever -> `BLOCKED_BY_WALL` events). The platformer run: 25
assertions, zero warnings under `-Wall -Wextra -Wpedantic -Wshadow`, C++20.

Pair this with the GUI gates (build_headless + vision loop): the selftest
proves the SIM, the screenshot proves the VIEW. Neither substitutes the other.

## 3. Telemetry serialization for an AI judge

Every N frames (30-60) serialize: player state (pos, vel, grounded, hp),
nearby-tile matrix relative to the player, entity proximities, distance to
goal, and frame outcome events. Dump to a file AND keep in memory. The judge
prompt asks for STRICT JSON back: `{"is_playable": bool, "confidence": float,
"bottlenecks": [...], "suggested_inputs": [...]}`. LLM responses are noisy -
extract the JSON with first-`{`-to-last-`}` slicing, not a full-string parse,
and unit-test the extraction with garbage-prefixed/suffixed samples.

Verdict states: PLAYABLE / NOT_PLAYABLE / **INCONCLUSIVE** (no API key, HTTP
failure, unparseable). INCONCLUSIVE is a real state - never fold it into
"failed", and never fake a verdict when the endpoint is unreachable.

## 4. Networking without libcurl

On bare boxes libcurl headers are often missing; OpenSSL is usually present.
A minimal HTTPS POST client is ~80 lines of POSIX sockets + OpenSSL
(`SSL_connect`, hand-write the POST, parse chunked responses). Wrap endpoint
URL/key/model in the config; if the key is absent at runtime, short-circuit
to INCONCLUSIVE with a log line instead of crashing.

## 5. Procedural rendering (ImDrawList) quick notes

- Tiles with a 3D bevel: base `AddRectFilled` + lighter top/left strip +
  darker bottom/right strip + outline. Camera = deadzone-follow on X only;
  transform via a single `WorldToScreen(world, cam, viewport_origin, scale)`.
- Parallax clouds/background layers drawn first, world next, HUD last. Keep HUD
  contrast in mind: light text over a bright banner is unreadable (seen in
  the platformer capture) - draw an opaque chip behind HUD text.
- **Default ImGui font ships few glyphs**: em-dashes and arrows render as `?`
  in log panels ("replaying..." became "replaying?"). Keep UI log strings
  ASCII, or load a fuller font at init.

## 6. THE capture race (cost an hour; do not repeat)

Symptom: headless selftest says the baseline replay finishes in 1242 frames;
the GUI's own log said 1424. Same binary, same input script.

Root cause: `capture_for_vision.sh` (and any screenshot harness) launches its
OWN instance of the app. With an instance left running from an earlier test,
two processes ticked simultaneously during the capture - the log lines the
agent read came from a DIFFERENT instance mid-run than assumed.

Rule: `pkill -f <binary>` before every capture, and when comparing sim counts
between headless and GUI logs, ensure exactly one process owned the run.
(capture_for_vision.sh now does the pkill itself; capture_frames_grid.sh too.)

## 7. Layout pattern: game canvas + overlay log panel

Game canvas fullscreen + a semi-transparent "AI Verification" log child
pinned bottom works well for agent-driven runs (the log IS the test report).
Keep the log window `ImGuiWindowFlags_AlwaysVerticalScrollbar` so long runs
stay readable, print one line per event (`[R1 f1242] replay done: ...`),
and auto-scroll to bottom. Verify with the vision loop: ask specifically
whether banner, HUD, world, and log lines are all legible.
