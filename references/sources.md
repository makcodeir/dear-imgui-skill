# Authoritative Sources (where to look, in order)

Do not guess and do not invent. These are the upstream references that ship in
the repository, in the order to consult them.

## 1. In the repo (always correct for *this* checkout)

| Need | Read |
|------|------|
| Exact signature / enum of any API | `imgui.h` — grep the name, read the adjacent comment |
| Lifecycle, mission, controls, integrator guide | `imgui.cpp` top: `CONTROLS GUIDE`, `PROGRAMMER GUIDE`, `API BREAKING CHANGES` |
| Working code for widgets/apps | `imgui_demo.cpp` (`ImGui::ShowDemoWindow()`) |
| Fonts: load, merge icons, DPI, UTF-8 | `docs/FONTS.md` |
| Integration Q&A, ID system, textures, DPI | `docs/FAQ.md` |
| What each example is | `docs/EXAMPLES.md` |
| Upcoming/known gaps | `docs/TODO.txt` |
| Version history | `docs/CHANGELOG.txt` |
| Build-time config (fonts, math types, allocators, IMGUI_DISABLE_*) | `imconfig.h` |
| Internal API (no forward-compat guarantee) | `imgui_internal.h` |
| Debug/edge-case hooks (`ConfigDebug*`) | `imgui.h` → search `ConfigDebug`; toggled in `Demo > Tools > Debug` |

Rules:
- The declaration in `imgui.h` **is** the spec. When a signature question comes
  up, `search_files` for it rather than recalling it.
- `imgui_demo.cpp` is the canonical usage reference — copy its idioms.
- `imgui_internal.h` is *not* stable API. Only reach for it when the public API
  genuinely cannot do the job.

## 2. Online (source of truth when the repo copy is older)

- Docs hub: https://github.com/ocornut/imgui/tree/master/docs
- Getting Started (integration in an existing app): https://github.com/ocornut/imgui/wiki/Getting-Started
- FAQ: https://www.dearimgui.com/faq
- Wiki index: https://github.com/ocornut/imgui/wiki
- Useful extensions (ImPlot, ImPlot3d, node editors, ...): https://github.com/ocornut/imgui/wiki/Useful-Extensions
- Image loading examples: https://github.com/ocornut/imgui/wiki/Image-Loading-and-Displaying-Examples
- Interactive demo + source browser: https://pthom.github.io/imgui_explorer
- Releases/changelog: https://github.com/ocornut/imgui/releases

### Extracting wiki pages (field-verified Sep 2026)

The GitHub wiki renders through the browser UI — scraping `innerText` of
https://github.com/ocornut/imgui/wiki/Getting-Started costs multiple fragile
calls (heading anchors don't match the TOC; content sits in wiki-specific
nodes). **Go straight to the markdown source** — every wiki page is mirrored:

```bash
curl -sL https://raw.githubusercontent.com/wiki/ocornut/imgui/Getting-Started.md
```

One call, clean markdown, quotes and code blocks intact. The wiki's
"Getting Started" is the integration guide — the repo `docs/` has no
GETTING_STARTED.md (verified on the 1.93.0-WIP checkout; README links to the
wiki instead). Also: upstream turned wiki editing off (announced on the Home
page), so page content is stable; when in doubt, `diff` against the same doc
in the local checkout — the repo copy is authoritative for *its* version.

## 3. Version discipline

Always know the tree's era before editing:

```bash
grep -m1 '#define IMGUI_VERSION ' imgui.h
grep -m1 '#define IMGUI_VERSION_NUM' imgui.h
```

`IMGUI_VERSION_NUM >= 19198` → the 1.92+ dynamic-font world. Read
`version-1.92-changes.md` before pasting any online snippet. Anything authored
before mid-2025 is suspect for font, texture, and `PushFont` code.

## 4. What NOT to do

- Do not add `#include <cmath>`-style modal functions to `imgui.cpp` /
  `imgui.h` — `ImGui::` is a namespace; add helper functions in your own
  translation units instead. Upstream asks you not to modify `imgui.cpp`.
- Do not ship as a DLL/shared library; build the `.cpp` files into your project.
- Do not rely on `imgui_internal.h` structures across versions.
- Do not use the legacy `Combo()`/`ListBox()` APIs; prefer `BeginCombo()` /
  `BeginListBox()`.
