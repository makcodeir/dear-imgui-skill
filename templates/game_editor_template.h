// game_editor_template.h - minimal 2D world + editor canvas pattern.
// Distilled from a complete working engine (Dodo, Sep 2026): flat-vector world,
// camera transforms, ImDrawList shape rendering, and the three-panel layout
// (Hierarchy | canvas | Inspector) that keeps all panels visible.
//
// NOT a framework - copy the pieces you need. Compile-check any code you keep
// against your imgui checkout; see scripts/build_headless.sh.
#pragma once
#include "imgui.h"
#include <vector>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace GameEditor {

// ---------------------------------------------------------------- world data
// Plain structs, no pointers between objects. IDs over indices so deletion
// doesn't invalidate references held across the frame.
struct Vec2 { float x = 0, y = 0; };
struct Color { float r = 0.85f, g = 0.35f, b = 0.35f, a = 1.0f; };
enum Shape { SHAPE_RECT = 0, SHAPE_CIRCLE = 1 };

struct GameObject {
    int id = 0;
    char name[64] = "object";
    Vec2 pos{0, 0}, size{64, 64}, vel{0, 0};
    float rot = 0;                  // radians
    int shape = SHAPE_RECT;
    Color color;
    bool visible = true;
};

struct World {
    std::vector<GameObject> objects;
    int next_id = 1;

    int Spawn() {
        GameObject o; o.id = next_id++;
        snprintf(o.name, sizeof(o.name), "object%d", o.id);
        objects.push_back(o); return o.id;
    }
    bool Destroy(int id) {
        for (size_t i = 0; i < objects.size(); i++)
            if (objects[i].id == id) { objects.erase(objects.begin() + i); return true; }
        return false;
    }
    GameObject* Find(int id) {
        for (auto& o : objects) {
            if (o.id == id) return &o;
        }
        return nullptr;
    }
    // const overload REQUIRED: render/gizmo helpers take const World&
    const GameObject* Find(int id) const {
        for (const auto& o : objects) {
            if (o.id == id) return &o;
        }
        return nullptr;
    }
};

// ------------------------------------------------------------------- camera
// World +y is UP (screen y is down): flip the y sign in both transforms.
// Keep BOTH transforms in one place; gizmo math and hit-testing share them.
struct Camera {
    Vec2 pos{0, 0};     // world point at screen center
    float zoom = 1.0f;  // screen px per world unit

    ImVec2 WorldToScreen(Vec2 wp, ImVec2 canvas_sz) const {
        return ImVec2(canvas_sz.x * 0.5f + (wp.x - pos.x) * zoom,
                      canvas_sz.y * 0.5f - (wp.y - pos.y) * zoom);
    }
    Vec2 ScreenToWorld(ImVec2 sp, ImVec2 canvas_sz) const {
        return Vec2{ pos.x + (sp.x - canvas_sz.x * 0.5f) / zoom,
                     pos.y - (sp.y - canvas_sz.y * 0.5f) / zoom };
    }
    // Cursor-anchored zoom: keep the world point under the mouse fixed.
    void ZoomAt(ImVec2 mouse_canvas, float factor, ImVec2 canvas_sz) {
        Vec2 before = ScreenToWorld(mouse_canvas, canvas_sz);
        zoom *= factor;
        if (zoom < 0.05f) zoom = 0.05f;
        if (zoom > 20.0f) zoom = 20.0f;
        Vec2 after = ScreenToWorld(mouse_canvas, canvas_sz);
        pos.x += before.x - after.x;
        pos.y += before.y - after.y;
    }
};

