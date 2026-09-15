// platformer_render.cpp — ImGui render harness: procedural ImDrawList game
// canvas, HUD overlay, AI verification panel, log console. GUI build only.
#include "platformer_core.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>

#include <cstdio>

// ---------------------------------------------------------------------------
// Camera transform: world px -> canvas-local px.
// ---------------------------------------------------------------------------
static ImVec2 WorldToScreen(Vec2 world, float cam_x, ImVec2 origin, float scale)
{
    return ImVec2(origin.x + (world.x - cam_x) * scale,
                  origin.y + world.y * scale);
}

namespace { // .cpp-private render state

struct Rect { ImVec2 mn, mx; };

constexpr float kCanvasScale = 2.0f; // world px -> canvas px
constexpr int   kViewTilesX = 20;   // matches engine camera view width

ImU32 ColTile(Tile t)
{
    switch (t) {
        case Tile::Ground:   return IM_COL32(146, 96, 57, 255);
        case Tile::Brick:    return IM_COL32(172, 108, 66, 255);
        case Tile::Question: return IM_COL32(255, 178, 28, 255);
        case Tile::Used:     return IM_COL32(120, 110, 100, 255);
        case Tile::PipeTL: case Tile::PipeTR:
        case Tile::PipeBL: case Tile::PipeBR: return IM_COL32(32, 160, 64, 255);
        case Tile::Lava:     return IM_COL32(240, 64, 32, 255);
        case Tile::FlagPole: return IM_COL32(180, 220, 180, 255);
        case Tile::FlagBase: return IM_COL32(120, 200, 120, 255);
        default: return 0;
    }
}

void DrawBevelTile(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 base)
{
    dl->AddRectFilled(mn, mx, base);
    // top highlight / bottom shade (3D bevel)
    float b = 3.f;
    dl->AddRectFilled(mn, ImVec2(mx.x, mn.y + b), ImU32(0x50FFFFFF) | (base & 0xFF000000), 0.f, 0);
    dl->AddRectFilled(ImVec2(mn.x, mx.y - b), mx, ImU32(0x50000000) | (base & 0xFF000000), 0.f, 0);
    dl->AddRect(mn, mx, IM_COL32(20, 14, 8, 200), 0.f, 0, 1.5f);
}

void DrawCoin(ImDrawList* dl, ImVec2 c, float t)
{
    float r = 6.f + 1.2f * sinf(t);
    dl->AddCircleFilled(c, r, IM_COL32(255, 214, 64, 255));
    dl->AddCircle(c, r, IM_COL32(140, 100, 10, 255), 0, 1.5f);
}

void DrawGoomba(ImDrawList* dl, ImVec2 mn, ImVec2 mx, bool flat)
{
    if (flat) { // stomped: flat pancake
        dl->AddRectFilled(ImVec2(mn.x, mx.y - 5.f), mx, IM_COL32(120, 70, 40, 255));
        return;
    }
    float cy = (mn.y + mx.y) * 0.5f;
    ImVec2 cc{(mn.x + mx.x) * 0.5f, cy};
    // body: dome via PathArcTo
    dl->PathClear();
    dl->PathArcTo(cc, (mx.x - mn.x) * 0.5f, float(M_PI), 0.f, 12);
    dl->PathLineTo(ImVec2(mx.x, mx.y));
    dl->PathLineTo(ImVec2(mn.x, mx.y));
    dl->PathFillConvex(IM_COL32(139, 84, 42, 255));
    // feet
    dl->AddRectFilled(ImVec2(mn.x, mx.y - 3.f), ImVec2(mn.x + 7.f, mx.y), IM_COL32(40, 26, 12, 255));
    dl->AddRectFilled(ImVec2(mx.x - 7.f, mx.y - 3.f), ImVec2(mx.x, mx.y), IM_COL32(40, 26, 12, 255));
    // eyes with pupil offset by walk direction (vel sign known by caller)
    dl->AddCircleFilled(ImVec2(mn.x + 6.f, cy - 2.f), 3.5f, IM_COL32(255, 255, 255, 255));
    dl->AddCircleFilled(ImVec2(mx.x - 6.f, cy - 2.f), 3.5f, IM_COL32(255, 255, 255, 255));
    dl->AddCircleFilled(ImVec2(mn.x + 7.f, cy - 2.f), 1.6f, IM_COL32(0, 0, 0, 255));
    dl->AddCircleFilled(ImVec2(mx.x - 5.f, cy - 2.f), 1.6f, IM_COL32(0, 0, 0, 255));
}

void DrawPlayer(ImDrawList* dl, ImVec2 mn, ImVec2 mx, bool facing_right, bool invuln)
{
    ImU32 suit = invuln ? IM_COL32(255, 120, 120, 220) : IM_COL32(228, 52, 60, 255);
    // cap
    dl->AddRectFilled(mn, ImVec2(mx.x, mn.y + (mx.y - mn.y) * 0.30f), IM_COL32(200, 40, 40, 255));
    // face
    dl->AddRectFilled(ImVec2(mn.x + 2.f, mn.y + (mx.y - mn.y) * 0.30f),
                      ImVec2(mx.x - 2.f, mn.y + (mx.y - mn.y) * 0.62f),
                      IM_COL32(255, 214, 170, 255));
    // eye (direction indicator)
    float ey = mn.y + (mx.y - mn.y) * 0.44f;
    dl->AddCircleFilled(ImVec2(facing_right ? mx.x - 7.f : mn.x + 7.f, ey), 2.f, IM_COL32(0, 0, 0, 255));
    // overalls/body
    dl->AddRectFilled(ImVec2(mn.x, mn.y + (mx.y - mn.y) * 0.62f), mx, suit);
    // buttons
    dl->AddCircleFilled(ImVec2(mn.x + 6.f, mn.y + (mx.y - mn.y) * 0.75f), 1.8f, IM_COL32(255, 214, 64, 255));
    dl->AddCircleFilled(ImVec2(mx.x - 6.f, mn.y + (mx.y - mn.y) * 0.75f), 1.8f, IM_COL32(255, 214, 64, 255));
}

struct Harness : RenderHarness {
    GLFWwindow* window = nullptr;
    float bob_clock = 0.f;
};

} // namespace

