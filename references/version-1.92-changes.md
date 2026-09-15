# Dear ImGui 1.92+ API Changes (what breaks copied tutorials)

Version 1.92.0 (June 2025) introduced the **dynamic font system** and backend
texture management. Any tutorial, snippet, or StackOverflow answer written
before that date may be **wrong** against a 1.92+/1.93 tree. This file is the
short list of what actually changed, so you can spot and fix stale code fast.

Check the tree first:

```bash
grep -m1 '#define IMGUI_VERSION_NUM' imgui.h   # >= 19198 means 1.92+ era
```

Source of truth: `docs/FONTS.md` ("New! Dynamic Fonts system in 1.92"),
`docs/FAQ.md`, and the "API BREAKING CHANGES" section of `imgui.cpp`.

---

## 1. Fonts: glyph ranges are obsolete

| Before 1.92 | 1.92+ |
|---|---|
| `io.Fonts->AddFontFromFileTTF("f.ttf", 18.0f, nullptr, io.Fonts->GetGlyphRangesJapanese())` | `io.Fonts->AddFontFromFileTTF("f.ttf")` |
| `io.Fonts->AddFontDefault()` then hope | `AddFontDefaultVector()` (scalable) or `AddFontDefaultBitmap()` (pixel) |
| `static const ImWchar icons[] = {...}; AddFontFromFileTTF(..., icons);` | Just `config.MergeMode = true; AddFontFromFileTTF(icon_font, 13.0f, &config);` |
| `GetGlyphRangesXXX()` used everywhere | Only needed if the backend lacks `RendererHasTextures` |

Precision: every `GetGlyphRanges*()` helper **except `GetGlyphRangesDefault()`**
is now obsolete-guarded (compiled only when `IMGUI_DISABLE_OBSOLETE_FUNCTIONS`
is *not* defined). `GetGlyphRangesDefault()` remains public API. The comment in
`imgui.h` states it plainly: *"Since 1.92: specifying glyph ranges is only
useful/necessary if your backend doesn't support
`ImGuiBackendFlags_RendererHasTextures`"*.

The atlas is now built incrementally and resized dynamically, so the old
"font atlas too big → white rectangles" failure mode is largely gone.

## 2. `PushFont` requires a size argument

```cpp
// BEFORE 1.92
ImGui::PushFont(font);

// 1.92+
ImGui::PushFont(font, 0.0f);             // 0.0f = keep current size
ImGui::PushFont(NULL, 32.0f);            // keep current font, set size 32
ImGui::PushFont(font, font->LegacySize); // legacy behaviour: size passed to AddFontXXX()
```

**Trap:** `PushFont(NULL, ImGui::GetFontSize())` is WRONG. `GetFontSize()`
returns the size *after* global scale factors (`FontScaleMain`, `FontScaleDpi`)
are applied, so passing it double-applies them. Use `style.FontSizeBase`
instead:

```cpp
ImGui::PushFont(NULL, ImGui::GetStyle().FontSizeBase * 2.0f); // 2x current
```

## 3. `IM_ARRAYSIZE` renamed

```cpp
IM_ARRAYSIZE(buf)   // obsolete in 1.92.6
IM_COUNTOF(buf)     // current
```
Both exist (the old is an alias) unless `IMGUI_DISABLE_OBSOLETE_FUNCTIONS`.

## 4. `ImTextureID` vs `ImTextureRef`

1.92 split texture identifiers:
- `ImTextureID` — backend-level handle (default `ImU64`, up to 64 bits).
- `ImTextureRef` — higher-level: holds *either* an `ImTextureID` *or* an
  `ImTextureData*` (for backend-managed textures, mainly the font atlas).

- Drawing functions (`Image`, `ImageButton`, `AddImage`) now take
  `ImTextureRef`. You can create one implicitly from your `ImTextureID`:
  ```cpp
  ImGui::Image((ImTextureID)(intptr_t)my_gl_tex, ImVec2(w, h)); // fine
  ```
- There is **no** implicit `ImTextureRef` → `ImTextureID` cast (it is lossy).
  Use `tex_ref.GetTexID()`.
- `ImDrawCmd::GetTexID()` replaced direct `.TextureId` access.
- Bind the atlas for custom rects via `io.Fonts->TexRef`.

## 5. Backend textures: `ImGuiBackendFlags_RendererHasTextures`

Fonts now stream updates to the GPU through a texture list instead of
`GetTexDataAsRGBA32()` + a backend-specific upload call.

- Renderer backends must set `ImGuiBackendFlags_RendererHasTextures` and, each
  frame, iterate `ImDrawData->Textures` handling `ImTextureStatus_WantCreate` /
  `_WantUpdates` / `_WantDestroy`.
- Obsoleted `ImFontAtlas` functions: `Build()`, `GetTexDataAsRGBA32()`,
  `GetTexDataAsAlpha8()`, `SetTexID()`, `IsBuilt()`.