// --------------------------------------------------------------- draw shapes
// Transform helpers return ImVec2 DIRECTLY - your own Vec2 won't implicitly
// convert (hard compile error: "cannot convert 'Vec2' to 'const ImVec2&'").
inline void DrawObject(ImDrawList* dl, const GameObject& o, const Camera& cam,
                       ImVec2 origin, ImVec2 canvas_sz, bool selected) {
    ImU32 fill = ImGui::ColorConvertFloat4ToU32({o.color.r, o.color.g, o.color.b, o.color.a});
    ImU32 outline = selected ? IM_COL32(255, 220, 60, 255) : IM_COL32(0, 0, 0, 90);

    ImVec2 cs = cam.WorldToScreen(o.pos, canvas_sz);
    cs.x += origin.x; cs.y += origin.y;

    float ca = std::cos(o.rot), sa = std::sin(o.rot);
    auto corner = [&](float dx, float dy) -> ImVec2 {   // local offset -> screen
        float rx = dx * ca - dy * sa, ry = dx * sa + dy * ca;
        ImVec2 s = cam.WorldToScreen({o.pos.x + rx, o.pos.y + ry}, canvas_sz);
        return ImVec2{origin.x + s.x, origin.y + s.y};
    };

    if (o.shape == SHAPE_CIRCLE) {
        float r = o.size.x * 0.5f * cam.zoom;
        dl->AddCircleFilled(cs, r, fill, 32);
        if (selected) dl->AddCircle(cs, r + 2.0f, outline, 32, 2.0f);
        dl->AddLine(cs, corner(o.size.x * 0.5f, 0), IM_COL32(0,0,0,120), 2.0f); // rotation cue
    } else {
        float hw = o.size.x * 0.5f, hh = o.size.y * 0.5f;
        ImVec2 p[4] = { corner(-hw,-hh), corner(hw,-hh), corner(hw,hh), corner(-hw,hh) };
        dl->AddConvexPolyFilled(p, 4, fill);
        if (selected) dl->AddPolyline(p, 4, outline, ImDrawFlags_Closed, 2.0f);
        // NOTE: the method is AddPolyline (there is no Polyline).
    }
}

// --------------------------------------------------------- rotated hit-test
// Inverse-rotate the point into object-local space, then test the AABB.
inline int PickObject(const World& w, Vec2 world_pt) {
    for (int i = (int)w.objects.size() - 1; i >= 0; i--) {   // topmost = last drawn
        const GameObject& o = w.objects[i];
        float dx = world_pt.x - o.pos.x, dy = world_pt.y - o.pos.y;
        float ca = std::cos(-o.rot), sa = std::sin(-o.rot);
        float lx = dx * ca - dy * sa, ly = dx * sa + dy * ca;
        float hw = o.size.x * 0.5f + 2.0f, hh = o.size.y * 0.5f + 2.0f;
        if (o.shape == SHAPE_CIRCLE) { if (lx*lx + ly*ly <= hw*hw) return o.id; }
        else if (std::fabs(lx) <= hw && std::fabs(ly) <= hh) return o.id;
    }
    return 0;
}

// ------------------------------------------------------------ canvas child
// Three-panel row recipe (Hierarchy | canvas | Inspector):
//   float row_h = GetContentRegionAvail().y - GetFrameHeightWithSpacing();
//   Hierarchy(); SameLine();
//   Canvas(...);            // width = avail.x - side panels, NEVER (-FLT_MIN, 0)
//   SameLine(); Inspector();
// A fill-width child ({-FLT_MIN, 0}) in a SameLine row eats the whole row and
// silently pushes the right panel off-window. Build + exit 0 won't catch it.
struct CanvasState {
    Camera cam;
    int selected_id = 0;
};