RenderHarness* RenderInit()
{
    Harness* h = new Harness();
    if (!glfwInit()) { delete h; return nullptr; }
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
    h->window = glfwCreateWindow(1280, 720, "ImGui Platformer — AI Verification Harness", NULL, NULL);
    if (!h->window) { glfwTerminate(); delete h; return nullptr; }
    glfwMakeContextCurrent(h->window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // don't pollute the skill dir
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(h->window, true);
    ImGui_ImplOpenGL3_Init("#version 130");
    return h;
}

bool RenderPump(RenderHarness* rh, FrameInput& out_live)
{
    Harness* h = (Harness*)rh;
    if (glfwWindowShouldClose(h->window)) return false;
    glfwPollEvents();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    out_live = FrameInput{};
    out_live.left  = ImGui::IsKeyDown(ImGuiKey_LeftArrow) || ImGui::IsKeyDown(ImGuiKey_A);
    out_live.right = ImGui::IsKeyDown(ImGuiKey_RightArrow) || ImGui::IsKeyDown(ImGuiKey_D);
    out_live.jump  = ImGui::IsKeyDown(ImGuiKey_Space) || ImGui::IsKeyDown(ImGuiKey_W) || ImGui::IsKeyDown(ImGuiKey_UpArrow);
    out_live.run   = ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift);
    return true;
}