- This will likely be **required** of all renderers before June 2026.
- If you use the standard `imgui_impl_*` files unmodified, you get this for
  free — you only need to act on this when writing your own renderer.

## 6. Misc renames/obsoletions in the 1.92s

- `ImGuiChildFlags_Border` → `ImGuiChildFlags_Borders` (renamed 1.91.1).
- `ImGuiWindowFlags_NavFlattened` → `ImGuiChildFlags_NavFlattened` (1.90.9).
- `ImGuiWindowFlags_AlwaysUseWindowPadding` → `ImGuiChildFlags_AlwaysUseWindowPadding`.
- `BeginChild(name, size, bool border)` still compiles (`Borders == 1 == true`).
- `AddCustomRectFontGlyph()` obsoleted (doesn't fit resizable fonts); use a
  custom `ImFontLoader` for colourful/custom glyphs.
- `ImFontConfig::FontDataOwnedByAtlas = false` — data must survive until
  `RemoveFont()`/context shutdown (a bug made this look fine before 1.92.6).

## 7. Behaviourally unchanged (safe to copy)

- Window/widget/table/menu/popup API names and signatures.
- `Begin`/`End` lifecycle and the true/false pairing rules.
- `##`/`###` label-ID conventions and `PushID`/`PopID`.
- The five-beat frame lifecycle and `ImGui_ImplXxx_*` backend entry points.
- `IM_COL32`, `ImVec2`/`ImVec4` layout rules and `-FLT_MIN` fill-width idiom.
- `IMGUI_CHECKVERSION()` before `CreateContext()`.

## Quick audit for stale code

```bash
# Obsolete font/atlas calls that may indicate pre-1.92 code:
grep -nE 'GetGlyphRanges|GetTexDataAs(RGBA32|Alpha8)|Fonts->Build|PushFont\([^,)]*\)|IM_ARRAYSIZE' your_file.cpp
```
If any of those appear and the tree is 1.92+, fix before debugging anything else.

---

## 8. 1.92.8: AddRect-family argument swap (ambiguity trap)

The current signature is
`AddRect(p_min, p_max, col, float rounding = 0.0f, float thickness = 1.0f, ImDrawFlags flags = 0)`.
An obsoleted inline overload `(…, float rounding, ImDrawFlags flags, float thickness)` also exists in
the header. A 6-argument call passing three trailing floats — e.g.
`AddRect(a, b, col, 0.0f, 0.0f, 1.0f)` — matches **both** and is a hard
`call of overloaded 'AddRect(...)' is ambiguous` error under g++ `-Werror`. Fix: omit defaulted
trailing args, or pass an explicit `ImDrawFlags` constant where the old flags slot was. (Confirmed on
1.93.0 WIP / 19297, paint-app A/B eval Sep 2026 — one arm's first failed cycle was exactly this.)

## Verification log (eval sessions)

Claims in this skill were exercised against the live tree, not just read from
docs. Method and results, so a future eval can re-run them:

| Claim | Method | Result |
|-------|--------|--------|
| Version era is 1.92+ | `grep IMGUI_VERSION_NUM imgui.h` | `19297`, `IMGUI_HAS_TEXTURES` present |
| `BeginChild` always needs `EndChild()` | Ran a program with `io.ConfigDebugBeginReturnValueOnce = true` (forces the first Begin/BeginChild to return false); compared skip vs call paths | **skip → assert** `"Must call EndChild() and not End()!"` (imgui.cpp:8358); **call → survived** |
| All cited identifiers exist | Extracted every `ImGui*`/`IM_*` token from the skill and grepped `imgui.h` | 0 missing (backend `ImGui_Impl*` entry points verified against `backends/*.h`) |
| Headless template builds + runs clean | `bash scripts/build_headless.sh` | 0 warnings under `-Wall -Wextra -Wpedantic -Wshadow -Wformat=2 -Werror`; printed "Rendered 3 frames without error." |
| GLFW template builds + links clean | Full `g++ ... -lglfw -lGL -ldl -lpthread` | exit 0, 0 warnings |
| DreamIDE template builds from the skill tree | `bash build.sh` in `templates/DreamIDE/` (installed copy) | 0 warnings, runs — needed `build.sh` hardening (env `IMGUI_DIR` + candidate probe + known-path fallback); the source repo's relative `../imgui` doesn't resolve in the skill tree |
| Old `Combo()`/`ListBox()` helpers are current API | grep `Combo` in imgui.h | imgui.h:671/:795: "kept available for convenience" — exclude from the stale-code audit grep |

Reproduce the `BeginChild` proof:

```bash
# force Begin/BeginChild to return false, then deliberately skip EndChild
g++ -std=c++11 -I. t.cpp imgui.cpp imgui_demo.cpp imgui_draw.cpp \
    imgui_tables.cpp imgui_widgets.cpp -o t && ./t   # asserts
```

Note the assert lands in `End()`/frame-end, **not** at compile time, which is
why this class of mistake survives to runtime — exactly what the skill warns
about.

