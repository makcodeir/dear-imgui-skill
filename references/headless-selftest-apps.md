# Headless ImGui self-test apps (spec-driven, no backend)

Pattern validated building a single-file ImGui "Paint" app against a strict
A/B-eval spec (448×448 canvas, 9 scripted ops with named pixel asserts, second
debug binary for End-pairing, mandatory compile journal). Reusable for any
"headless app + self-test + journal" class requirement; for replay/determinism
variants see `references/platformer-verification.md`; init block per
`templates/gallery_headless.cpp`.

## Architecture that held up (0 wasted cycles)

- Model in plain structs + contiguous `std::vector<ImU32>`; tools are free
  functions `Tool*(Canvas&, coords..., ImU32 col)` with no ImGui state inside.
- **Single code path for UI and self-test** (the usual hard spec requirement):
  `Op*(Canvas&, History&, ...)` wrappers = `HistoryRecord()` + `Tool*...`. The
  UI mouse handler calls the same functions (press: record once + stamp first
  point; drag: stamp segments; release: commit shape ops). Flood fill op runs
  immediately on click.
- Undo/redo: full-canvas snapshot stacks `std::vector<std::vector<ImU32>>`,
  capped (spec wanted ≥5; 16 used). 448²×4B×16 ≈ 13 MB worst case — fine.
- Canvas-to-frame: run-length `ImDrawList::AddRectFilled` blit per row (spec-
  blessed alternative to the ImTextureData route; no backend to upload to).
- Quit-flag discipline: menu Exit writes `quit_requested`, the frame loop
  actually `break`s on it (Pitfall 13 dead-write avoidance, compiler-visible).

## stdout contract style

One line per named op, then an exactly-formatted summary:

```cpp
static int failures = 0;
static void CheckNamed(const char* name, bool ok) {
    printf("assert %s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) failures++;
}
// end: printf("paint OK ops=9 painted=%d\n", painted);
```

Exit 1 after printing FAILs — never abort() — so graders see which check died.
Print PASS lines BEFORE the frame loop; write the artifact (canvas.ppm) after
it; the summary line must remain the last stdout line.

## Verify your count independently, in Python, before the judge does

Recount painted pixels from the PPM you wrote (catches alpha/endianness bugs
the in-memory count can't):

```python
d = open('canvas.ppm','rb').read()
assert d[:2] == b'P6'; idx = 2          # consume magic ONCE (re-parsing it
                                          # is the classic int(b'P6') crash)
nums = []
while len(nums) < 3:                    # w, h, maxval
    while d[idx] in b' \t\r\n': idx += 1
    j = idx
    while d[j] not in b' \t\r\n': j += 1
    nums.append(int(d[idx:j])); idx = j
w, h, mx = nums; idx += 1               # single whitespace byte before payload
px = d[idx:]
assert len(px) == w*h*3                 # binary P6 = RGB888, no alpha
n = sum(1 for i in range(0, w*h*3, 3)
        if (px[i], px[i+1], px[i+2]) != (32, 32, 32))
print(n)                                # must equal printed painted=
```

Choose a bg color that no tool ever emits (not near white/black) so the
"differs from background" recomputation is unambiguous.

## End-pairing proof binary (R6 pattern)

Second build = R1 command + `-DDEBUG_RETURN_TEST`; in main under that ifdef:
`io.ConfigDebugBeginReturnValueLoop = true;` before the frame loop. Identical
self-test output + exit 0 with no imgui assert ⇒ every `Begin*` false branch is
paired (`End`/`EndChild` unconditional, everything else iff true — Pitfall 1).
Costs one line of app code and one compile; converts "I think pairing is
right" into runtime-proven.

## Compile-cycle ledger for strict -Werror ImGui

Empirical (Sep 2026 paint build): 1 failed cycle total, on non-ImGui C++
hygiene (unused local, missing `<cstdint>`, ImClamp scope, AddRect overload —
Pitfalls 31–33). The class of failure that DOESN'T cost cycles is prevented by
the gallery_headless init block + texture-ack loop (Pitfall 11). Geometry
correctness never belongs in the compile loop: port each plot algorithm to a
Python one-liner first and test `point in plotted_set` for the exact asserts
you plan to write (Pitfall 34) — two runtime self-test FAILs were fixed
between compiles, spending zero cycles.

## Journal (BUILDLOG.md) discipline under shared-agent conditions

- Append immediately after EVERY compile: attempt #, exact command, exit code,
  first error lines verbatim (capture via `> attemptN.log 2>&1`; state that a
  clean build's 0-byte log is success, NOT an interrupted build — this exact
  misreading happened when a sibling process annotated our journal).
- Final summary: total failed cycles + one line per error class.
- Never rewrite/delete history; if another agent's entries claim your history,
  append a corrective provenance note citing file mtimes and tool-generated
  "modified by sibling subagent" warnings as evidence (Pitfall 35).
- Terminal heredoc append with `&&` in the prose trips the '&'-backgrounding
  guard; use write_file + `cat tmp >> journal`.