static void DrawGameCanvas(const GameState& gs, Harness* h)
{
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetMainViewport()->WorkSize);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(92, 148, 252, 255)); // sky
    ImGui::Begin("##game", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 wp = ImGui::GetWindowPos();
    float scale = kCanvasScale;
    float cam_x = gs.camera.x;

    // clouds (parallax decor, deterministic from camera)
    for (int i = 0; i < 6; i++) {
        float cx = fmodf(i * 517.f - cam_x * 0.25f, 1300.f);
        if (cx < -100.f) cx += 1300.f;
        ImVec2 c{wp.x + cx, wp.y + 40.f + 30.f * float(i % 3)};
        dl->AddCircleFilled(c, 22.f, IM_COL32(240, 244, 255, 200));
        dl->AddCircleFilled(ImVec2(c.x + 20.f, c.y + 6.f), 16.f, IM_COL32(240, 244, 255, 200));
        dl->AddCircleFilled(ImVec2(c.x - 20.f, c.y + 6.f), 16.f, IM_COL32(240, 244, 255, 200));
    }

    // visible tile range
    int tx0 = int(cam_x / gs.cfg.tile) - 1;
    int tx1 = tx0 + kViewTilesX + 3;
    for (int ty = 0; ty < gs.cfg.map_h; ty++) {
        for (int tx = tx0; tx <= tx1; tx++) {
            Tile t = gs.level.at(tx, ty);
            if (t == Tile::Empty) continue;
            ImVec2 mn = WorldToScreen({float(tx) * gs.cfg.tile, float(ty) * gs.cfg.tile},
                                      cam_x, wp, scale);
            ImVec2 mx{mn.x + gs.cfg.tile * scale, mn.y + gs.cfg.tile * scale};
            if (t == Tile::Question) { // pulse glow
                float pulse = 0.5f + 0.5f * sinf(h->bob_clock * 4.f + tx);
                ImU32 c = IM_COL32(255, 178, 28, 255);
                DrawBevelTile(dl, mn, mx, c);
                ImVec2 cc{(mn.x + mx.x) * .5f, (mn.y + mx.y) * .5f};
                dl->AddText(ImVec2(cc.x - 7.f, cc.y - 9.f),
                            IM_COL32(80, 40, 0, 255), "?");
                if (pulse > 0.8f)
                    dl->AddRect(mn, mx, IM_COL32(255, 255, 180, 120), 0.f, 0, 2.f);
            } else if (t == Tile::FlagPole) {
                dl->AddRectFilled(ImVec2(mn.x + mn.x * 0.f + (mx.x - mn.x) * 0.45f, mn.y),
                                  ImVec2(mn.x + (mx.x - mn.x) * 0.55f, mx.y),
                                  IM_COL32(160, 200, 160, 255));
            } else {
                DrawBevelTile(dl, mn, mx, ColTile(t));
            }
        }
    }

    // coins
    for (const Coin& c : gs.level.coins) {
        if (c.collected) continue;
        // animate bob using a phase derived from position (deterministic)
        float phase = c.pos.x * 0.05f + h->bob_clock * 3.f;
        DrawCoin(dl, WorldToScreen({c.pos.x, c.pos.y + 2.5f * sinf(phase)}, cam_x, wp, scale), phase);
    }

    // flag (cloth on the pole at flag_pos)
    {
        ImVec2 base = WorldToScreen({gs.level.flag_pos.x + gs.cfg.tile * 0.5f,
                                     gs.level.flag_pos.y}, cam_x, wp, scale);
        float pole_top = base.y - 8.f * gs.cfg.tile * scale * 0.5f;
        dl->AddTriangleFilled(ImVec2(base.x, pole_top + 12.f),
                              ImVec2(base.x, pole_top + 44.f),
                              ImVec2(base.x - 40.f, pole_top + 28.f),
                              IM_COL32(60, 220, 120, 255));
    }

    // enemies
    for (const Enemy& e : gs.level.enemies) {
        if (!e.alive && e.dead_timer > 0.5f) continue;
        ImVec2 mn = WorldToScreen(e.pos, cam_x, wp, scale);
        ImVec2 mx{mn.x + 20.f * scale, mn.y + 20.f * scale};
        DrawGoomba(dl, mn, mx, !e.alive);
    }

    // player (always drawn; blink handled by alpha inside DrawPlayer)
    {
        ImVec2 mn = WorldToScreen(gs.pos, cam_x, wp, scale);
        ImVec2 mx{mn.x + gs.cfg.player_size.x * scale, mn.y + gs.cfg.player_size.y * scale};
        DrawPlayer(dl, mn, mx, gs.vel.x >= 0.f, gs.invuln > 0);
    }

    // HUD (world-anchored overlay on top of the canvas)
    {
        char line[128];
        snprintf(line, sizeof line, "SCORE %06d   COINS x%02d   HP %d   TIME %d",
                 gs.score, gs.coins, gs.health,
                 std::max(0, gs.cfg.time_limit_frames - gs.frame) / 60);
        dl->AddText(ImVec2(wp.x + 16.f, wp.y + 12.f), IM_COL32(255, 255, 255, 255), line);
        snprintf(line, sizeof line, "x=%.0f vx=%.0f %s", gs.pos.x, gs.vel.x,
                 gs.on_ground ? "GROUND" : "AIR");
        dl->AddText(ImVec2(wp.x + 16.f, wp.y + 30.f), IM_COL32(255, 255, 255, 180), line);
    }

    // outcome banner
    if (gs.won) {
        dl->AddRectFilled(wp, ImVec2(wp.x + 1280.f, wp.y + 90.f), IM_COL32(20, 120, 40, 180));
        dl->AddText(ImVec2(wp.x + 500.f, wp.y + 30.f), IM_COL32(255, 255, 255, 255),
                    "COURSE CLEAR!  (REACHED_GOAL)");
    } else if (!gs.alive) {
        dl->AddRectFilled(wp, ImVec2(wp.x + 1280.f, wp.y + 90.f), IM_COL32(120, 20, 20, 180));
        dl->AddText(ImVec2(wp.x + 500.f, wp.y + 30.f), IM_COL32(255, 255, 255, 255),
                    gs.outcome == FrameOutcome::FALLEN_INTO_PIT ? "FELL IN THE PIT..." : "DEFEATED...");
    }

    ImGui::End();
    ImGui::PopStyleColor();
}

