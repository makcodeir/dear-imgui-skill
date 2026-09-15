# Installing Dear ImGui (from source / minimal files from GitHub)

Upstream's own guidance ([wiki/Getting-Started](https://github.com/ocornut/imgui/wiki/Getting-Started)):

> "It is preferable that you build yourself from sources."
> "It is recommended that you follow those steps and **not attempt to build Dear
> ImGui as a static or shared library** ... Note that Dear ImGui is both small
> and easy to build, so adding its files directly to your project should be
> fine. Note that Dear ImGui is a very call-heavy API, so building as a shared
> library is not ideal."

Dear ImGui is **not a system library** — there is no `./configure && make install`.
You vendor ~16 small files and compile them *with your app*. Three ways to do
that, in the order you should consider them:

| Method | When | Cost |
|---|---|---|
| A. `git clone` into the project | you'll track upstream / need examples+docs | 131 MB with `.git` (measured) |
| B. minimal files from GitHub raw | just need to build an app | **17 files, ~4 MB** (measured, 1.93 WIP incl. opengl3 loader header) |
| C. release tarball | offline machine, pinned version | full tree, ~4 MB (est., not measured) |

**Trap — do NOT `apt install libimgui-dev`** (or `libimgui-*` distro packages):
distros ship old versions (1.86 on Ubuntu 22.04) from the **pre-1.92 font
system** — code written against it fails to compile or asserts on 1.92+ trees
and vice-versa. See `version-1.92-changes.md`. The distro package also splits
headers/lib in ways upstream never tests.

## Method A — git clone into the project (upstream-recommended default)

```bash
cd your_project            # vendor it as a subfolder, commit it (or use a submodule)
git clone --depth 1 https://github.com/ocornut/imgui.git third_party/imgui
# 'docking' branch instead of master if you want Docking/Multi-viewport:
# git clone --depth 1 -b docking https://github.com/ocornut/imgui.git third_party/imgui
```

- `master` and `docking` are the two maintained branches; both are safe to
  track. Pin a tag (`git checkout v1.92.3`) for reproducible builds — see
  https://github.com/ocornut/imgui/tags for the list.
- You get `examples/`, `docs/`, `misc/` too — fine, ignore them; only the root
  `.cpp/.h` and one backend pair ever get compiled.

## Method B — minimal files from GitHub raw (smallest working set)

`scripts/install_imgui_minimal.sh [TAG] [DEST]` downloads exactly what compiles
and runs — verified Sep 2026 against `master` (1.93.0, `IMGUI_VERSION_NUM 19297`):

```bash
bash scripts/install_imgui_minimal.sh master third_party/imgui
```

What it fetches and why each file exists:

```
imgui.cpp imgui_demo.cpp imgui_draw.cpp imgui_tables.cpp imgui_widgets.cpp
                                   # the 5 core translation units — compile these WITH your app
imgui.h imgui_internal.h           # public + internal API headers
imconfig.h                         # user config #defines (empty by default)
imstb_rectpack.h imstb_textedit.h imstb_truetype.h   # vendored stb libs used by draw/widgets
backends/imgui_impl_glfw.h/.cpp    # platform backend pair  (pick YOUR combo)
backends/imgui_impl_opengl3.h/.cpp # renderer backend pair  (pick YOUR combo)
backends/imgui_impl_opengl3_loader.h  # embedded GL3W-based loader — REQUIRED on
                                      # 1.93+ (imgui_impl_opengl3.cpp includes it
                                      # unconditionally; its absence is a compile
                                      # error: "imgui_impl_opengl3_loader.h: No
                                      # such file or directory")
LICENSE.txt
```

- `imgui_demo.cpp` is required by default (`imgui.cpp` references
  `ShowDemoWindow`/debug tools in it). Exclude it only if you
  `#define IMGUI_DISABLE_DEMO_WINDOWS` at project level.
- Backend pairs are swappable: `imgui_impl_sdl2`, `imgui_impl_sdl3`,
  `imgui_impl_win32`, `imgui_impl_wgpu`, `imgui_impl_dx11`, `imgui_impl_dx12`,
  `imgui_impl_vulkan`, `imgui_impl_opengl2`, ... — each is one `.h` + one
  `.cpp`. Pick the platform you window with + the renderer you draw with.
- `std::string`-based `InputText`: additionally fetch
  `misc/cpp/imgui_stdlib.h/.cpp` (then `#include "misc/cpp/imgui_stdlib.h"`).
- The script verifies the download (`IMGUI_VERSION` present, warns when the
  tag is pre-1.92) and prints the ready-to-paste g++ command.

## Method C — release tarball (offline / pinned)

```bash
curl -LO https://github.com/ocornut/imgui/archive/refs/tags/v1.92.3.tar.gz
tar xzf v1.92.3.tar.gz && mv imgui-1.92.3 third_party/imgui
```

Same file layout as a clone; same minimal-file set applies (the tarball is the
full tree, so delete nothing — just compile the 5 core files + backend pair).

## Verify what you vendored

```bash
grep -m1 '#define IMGUI_VERSION_NUM' third_party/imgui/imgui.h
# >= 19198  → 1.92+ dynamic-font API (what this skill's API tables assume)

# 10-second completeness probe — compiles the core with zero backends:
g++ -std=c++11 -Ithird_party/imgui -c third_party/imgui/imgui.cpp -o /tmp/imgui_probe.o
```

Drift check (field-verified Sep 2026): if a known-good checkout exists locally,
`diff` the vendored `imgui.h` against it — byte-identical means the skill's API
tables and 1.92-change list apply unmodified; a diff means re-check
`version-1.92-changes.md` before trusting them. `diff -q` exit 1 also catches a
truncated/partial vendored copy instantly.

Then compile your app with the 5 core `.cpp` + your backend pair. Full recipes:
plain `g++` one-liner in the script's output / SKILL.md; CMake wiring in
`cmake-integration.md` (point `IMGUI_DIR` at `third_party/imgui` — verify
`imgui.h` AND `backends/` exist there; nested same-named folders have cost
real configure cycles).

## What NOT to do

- Don't `apt install libimgui-dev` (pre-1.92, distro-packaged layout).
- Don't build a `libimgui.a`/`.so` unless you specifically need it — upstream
  advises compiling the sources directly into your target (call-heavy API;
  shared-lib overhead).
- Don't download files one at a time by hand and skip `imstb_*.h` or
  `imgui_internal.h` — `imgui_draw.cpp`/`imgui_widgets.cpp` include them and
  the build fails with confusing stb errors. The 16-file list above is complete.
