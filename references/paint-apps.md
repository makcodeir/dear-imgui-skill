# Paint / pixel-editor apps in ImGui (miniPaint native rewrite, Sep 2026)

Worked example: `/home/geek/Documents/programming/Agents/Skills/imgui/minipaint-cpp/`
(paint_core.h/.cpp + main.cpp + build.sh + README.md; plan at
`/home/geek/Documents/programming/.hermes/plans/2026-09-15_165204-minipaint-cpp.md`).
Scope was 3/10: brush/eraser/line/rect/ellipse/fill/picker, layers with
visibility+opacity+merge-down, BMP open / PNG+BMP save, zoom/pan. Explicitly
out of scope: undo/redo, selection, text tool, filters, PNG open, blend modes.

## 1. The pixel-format contract (settle this before any op)

Layer pixels are `uint32_t` packed with `IM_COL32` layout. On x86 LE that is
bytes `r,g,b,a` in memory, so three consumers need ZERO conversion:

- `glTexSubImage2D(GL_RGBA, ...)` texture upload
- `stbi_write_png(..., comp=4)` save
- BMP 32bpp writer (swaps to BGRA explicitly, one loop)

Enforce with ONE helper (`pack_rgba`) + a selftest assert that memcpys the
packed value to bytes and checks `r,g,b,a` order. Nothing else may hand-roll
packing. This kills the classic RGBA/BGRA/alpha-premult bug class at compile
review time.

## 2. Layer-as-texture + dirty-rect uploads (the perf core)

- One GL texture per layer, created lazily on first frame (`glTexImage2D` full
  upload), `GL_NEAREST` filtering.
- Edits mutate CPU pixels and UNION a dirty rect on the layer. Per frame:

```cpp
Rect r = dirty_take(l);
glBindTexture(GL_TEXTURE_2D, l.tex);
glPixelStorei(GL_UNPACK_ROW_LENGTH, l.w);   // skip untouched left margin
glTexSubImage2D(GL_TEXTURE_2D, 0, r.x0, r.y0, r.w(), r.h(), GL_RGBA,
                GL_UNSIGNED_BYTE, l.px.data() + (size_t)r.y0 * l.w + r.x0);
glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);     // pointer offset skips top margin
```

- Full-image uploads never happen after layer creation. Doc replacement
  (new/open) must `glDeleteTextures` old layers and zero `tex` fields.

## 3. GPU compositing (layers are free)

Draw back-to-front in the canvas child's draw list; per-layer opacity is a
vertex alpha tint — no CPU composite pass, ever:

```cpp
dl->AddImage((ImTextureID)(intptr_t)checker_tex, p0, p1, ImVec2(0,0),
             ImVec2((float)doc.w / 16.0f, (float)doc.h / 16.0f)); // uv scale = canvas px / tex size
for (const Layer& l : doc.layers) if (l.visible)
    dl->AddImage((ImTextureID)(intptr_t)l.tex, p0, p1, ImVec2(0,0), ImVec2(1,1),
                 IM_COL32(255,255,255, (int)(l.alpha * 255.0f)));
```

Checker background: one 16x16 GL texture (2x2 cells), `GL_REPEAT` wrap, uv
scaled by canvas px / 16 so cells zoom with the view.

## 4. Low-latency input model

- Brush/eraser: read `io.MousePos` per frame, `stroke_segment` from last point
  to current, update last — worst case one frame of latency, no event queue.
- Line/rect/ellipse: track drag start; preview as ImDrawList vector overlay
  (`AddLine` / `AddRect(a,b,col,0.0f,thickness)` — 5 args max, Pitfall 32 /
  `AddEllipse(center,ImVec2,col,0.0f,0,thickness)`); rasterize into the layer
  only on mouse release. Preview is O(1) and never touches pixels.
- Pan = middle-drag via `InvisibleButton` with
  `ImGuiButtonFlags_MouseButtonLeft|MouseButtonMiddle`; zoom at cursor =
  `pan = mouse_screen - canvas_pt * new_zoom` (node-graphs.md recipe).

