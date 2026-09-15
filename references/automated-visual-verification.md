# Automated Visual Verification Loop (control API + settle-sync + vision)

Extends "The Vision Loop" from one-way (launch → screenshot → look) to a
CLOSED loop (act → settle → capture → assert → next action). Built and
validated Sep 2026 against imgui 1.93.0 WIP + GLFW/OpenGL3.

Reference implementation (pending user review before promotion to
`templates/vision_loop/`):
`/home/geek/Documents/programming/Agents/Skills/imgui/vision-loop/` —
`imgui_harness.h` (~330 Lo, header-only, POSIX UDS, no deps beyond ImGui
public API), `loop_demo.cpp`, `drive.py` (stdlib client), `closed_loop.sh`
(orchestrator), `build.sh` (static-GLFW link per game-editor-apps.md).

## Architecture in one paragraph

The app embeds a Unix-domain-socket control harness. It registers (a) flat
model vars (`VarInt/VarFloat/VarBool`, once) and (b) the LIVE screen rect of
each testable widget (`BeginActions()` per frame + `Action("name")` right
after emitting the widget, using `GetItemRectMin/Max`). Driver sends one
line-command per connect; replies are single `OK k=v k=v` / `ERR reason`
lines. `set <var> <v>` = deterministic state SETUP. `press <name>` /
`clickat x y` = injects REAL `io.AddMousePosEvent`/`AddMouseButtonEvent` at
the widget's live rect center over hover/down/up frames — exercises actual
hit-testing and rendering (the "domain-specific computer use" anchor:
coordinates come from the app's own layout, never OS-level guessing;
verification stays visual). `frame` counter + `sync N` barrier replace every
fixed sleep.

## Command contract

| cmd | reply |
|---|---|
| `ping` / `frame` | `OK frame=<n>` |
| `state` | `OK frame=<n> count=3 gauge=0.50 blue=1` (all registered vars) |
| `get <var>` / `set <var> <v>` | `OK frame=<n> <var>=<val>` / `... applied_frame=<n+1>` |
| `press <name>` / `clickat <x> <y>` | `OK queued_through=<F+2>` |
| `sync <n>` | deferred until frame >= n, then `OK frame=<n>` |
| `register_dump` | `OK frame=<n> inc=(x,y) ...` |
| `quit` | `OK bye` → clean exit 0 |

Unknown var/action → `ERR ...` — never silent.

## THE ordering fact (verified against imgui_impl_glfw.cpp)

`ImGui_ImplGlfw_UpdateMouseData()` + cursor callbacks **re-queue the OS mouse
position inside `ImGui_ImplGlfw_NewFrame()` every frame**. Therefore
`Harness::Poll()` MUST run AFTER both backend `_NewFrame()` calls and BEFORE
`ImGui::NewFrame()`, so synthetic events are last in io's per-frame queue and
win. Consequences:

1. Re-inject `AddMousePosEvent(x,y)` on EVERY frame of a click in flight —
   inject position once then button-down the next frame, and the backend's
   fresh OS position event (queued before yours) moves the cursor back → the
   down lands at the real mouse → click misses.
2. Click = 3 consecutive frames: F hover(pos), F+1 pos+down, F+2 pos+up.
   Activation fires on the UP frame while hovered. `queued_through=F+2`;
   post-action state is in presents F+2 and F+3.
3. After the sequence the injected cursor stays STICKY at the last target
   (visible in screenshots as a lingering hover-highlight — the vision model
   actually caught the Plus button still highlighted). Useful for
   IsItemHovered-style checks; just know captures show it.

## Settle/sync semantics

- Frame counter ticks once per loop iteration in `EndFrame()` (after present),
  so at iteration j during Poll, `frame == j`; after its EndFrame, `frame == j+1`.
- `sync N` is answered WITHOUT blocking the app: handler stores
  `pending_target`; the loop answers when `frame >= N` (client waits on the
  open connection with a socket timeout → driver prints `ERR driver-timeout`
  if the loop stalls).
- Capture rule: after `press`/`set`, issue `sync queued_through+2` (resp
  `applied_frame+1`) so ≥2 fully-presented frames show the post-action state
  before `import -window`. Launch readiness = poll `ping`, not sleep.

## Protocol trade-offs (why UDS + key=value)

UDS: zero deps, no port races, filesystem perms, python-stdlib client, one
`accept`ed non-blocking client is all a test loop needs. HTTP/WebSocket were
rejected for a hand-rolled-parser cost with no agent-side gain (curl
friendliness noted as extension point); text `k=v` beats JSON because no C++
parser is needed and bash `sed 's/.*frame=//'` extracts values.
ImGuiTestEngine (upstream) is strictly more powerful but ~20 files
+ activation flags; the harness is one header and composes with the existing
capture scripts. Choose per budget, mention both to the user.

## Verified run (what success looks like)

`closed_loop.sh` manifest: baseline `count=0` → `press inc` x3 →
`count=3 gauge=0.50 blue=0` → `set gauge 0.95` → `blue` press → `blue=1`;
negative controls `ERR`; exit 0. Vision reads of the `_small.png` captures
confirmed BOTH text locations showed Count: 3, grey→blue canvas flip, and
gauge fill ≈50%→95% — text assertion and pixels agreed at every step. Pass
criterion for any app: state= line matches the action AND the vision answer
matches the state line.

## C++/build pitfalls hit while implementing

- **`-Werror=format-truncation`** fires on `snprintf(buf, n, " %s=...", name)`
  even when `name` is a `char[64]` member — gcc adds float worst cases and
  bounds the region by the format's remaining size. Fix = explicit precision
  matching the field's real max: `" %.63s=%d"`, `"%.200s"` for error echoes.
  This is the strict-build tax on hand-rolled reply formatting; budget for it.
- **Tag/function name collision:** `struct Action` + `inline void Action(...)`
  in the same namespace — inside the function body `Action& s = ...` resolves
  the *ordinary* name to the function → clang "must use struct tag", and g++
  agrees later. Name records distinctly (`ActionRec`) when an accessor shares
  the concept's name.
- `sscanf` into fixed `char a1[64]` with `%63s` keeps dispatch parsing
  warning-clean under `-Wformat=2 -Werror`.