inline void Canvas(const char* label, World& world, CanvasState& st, float width, float height) {
    ImGui::BeginChild(label, ImVec2(width, height), ImGuiChildFlags_Borders);
    ImVec2 c0 = ImGui::GetCursorScreenPos();
    ImVec2 sz = ImGui::GetContentRegionAvail();
    ImVec2 origin{c0.x, c0.y};

    if (ImGui::IsWindowHovered()) {
        // pan: MMB, or Alt+LMB (Alt read via ImGui key API - not raw GLFW codes)
        bool alt = ImGui::IsKeyDown(ImGuiKey_LeftAlt) || ImGui::IsKeyDown(ImGuiKey_RightAlt);
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) ||
            (alt && ImGui::IsMouseDragging(ImGuiMouseButton_Left))) {
            ImVec2 d = ImGui::GetIO().MouseDelta;
            st.cam.pos.x -= d.x / st.cam.zoom;
            st.cam.pos.y += d.y / st.cam.zoom;
        }
        if (ImGui::GetIO().MouseWheel != 0.0f) {
            ImVec2 m = ImGui::GetMousePos();
            st.cam.ZoomAt(ImVec2{m.x - origin.x, m.y - origin.y},
                          ImGui::GetIO().MouseWheel > 0 ? 1.15f : 1.0f / 1.15f, sz);
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !alt) {
            Vec2 wpt = st.cam.ScreenToWorld(
                ImVec2{ImGui::GetIO().MousePos.x - origin.x, ImGui::GetIO().MousePos.y - origin.y}, sz);
            st.selected_id = PickObject(world, wpt);
        }
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    // grid (minor 64 / major 256 world units, world origin marked by axes)
    {
        const float minor = 64.0f * st.cam.zoom, major = 256.0f * st.cam.zoom;
        if (minor >= 8.0f) {
            ImVec2 ctr = st.cam.WorldToScreen({0,0}, sz);
            float cx = origin.x + ctr.x, cy = origin.y + ctr.y;
            for (float x = std::fmod(cx - origin.x, minor); x < sz.x; x += minor)
                dl->AddLine(ImVec2{origin.x + x, c0.y}, ImVec2{origin.x + x, c0.y + sz.y},
                            std::fmod(x + cx - origin.x, major) < 1.0f
                                ? IM_COL32(255,255,255,24) : IM_COL32(255,255,255,12));
            for (float y = std::fmod(cy - origin.y, minor); y < sz.y; y += minor)
                dl->AddLine(ImVec2{c0.x, origin.y + y}, ImVec2{c0.x + sz.x, origin.y + y},
                            std::fmod(y + cy - origin.y, major) < 1.0f
                                ? IM_COL32(255,255,255,24) : IM_COL32(255,255,255,12));
            if (cx >= c0.x && cx <= c0.x + sz.x)
                dl->AddLine(ImVec2{cx, c0.y}, ImVec2{cx, c0.y + sz.y}, IM_COL32(255,90,90,80));
            if (cy >= c0.y && cy <= c0.y + sz.y)
                dl->AddLine(ImVec2{c0.x, cy}, ImVec2{c0.x + sz.x, cy}, IM_COL32(90,255,90,80));
        }
    }
    for (const auto& o : world.objects)
        if (o.visible) DrawObject(dl, o, st.cam, origin, sz, o.id == st.selected_id);

    ImGui::EndChild();
}

// -------------------------------------------------------------- play state
// Snapshot = plain value copy of the vector. Stop restores it (engine edits
// during play are discarded); editor edits made before Play are preserved.
enum PlayState { PLAY_STOPPED = 0, PLAY_RUNNING = 1, PLAY_PAUSED = 2 };

struct PlayController {
    World* world = nullptr;
    PlayState state = PLAY_STOPPED;
    std::vector<GameObject> snapshot;

    void Bind(World* w) { world = w; state = PLAY_STOPPED; snapshot.clear(); }
    void Play()  { if (world && state != PLAY_RUNNING) { snapshot = world->objects; state = PLAY_RUNNING; } }
    void Pause() { if (state == PLAY_RUNNING) state = PLAY_PAUSED;
                   else if (state == PLAY_PAUSED) state = PLAY_RUNNING; }
    void Stop()  { if (!world) return;
                   if (state != PLAY_STOPPED) world->objects = snapshot;
                   state = PLAY_STOPPED; snapshot.clear(); }

    void Update(float dt, Vec2 view_min, Vec2 view_max) {
        if (state != PLAY_RUNNING || !world) return;
        for (auto& o : world->objects) {
            o.pos.x += o.vel.x * dt; o.pos.y += o.vel.y * dt;
            // wrap-around
            if (o.pos.x < view_min.x - o.size.x) o.pos.x = view_max.x + o.size.x;
            if (o.pos.x > view_max.x + o.size.x) o.pos.x = view_min.x - o.size.x;
            if (o.pos.y < view_min.y - o.size.y) o.pos.y = view_max.y + o.size.y;
            if (o.pos.y > view_max.y + o.size.y) o.pos.y = view_min.y - o.size.y;
        }
    }
};

} // namespace GameEditor
