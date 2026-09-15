# Common GUI Construction Patterns (analysis of `examples/`)

Ten examples were read and compared to extract the invariant construction
pattern. The point: **every example is the same skeleton with a different
platform/renderer pair**. Once you know the skeleton you can build any of them.

> Scope note: this file is about the *constructing* the GUI, not about how the
> platform/renderer glue works internally. Treat the backend columns below as
> labels for "which example compiles where", and stay in the core `ImGui::`
> API when building UI.

## The 10 examples selected

Chosen for representativeness across platforms and API styles:

| # | Example | Platform backend | Renderer backend | Builds on Linux |
|---|---------|------------------|------------------|-----------------|
| 1 | `example_null` | `imgui_impl_null` | `imgui_impl_null` | yes (headless) |
| 2 | `example_glfw_opengl3` | `imgui_impl_glfw` | `imgui_impl_opengl3` | yes |
| 3 | `example_glfw_opengl2` | `imgui_impl_glfw` | `imgui_impl_opengl2` | yes |
| 4 | `example_sdl2_opengl3` | `imgui_impl_sdl2` | `imgui_impl_opengl3` | yes |
| 5 | `example_sdl2_sdlrenderer2` | `imgui_impl_sdl2` | `imgui_impl_sdlrenderer2` | yes |
| 6 | `example_sdl3_sdlrenderer3` | `imgui_impl_sdl3` | `imgui_impl_sdlrenderer3` | yes |
| 7 | `example_glut_opengl2` | `imgui_impl_glut` | `imgui_impl_opengl2` | yes |
| 8 | `example_win32_directx11` | `imgui_impl_win32` | `imgui_impl_dx11` | no (Windows) |
| 9 | `example_android_opengl3` | `imgui_impl_android` | `imgui_impl_opengl3` (ES3) | no (Android) |
| 10 | `example_glfw_vulkan` | `imgui_impl_glfw` | `imgui_impl_vulkan` | yes |
| — | `example_apple_metal` | `imgui_impl_osx` | `imgui_impl_metal` | no (macOS) |
| — | `example_win32_vulkan` | `imgui_impl_win32` | `imgui_impl_vulkan` | no (Windows) |

(12 listed; the first ten are the primary set, the last two were checked to
confirm the pattern holds.)

## Pattern A — the invariant frame lifecycle

Verified line-by-line; identical ordering in all of them:

```
init:     glfwInit / SDL_Init / Win32 ...
          IMGUI_CHECKVERSION();
          ImGui::CreateContext();
          io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; (+Gamepad)
          ImGui::StyleColorsDark();
          <Platform>_Init(...);
          <Renderer>_Init(...);
loop:     poll events
          <Renderer>_NewFrame();      // renderer FIRST
          <Platform>_NewFrame();      // platform SECOND
          ImGui::NewFrame();
          ... UI submission ...
          ImGui::Render();
          <Renderer>_RenderDrawData(ImGui::GetDrawData());
          swap buffers
shutdown: <Renderer>_Shutdown();
          <Platform>_Shutdown();
          ImGui::DestroyContext();
```

`example_null` is special: it embeds `imgui_impl_null.cpp` directly and calls
`ImGui_ImplNullPlatform_NewFrame()` + `ImGui_ImplNullRender_NewFrame()`, but the
NewFrame order still holds. It is the quickest compile test of the core files.

## Pattern B — the canonical "Hello, world!" window

Every GUI example contains byte-identical UI code (only the surrounding backend
differs). This is the idiomatic minimum:

```cpp
{
    static float f = 0.0f;
    static int counter = 0;
    ImGui::Begin("Hello, world!");
    ImGui::Text("This is some useful text.");
    ImGui::Checkbox("Demo Window", &show_demo_window);
    ImGui::SliderFloat("float", &f, 0.0f, 1.0f);
    ImGui::ColorEdit3("clear color", (float*)&clear_color);
    if (ImGui::Button("Button")) counter++;
    ImGui::SameLine();
    ImGui::Text("counter = %d", counter);
    ImGui::Text("Application average %.3f ms/frame (%.1f FPS)",
                1000.0f / io.Framerate, io.Framerate);
    ImGui::End();
}
```