static void DrawAiPanel(bool replay_active, const std::vector<std::string>& log_lines)
{
    ImGui::SetNextWindowPos(ImVec2(0, ImGui::GetMainViewport()->WorkSize.y - 300.f));
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetMainViewport()->WorkSize.x, 300.f));
    ImGui::SetNextWindowBgAlpha(0.92f);
    ImGui::Begin("AI Verification", nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextUnformatted(replay_active ? "status: REPLAY RUNNING (baseline input script)"
                                         : "status: LIVE / idle");
    ImGui::Separator();
    ImGui::BeginChild("log", ImVec2(0, 0), ImGuiChildFlags_Borders);
    for (const std::string& l : log_lines) ImGui::TextUnformatted(l.c_str());
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.f)
        ImGui::SetScrollHereY(1.f);
    ImGui::EndChild();
    ImGui::End();
}

void RenderFrame(RenderHarness* rh, const GameState& gs, bool replay_active,
                 const std::vector<std::string>& log_lines)
{
    DrawGameCanvas(gs, (Harness*)rh);
    DrawAiPanel(replay_active, log_lines);
    ((Harness*)rh)->bob_clock += ImGui::GetIO().DeltaTime;

    ImGui::Render();
    int dw, dh;
    glfwGetFramebufferSize(((Harness*)rh)->window, &dw, &dh);
    glViewport(0, 0, dw, dh);
    glClearColor(0.06f, 0.06f, 0.09f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(((Harness*)rh)->window);
}

void RenderShutdown(RenderHarness* rh)
{
    Harness* h = (Harness*)rh;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(h->window);
    glfwTerminate();
    delete h;
}
