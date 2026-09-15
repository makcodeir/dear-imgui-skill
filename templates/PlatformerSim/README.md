# PlatformerSim template

A complete, field-tested deterministic 2D platformer + AI-playability
verification harness in pure Dear ImGui (all visuals procedural ImDrawList,
no sprites, no game engines). The worked example behind
`references/platformer-verification.md`.

## Files

| File | Role |
|---|---|
| `platformer_core.h` | Types: `Vec2`/`AABB`, `EngineConfig`, tilemap/`Level`, `GameState`, `FrameInput`, `ReplayResult`/`RunReplay`, AI client + render APIs |
| `platformer_engine.cpp` | 224x15 level (pits, pipes, staircase, flag, 8 goombas, coins), fixed-dt sim (coyote time, jump buffer, variable jump, momentum/friction), axis-separated AABB sweeps, stomp/damage, events, camera, telemetry serializer, geometric baseline-script generator |
| `platformer_aiclient.cpp` | Minimal HTTPS client on OpenSSL + POSIX sockets (no libcurl), judge prompt, chunked-response decode, strict-JSON verdict extraction |
| `platformer_render.cpp` | GLFW+OpenGL3 harness: procedural world (beveled tiles, goombas, player, coins, flag, parallax clouds), HUD, auto-scrolling "AI Verification" log panel |
| `main.cpp` | Auto cycle: replay baseline script -> dump telemetry -> query AI auditor -> live play; `[R]` restarts |
| `platformer_selftest.cpp` | 25-assertion headless suite (no GLFW/GL needed) |
| `build.sh` | Builds both binaries with strict warnings |

## Build

Paths in `build.sh` assume this layout (edit the two `GLFW_ROOT`/`IMGUI`
variables for your machine - see `references/cmake-integration.md` for the
static-GLFW toolchain discovery):

    cd PlatformerSim && bash build.sh

Requires: imgui sources, static GLFW 3.3+, OpenSSL dev (`-lssl -lcrypto`).
The selftest itself needs NO display and NO GL - it links only the engine
sources and runs anywhere:

    /tmp/platformer_selftest        # expect: 25 checks, 0 failed
    /tmp/platformer                 # GUI; replay -> telemetry -> AI audit

## Key patterns worth stealing

1. **Input scripts as data** (`FrameInput` arrays) + a `RunReplay()` core -
   makes the game itself a test fixture.
2. **Bit-identical replay assertion** - run the script twice, compare
   everything; catches any sim nondeterminism early.
3. **Telemetry snapshots every N frames** - the bridge between a running sim
   and an LLM judge.
4. **INCONCLUSIVE as an explicit verdict** - no API key / HTTP failure is not
   a test failure; log it and move on.
5. **HUD over an opaque chip; ASCII-only log strings** - the default ImGui
   font lacks em-dash/arrow glyphs (they render as `?`) and light text over a
   bright banner is unreadable.

## Known state (as shipped)

- Selftest 25/25; baseline replay REACHED_GOAL in 1242 frames (score 2835,
  14 coins, 1 stomp) - deterministic across runs.
- AI verdict requires `OPENAI_API_KEY` (or compatible) at runtime; without it
  the cycle completes and logs `INCONCLUSIVE (no verdict)`.
- Cosmetic: default-font glyph substitution (`?`) if you re-add em-dashes;
  second HUD line is low-contrast on the banner.