## 5. Raster op notes

- **Thin strokes go dotted if you stamp circles** (Pitfall 42): for diameter
  <= 1.5 quantize to the pixel grid and `plot()` directly along the segment;
  reserve circle stamps for diameter >= 2.
- Flood fill: scanline stack + `visited` byte array (prevents infinite loops
  when the fill color is within tolerance of the target); match = squared
  per-channel diff vs the START pixel's color.
- Ellipse: fill = `(dx/rx)^2 + (dy/ry)^2 <= 1`; stroke = fill minus inner
  ellipse at `(rx-t, ry-t)`. No per-pixel sqrt.
- Blend: float source-over in `blend_over_a(dst, src, alpha_scale)`; layer
  opacity folds in via the same function (`l.alpha`), so merge-down and
  flatten and the GPU tint all agree.

## 6. Layout (three-shell recipe)

Root window = viewport-sized shell: main menu bar; left tool rail (square
buttons, width = 1.6 x frame height); center canvas child with EXPLICIT width
`avail.x - panel_w - ItemSpacing.x` (Pitfall 16 — a fill-width canvas child
swallows the right panel); right color/layers panel; content child gets a
NEGATIVE height to leave room for the status bar row below.

## 7. File I/O without network

- stb_image_write.h vendored from the GLFW deps tree
  (`projects/DBSCAN/external/glfw/deps/`). Under `-Werror` suppress
  `-Wshadow -Wunused-function -Wmissing-field-initializers` around the
  include, and include it at FILE SCOPE — inside a function body its
  declarations hit "expected unqualified-id".
- No stb_image.h on the box and no network: PNG open was cut; a ~60-line
  uncompressed 24/32bpp BMP reader covers "open existing" (bottom-up rows,
  padded 24bpp rows, top-down negative height). Selftest round-trips the
  writer and hand-crafts a 24bpp header to test the reader.

## 8. Verification patterns for paint apps

- Headless selftest binary (`-DPAINT_SELFTEST`): assert/PASS contract covers
  every raster op + blend math + dirty-rect union/clamp + file roundtrips.
- **Meta-lesson from this run: when selftest asserts fail, suspect the TEST's
  hand-computed expectations first.** 4 of 5 failures were test bugs (wrong
  blend value, forgot to set the layer alpha under test, reused a mutated
  layer for a negative case, expected "0 filled" when the correct semantics
  were "fills around the obstacle"). Only 1 of 5 was a code bug. Same spirit
  as Pitfall 34's "suspect your own check coordinates".
- **`--demo` flag**: apps whose state is user-drawn content have nothing to
  show in a one-shot capture — paint scripted content at startup so the
  vision loop sees strokes/shapes/fills. `capture_for_vision.sh` passes
  trailing args through: `capture_for_vision.sh ./app "Title" 4 --demo`.
- **Env-gated perf log**: `MINIPAINT_PERF=1` + `fprintf(stderr, "perf: %.1f
  fps")` every 120 frames gives numeric performance evidence without touching
  the UI (measured: vsync-capped 120 fps).
- **Vision-outage fallback** (vision tool unreachable all session): histogram
  palette match (`convert cap.png -colors 12 -format %c histogram:info:`) —
  demo colors present, and the semi-transparent green matched the hand-computed
  blend exactly; plus regional content probes. Both crop gotchas: ImageMagick
  `-crop` geometry is `WxH+X+Y` (an `"x,y WxH"` geometry silently crops
  NOTHING and returns whole-image stats — identical stats for every region is
  the tell), and read the capture's real `%wx%h` first (the WM had resized it).

## 9. 1.93.0 WIP traps hit building this

- `ImVec2` has NO operator+/- on this tree — write `ImVec2(p0.x-1, p0.y-1)`.
- No `SetNextWindowViewport` / `ImGuiWindowFlags_NoDocking` on a non-docking
  master build. Don't copy them from docking-branch snippets.
- `AddImage` takes `ImTextureRef`; the `(ImTextureID)(intptr_t)tex` cast still
  converts implicitly.
