# SVG for Dear ImGui GUIs

ImGui has **no SVG runtime** — nothing loads `.svg` at execution time. In this
stack SVG plays two roles, and knowing which one you are in prevents weeks of
wrong-tooling:

| Goal | Right tool |
|---|---|
| Ship graphics inside the app (icons, spinners, loaders, decorations) | author small SVG → **convert to ImDrawList C++** with `scripts/svg2drawlist.py` (design-time codegen, zero runtime deps) |
| Draw procedurally from data you already have | write ImDrawList code directly (see main SKILL.md, `node-graphs.md`, `logic-circuit-simulator.md`) |
| Use a big/complex external SVG or raster art at runtime | rasterize offline to PNG and ship a texture — do **not** extend the converter |
| Animated graphic for docs / README / web (not the app) | plain SVG with SMIL; `templates/svg/*.svg` are also valid web assets |

The converter's output is normal hand-written-style ImGui code: it obeys the
1.92+ public API (`PathFillConcave`, `IM_COUNTOF`, `PushFont(NULL, size)`),
compiles under `-Wall -Wextra -Wpedantic -Wshadow -Wformat=2 -Werror`, and has
no cost beyond the draw calls it emits.

## Quick start

```bash
# from the imgui checkout (or anywhere with imgui.h on the include path):
python3 <skill>/scripts/svg2drawlist.py myicon.svg                 # -> ./myicon_imgui.h
python3 <skill>/scripts/svg2drawlist.py icons/ -o icons_gen.h --tint FFFFFF --runtime-include
python3 <skill>/scripts/svg2drawlist.py myicon.svg --check         # lint only, exit!=1 on errors
```

Each input file yields one function:

```cpp
inline void DrawSvg_<name>(ImDrawList* dl, const ImVec2& origin, const ImVec2& size,
                           float t,                       // ImGui::GetTime() to animate
                           ImU32 tint = IM_COL32(255,255,255,255)); // replaces --tint color
```

Call it any time between `NewFrame()`/`Render()` — from `ImDrawList` usage
under `ImGui::GetWindowDrawList()`, from a button's rect (`GetItemRectMin()`),
or from a draw callback. Pass `t = 0` for static assets. Output needs
`templates/svg/svg_runtime.h` (the `--runtime-include` flag wires it up).

Reference implementation (authoring set + demo app + strict build):
`templates/svg/` — `bash templates/svg/build.sh` regenerates
`svg_icons_imgui.h` from the `.svg` sources, then builds the headless
self-test and the `svg_widgets` GUI app. What it looks like was verified via
the vision loop (8 icons + 4 animated widgets all rendering, correct tint).

## Authoring rules for the convertible subset

Write SVGs that follow these rules and conversion is loss-free. The files in
`templates/svg/` are the worked examples.

* Geometry: `<circle> <ellipse> <rect> <line> <polyline> <polygon> <path>`
  (+ `<g transform/opacity>`, `<text>` with caveats below). Path commands
  `M L H V C S Q T A Z`, absolute or relative, any parameter repetition.
* Paint: hex/named colors, `fill="none"`, `opacity`, `stroke-width`,
  inline `style="stop-color:..."`. One gradient kind at a time on a `<rect>`
  (linear; radial gets a documented approximation).
* Animations (SMIL, loop forever): `<animate>` on numeric attributes
  (`cx cy r x y width height rx ry opacity stroke-width`) with
  `values`/`from`+`to`, `keyTimes`, `keySplines`, `begin`, `dur`,
  `repeatCount="indefinite"`; `<animate attributeName="stroke">` / `fill`
  with hex lists; `<animate attributeName="d">` (path morph);
  `<animateTransform type="rotate">`; `<animate attributeName="transform">`
  with `translate()/scale()` lists; `stroke-dasharray` animation for
  draw-on effects.
* `stroke-dasharray` must be a **single visible run** (spinner trick:
  `dash ≈ 75% of path length, gap ≥ rest`). Repeating patterns (gear teeth,
  dashes) do NOT convert — use explicit geometry for them (compare
  `icon-gear.svg`: circle + 8 `<line>` spokes, no dash pattern).
* GUI hygiene the `--check` mode enforces/advises: `viewBox` present; 24×24
  or 16×16 grid for icons; integral coordinates; monochrome art uses ONE
  color (pass it to `--tint`) so the runtime can recolor it; no `<script>`
  (hard error), no `class`/CSS `<style>` blocks (advisory — converter only
  reads attributes + inline `style=`).
* Keep the viewBox **square** if you use rotation animations: kx/ky may
  differ for non-square destination boxes and rotation happens in user
  space; non-square + rotate distorts unless `size` preserves aspect.

## Element → ImDrawList mapping