Takeaways:
- All mutable widget state is `static` (or in app state), declared in the loop.
- `Begin/End` wrap everything; no per-widget object allocation.
- Text uses printf formatting; buttons return `bool`.
- The first thing every example shows is **`ImGui::ShowDemoWindow()`** — the demo
  is the real documentation for the end-user API.

## Pattern C — the DPI/scaling block (modern examples)

`example_glfw_opengl3` and the other updated ones add:

```cpp
float main_scale = ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor());
GLFWwindow* window = glfwCreateWindow((int)(1280*main_scale), (int)(800*main_scale), ...);
...
ImGuiStyle& style = ImGui::GetStyle();
style.ScaleAllSizes(main_scale);   // bake style scale (call once)
style.FontScaleDpi = main_scale;   // initial font scale
```
Older examples omit this. For DPI-correct apps, include it.

## Pattern D — main menu bar structure

From `imgui_demo.cpp::ShowExampleAppMainMenuBar` and the `Layout` example:

```cpp
if (ImGui::BeginMainMenuBar())          // standalone bar at top of viewport
{
    if (ImGui::BeginMenu("File")) { /* MenuItems */ ImGui::EndMenu(); }
    ImGui::EndMainMenuBar();
}
```
or, per-window, pass `ImGuiWindowFlags_MenuBar` to `Begin()` and call
`BeginMenuBar()`/`EndMenuBar()` inside. `MenuItem("Redo","Ctrl+Y",false,false)`
is the idiom for a **disabled** item.

## Pattern E — two-pane layout + property table

From `imgui_demo.cpp::ShowExampleAppLayout` / `ShowExampleAppPropertyEditor`:

```cpp
ImGui::BeginChild("left pane", ImVec2(150, 0),
                  ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
for (int i = 0; i < 100; i++) {
    char label[128]; sprintf(label, "MyObject %d", i);
    if (ImGui::Selectable(label, selected == i, ImGuiSelectableFlags_SelectOnNav))
        selected = i;
}
ImGui::EndChild();
ImGui::SameLine();
ImGui::BeginChild("item view", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()));
// ... detail pane; the negative height reserves room for a row below ...
ImGui::EndChild();
```
Property editors use a 2-column table (`TableSetupColumn` + `TableNextColumn`)
to align labels and values without pixel math.

## Cross-cutting rules distilled

1. **Immediate mode**: the UI is re-emitted each frame; there is no retained
   widget object. State lives in `static`s or your own struct.
2. **Backends are a pair**: one Platform + one Renderer. Swapping them is the
   only difference between examples.
3. **`ShowDemoWindow()` is the reference**: the examples deliberately don't
   re-implement everything — they point you at the demo.
4. **`Begin/End` pairing discipline** is the core skill (see the pairing matrix
   in `api-quickref.md`).
5. **All examples compile with `-Wall -Wformat`** by upstream CI; the updated
   Makefiles add `-Wextra -Wpedantic` under `WITH_EXTRA_WARNINGS=1`. Your code
   should too.

## Where to look in the demo for X

- Windows/flags: `Demo > Windows`
- Widgets (all of them): `Demo > Widgets`
- Layout & grouping: `Demo > Layout & Scrolling`
- Popups/modals: `Demo > Popups & Modal windows`
- Tables: `Demo > Tables & Columns`
- Menus/menu bar: `Demo > Examples > Main menu bar`, `Layout`
- Custom drawing: `Demo > Examples > Custom rendering`
- IDs/ID stack: `ImGui::ShowIDStackToolWindow(bool* p_open = NULL)`
- Style/colours: `Demo > Style Editor`
