// game_editor_template_check.cpp - compile+link check for game_editor_template.h.
// Exercises: camera transforms, DrawObject, PickObject, PlayController snapshot,
// Canvas child (inside a real ImGui frame). Run with no window: pure ImGui math.
#include "templates/game_editor_template.h"
#include <stdio.h>
#include <stdint.h>

int main() {
    using namespace GameEditor;
    int fails = 0;
    #define CHECK(c, m) do { if (c) printf("  [PASS] %s\n", m); else { printf("  [FAIL] %s\n", m); fails++; } } while (0)

    // camera roundtrip: screen center is the camera origin, +y up
    Camera cam;
    ImVec2 sz{800, 600};
    Vec2 w = cam.ScreenToWorld(cam.WorldToScreen({123, -45}, sz), sz);
    CHECK(w.x == 123 && w.y == -45, "world->screen->world roundtrip");
    ImVec2 up = cam.WorldToScreen({0, 100}, sz);      // +y world = up on screen
    CHECK(up.y < sz.y * 0.5f, "world +y maps upward on screen");

    // zoom keeps the cursor point fixed
    Camera z; z.pos = {10, 10};
    Vec2 anchor = z.ScreenToWorld(ImVec2{650, 200}, sz);
    z.ZoomAt(ImVec2{650, 200}, 2.0f, sz);
    Vec2 after = z.ScreenToWorld(ImVec2{650, 200}, sz);
    CHECK(std::fabs(after.x - anchor.x) < 0.001f && std::fabs(after.y - anchor.y) < 0.001f,
          "cursor-anchored zoom keeps world point fixed");

    // pick: topmost wins, rotation respected
    World world;
    int a = world.Spawn(); world.Find(a)->pos = {0, 0}; world.Find(a)->size = {100, 100};
    int b = world.Spawn(); world.Find(b)->pos = {0, 0}; world.Find(b)->size = {100, 100};
    CHECK(PickObject(world, {0, 0}) == b, "pick returns topmost (last drawn)");
    world.Find(b)->rot = 0.785398f; // 45 deg: AABB corner leaves rotated bounds
    CHECK(PickObject(world, {60, 60}) == 0, "rotated object hit-test uses local space");

    // play controller: snapshot/restore
    PlayController pc; pc.Bind(&world);
    world.Find(b)->vel = {50, 0};
    float x0 = world.Find(b)->pos.x;
    pc.Play(); pc.Update(1.0f, {-1000, -1000}, {1000, 1000});
    CHECK(world.Find(b)->pos.x > x0 + 49, "play integrates vel*dt");
    pc.Stop();
    CHECK(world.Find(b)->pos.x == x0, "stop restores snapshot");
    CHECK(pc.state == PLAY_STOPPED, "stop resets state");

    // full frame: Canvas child + DrawObject inside a real ImGui context
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = sz; io.DeltaTime = 1.0f / 60.0f;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    unsigned char* px; int tw, th;
    io.Fonts->GetTexDataAsRGBA32(&px, &tw, &th);
    for (int f = 0; f < 3; f++) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        Canvas("canvas", world, *(new CanvasState{cam, b}), 500, 300);
        ImGui::Render();
        // acknowledge texture requests like a real renderer would (headless)
        ImDrawData* dd = ImGui::GetDrawData();
        if (dd && dd->Textures)
            for (ImTextureData* t : *dd->Textures)
                if (t->Status != ImTextureStatus_OK) {
                    t->SetTexID((ImTextureID)(intptr_t)(t->UniqueID + 1));
                    t->SetStatus(ImTextureStatus_OK);
                }
    }
    ImGui::DestroyContext();
    printf(fails ? "\n%d FAILURES\n" : "\nTEMPLATE CHECK PASSED\n", fails);
    return fails ? 1 : 0;
}