| SVG | Generated call |
|---|---|
| `circle` (fill / stroke / both) | `AddCircleFilled` / `AddCircle` (two calls when both) |
| `ellipse` | `AddEllipse(center, ImVec2(rx*kx, ry*ky), ...)` |
| `rect` | `AddRectFilled` / `AddRect` with `rounding = rx*ks` |
| `rect` with `fill=url(#linearGradient)` | `AddRectFilledMultiColor` (corner colors; diagonal gradient gets a mid-color approximation) |
| `line` | `AddLine` |
| `polygon` | `PathFillConcave` (public API — correct for concave shapes) |
| `polyline` | `PathStroke` |
| `path` stroke | `PathClear` + `PathLineTo`/`PathBezierCubicCurveTo`/`PathBezierQuadraticCurveTo` (curves stay analytic) + `PathStroke` |
| `path` fill | flatten (16 pts/curve) → `PathFillConcave` |
| `text` | `PushFont(NULL, font_size*ky)` + `CalcTextSize` + `AddText` (see pitfall 3) |
| `animateTransform rotate` | `SvgRotatePts` on tessellated ring each frame |
| `stroke-dasharray` (+ optional rotate) | tessellate, precompute `cl[]` arc-lengths, `PathLineTo` only points inside the window |

| SMIL | Runtime expression |
|---|---|
| `<animate values dur>` | `SvgKeysLoop(t, begin, dur, keys, n, keytimes, splines)` |
| `keySplines="0.5 0 0.5 1"` | `SvgEaseSpline` — cubic-bezier, same semantics as browsers |
| `begin="0.5s"` stagger | separate phase-offset loops (see `dots-loading.svg`) |
| `<animate attributeName="fill" values="#a;#b;#c">` | `SvgColorKeysLoop` |
| `repeatCount="indefinite"` | `SvgLoopT` = `fmod(t - begin, dur)` |

## Limitations (real, not hypothetical — all hit during development)

1. **`ImClamp`/`ImLerp` are in `imgui_internal.h`.** Generated runtime code must
   define its own tiny helpers instead — the skill's public-API-only policy.
2. **`ImVec2` has NO arithmetic operators unless you `#define
   IMGUI_DEFINE_MATH_OPERATORS`** before including imgui.h (which leaks
   operators into user builds). Generated code composes coordinates component
   -wise; hand-written conversions must too. (`no match for 'operator-'` at
   compile time is the signature.)
3. **`dl->AddText(ImFont*, size, ...)` silently emits ZERO vertices for a font
   size that has no baked data yet** (1.92 dynamic fonts). In a headless
   harness this never resolves; in a real window use `PushFont(NULL, size)` +
   the current-font `AddText(pos, col, text)` overload, and accept that brand-new
   sizes render from the *second* frame (the bake happens on the next
   `NewFrame`). Related: in headless texture-ack loops, window text at the
   default size does emit vertices, but extra baked sizes may still come out
   empty — verify text conversions with the vision loop, not vertex counts.
4. **`stroke-linecap="round"` is ignored** — ImDrawList has no cap flags. The
   generator warns; add `AddCircleFilled(endpoint, thickness/2)` if it matters.
5. **SVG filters (`feGaussianBlur` glow), `mask`, `clipPath`, `<use>`,
   `foreignObject`, CSS `<style>` blocks do not convert.** The generator emits
   `// WARN:` + keeps what animates (e.g. neon text keeps its opacity pulse,
   loses its glow). For glow in-app: draw the shape 2-3× with alpha falloff.
6. **`<animate attributeName="d">` between structurally different paths**
   (triangle→circle) is not even well-defined in SMIL; the converter
   arc-length-resamples every keyframe to 48 points and lerps point-wise.
   Result is *smoother than browsers* but re-times the surface parameter —
   fine for UI, wrong for exact path-following motion.
7. **Animated `cx/cy`/geometry + a static `transform` on the same element:**
   the animation is applied in user space (pre-transform); the generator warns
   when the combination would be ambiguous. Design one or the other.
8. **Rotate-animation pivots come from the keyframe data** (`from="0 100 100"`);
   if the pivot *moves* mid-animation only the first pivot is used (warned).
9. **Vertex cost**: rotation/dash/morph tessellate (48-96 pts ≈ 100-200 verts
   per animated shape). At UI scale this is free, but do not convert a
   500-element art SVG expecting the same budget — flatten static art to a
   texture instead.
10. **Default output lands in CWD** (`<name>_imgui.h`), never next to the
    input — keep design repos clean; always pass `-o` explicitly in scripts.

## Verification recipe for any conversion

```bash
# 1. lint
python3 <skill>/scripts/svg2drawlist.py assets/ --check
# 2. convert + strict compile + headless run (see templates/svg/build.sh for a worked pair)
python3 <skill>/scripts/svg2drawlist.py assets/ -o gen.h --runtime-include --tint FFFFFF
#    then compile gen.h's caller with the skill's usual flags and run 120+ frames
# 3. per-shape liveness: probe VtxBuffer delta per function at t=0 and t=0.5s;
#    animated shapes must differ in geometry OR (color-only) still compile —
#    templates/svg/svg_widgets.cpp selftest shows the pattern.
# 4. pixels: bash scripts/capture_for_vision.sh /tmp/svg_widgets "SVG Widgets - Dear ImGui"
#    -> vision_analyze with a question that NAMES every expected icon/widget.
```

Step 3 catches the "empty function" failure mode: a shape that silently emits
no draw calls still compiles and still exits 0. It bit the `<text>` conversion
during development; the probe found what the build gate could not.
