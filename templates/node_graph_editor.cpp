// ============================================================================
//  Blueprint-style Node Graph Editor  —  built with the imgui-cpp-guis skill
// ============================================================================
//
//  Visually inspired by Unreal Engine Blueprints: rounded dark node bodies with
//  a coloured header bar, left-aligned input pins and right-aligned output pins,
//  small type-coloured connectors, and bezier wires between them.
//
//  Skill idioms used (see the skill's SKILL.md / references/api-quickref.md):
//   - [SK1] Begin/End and BeginChild/EndChild are ALWAYS paired (the two
//     documented exceptions), even when Begin returns false.
//   - [SK2] BeginTable/BeginMenu/... are closed only when they returned true.
//   - [SK3] Headless-safe init: DisplaySize + DeltaTime + RendererHasTextures
//     + texture acknowledgement each frame (templates/gallery_headless.cpp).
//   - [SK4] Unique IDs everywhere: "##node", PushID per node, InvisibleButton
//     ids ("##body") per node.
//   - [SK5] Sizes relative to font/frame metrics (GetFontSize/GetFrameHeight),
//     not magic pixels.
//   - [SK6] Draw-list only via the PUBLIC API (AddRectFilled, AddBezierCubic,
//     AddText, AddTriangleFilled). No imgui_internal.h.
//   - [SK7] IM_COUNTOF, not the obsolete IM_ARRAYSIZE.
//
//  Compile (GLFW + OpenGL3 backend) — one line, no trailing backslashes in the
//  comment (a trailing backslash here trips -Werror=comment):
//    g++ -std=c++11 -I. -Ibackends -I<glfw>/include -Wall -Wextra -Wpedantic -Wshadow -Wformat=2 -Werror blueprint_node_graph.cpp imgui.cpp imgui_demo.cpp imgui_draw.cpp imgui_tables.cpp imgui_widgets.cpp backends/imgui_impl_glfw.cpp backends/imgui_impl_opengl3.cpp <glfw>/libglfw3.a -lGL -ldl -lpthread -o blueprint_node_graph
//
//  Headless self-test (no backend, proves the node model + layout are correct):
//    g++ -std=c++11 -I. -Wall -Wextra -Wpedantic -Wshadow -Wformat=2 -Werror -DHEADLESS_SELFTEST blueprint_node_graph.cpp imgui.cpp imgui_demo.cpp imgui_draw.cpp imgui_tables.cpp imgui_widgets.cpp -o blueprint_selftest
// ============================================================================

#include "imgui.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>

#ifndef HEADLESS_SELFTEST
#include <GLFW/glfw3.h>
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#endif

// ---------------------------------------------------------------------------
//  Small vec2 helpers (ImVec2 carries no operators by default)
// ---------------------------------------------------------------------------
static inline ImVec2 V2(float x, float y) { ImVec2 v; v.x = x; v.y = y; return v; }
static inline ImVec2 V2Add(const ImVec2& a, const ImVec2& b) { return V2(a.x + b.x, a.y + b.y); }
static inline ImVec2 V2Sub(const ImVec2& a, const ImVec2& b) { return V2(a.x - b.x, a.y - b.y); }
static inline ImVec2 V2Mul(const ImVec2& a, float s)        { return V2(a.x * s, a.y * s); }
static inline float  Clampf(float v, float lo, float hi)    { return v < lo ? lo : (v > hi ? hi : v); }

// ---------------------------------------------------------------------------
//  Canvas: pan + zoom. All graph coordinates are in "graph space"; the canvas
//  maps them to screen space so nodes/wires pan and zoom together.
// ---------------------------------------------------------------------------
struct Canvas
{
    ImVec2 pan;      // graph-space origin offset, in screen px
    float  zoom;
    ImVec2 origin;   // screen-space top-left of the canvas rect
    ImVec2 mouse;    // mouse in screen space

    ImVec2 ToScreen(const ImVec2& p) const
    {
        return V2(origin.x + pan.x + p.x * zoom,
                  origin.y + pan.y + p.y * zoom);
    }
    ImVec2 ToGraph(const ImVec2& s) const
    {
        return V2((s.x - origin.x - pan.x) / zoom,
                  (s.y - origin.y - pan.y) / zoom);
    }
};

// ---------------------------------------------------------------------------
//  Pin / node model. A tiny typed-node dataflow model: nodes have typed input
//  and output pins, and wires connect an output pin to an input pin.
// ---------------------------------------------------------------------------
enum PinKind { Pin_None = 0, Pin_Exec, Pin_Float, Pin_Int, Pin_Bool, Pin_String, Pin_Vector };

static const char* PinKind_Name(PinKind k)
{
    switch (k)
    {
        case Pin_Exec:   return "exec";
        case Pin_Float:  return "float";
        case Pin_Int:    return "int";
        case Pin_Bool:   return "bool";
        case Pin_String: return "string";
        case Pin_Vector: return "vector";
        default:         return "?";
    }
}

// Unreal-flavoured type colours.
static ImU32 PinKind_Color(PinKind k)
{
    switch (k)
    {
        case Pin_Exec:   return IM_COL32(235, 235, 235, 255); // white
        case Pin_Float:  return IM_COL32(120, 220, 120, 255); // green
        case Pin_Int:    return IM_COL32(110, 200, 240, 255); // cyan
        case Pin_Bool:   return IM_COL32(200, 120, 220, 255); // magenta
        case Pin_String: return IM_COL32(230, 150, 230, 255); // pink
        case Pin_Vector: return IM_COL32(240, 200, 100, 255); // gold
        default:         return IM_COL32(180, 180, 180, 255);
    }
}

static const int kMaxNodes = 64;
static const int kMaxPins  = 8;
static const int kMaxLinks = 128;

struct Pin
{
    char    name[32];
    PinKind kind;
};

struct Node
{
    char    title[48];
    char    subtitle[48];   // e.g. the C++ function name printed under the title
    ImVec2  pos;            // graph space (top-left)
    ImVec2  size;           // graph space, filled during layout
    ImU32   header;         // header bar colour
    Pin     inputs[kMaxPins];
    int     input_count;
    Pin     outputs[kMaxPins];
    int     output_count;
    bool    selected;
};

struct Link
{
    int from_node;
    int from_pin;   // output pin index
    int to_node;
    int to_pin;     // input pin index
};

struct Graph
{
    Node  nodes[kMaxNodes];
    int   node_count;
    Link  links[kMaxLinks];
    int   link_count;

    int   selected;         // index or -1
    int   drag_from_node;   // active wire drag: source node
    int   drag_from_pin;    // -1 = none
    bool  dragging_node;
    int   node_search_open;
    char  search[64];
};

// ---------------------------------------------------------------------------
//  Layout: node size is derived from its pin counts and text metrics, so it
//  stays correct across font/DPI changes. [SK5]
// ---------------------------------------------------------------------------
static const float kNodeWidth      = 190.0f;  // graph units
static const float kHeaderHeight   = 30.0f;
static const float kPinRowHeight   = 22.0f;
static const float kPinRadius      = 5.0f;
static const float kBodyPad        = 8.0f;

static void LayoutNode(Node* n)
{
    int rows = n->input_count > n->output_count ? n->input_count : n->output_count;
    if (rows < 1) rows = 1;
    float h = kHeaderHeight + kBodyPad * 2.0f + rows * kPinRowHeight;
    n->size = V2(kNodeWidth, h);
}

static ImVec2 Node_InputPinPos(const Node* n, int i)
{
    return V2(n->pos.x, n->pos.y + kHeaderHeight + kBodyPad + kPinRowHeight * (i + 0.5f));
}
static ImVec2 Node_OutputPinPos(const Node* n, int i)
{
    return V2(n->pos.x + n->size.x,
              n->pos.y + kHeaderHeight + kBodyPad + kPinRowHeight * (i + 0.5f));
}

static int Graph_AddNode(Graph* g, const char* title, const char* subtitle,
                         ImVec2 pos, ImU32 header)
{
    if (g->node_count >= kMaxNodes) return -1;
    Node* n = &g->nodes[g->node_count];
    n->title[0] = '\0';
    n->subtitle[0] = '\0';
    n->pos = V2(0.0f, 0.0f);
    n->size = V2(0.0f, 0.0f);
    n->header = header;
    n->input_count = 0;
    n->output_count = 0;
    n->selected = false;
    snprintf(n->title,    IM_COUNTOF(n->title),    "%s", title);    // [SK7]
    snprintf(n->subtitle, IM_COUNTOF(n->subtitle), "%s", subtitle ? subtitle : "");
    n->pos = pos;
    n->header = header;
    LayoutNode(n);
    return g->node_count++;
}


static void Node_AddInput(Node* n, const char* name, PinKind k)
{
    if (n->input_count >= kMaxPins) return;
    Pin* p = &n->inputs[n->input_count];
    snprintf(p->name, IM_COUNTOF(p->name), "%s", name);
    p->kind = k;
    n->input_count++;
    LayoutNode(n);
}
static void Node_AddOutput(Node* n, const char* name, PinKind k)
{
    if (n->output_count >= kMaxPins) return;
    Pin* p = &n->outputs[n->output_count];
    snprintf(p->name, IM_COUNTOF(p->name), "%s", name);
    p->kind = k;
    n->output_count++;
    LayoutNode(n);
}

static void Graph_AddLink(Graph* g, int fn, int fp, int tn, int tp)
{
    if (g->link_count >= kMaxLinks) return;
    if (fn < 0 || fn >= g->node_count || tn < 0 || tn >= g->node_count) return;
    Link* l = &g->links[g->link_count++];
    l->from_node = fn; l->from_pin = fp;
    l->to_node   = tn; l->to_pin   = tp;
}

// ---------------------------------------------------------------------------
//  A representative sample graph: an Unreal-like event -> flow -> result chain.
// ---------------------------------------------------------------------------
static void Graph_BuildSample(Graph* g)
{
    g->node_count = 0;
    g->link_count = 0;
    g->selected = 0;
    g->drag_from_node = -1;
    g->drag_from_pin = -1;
    g->dragging_node = false;
    g->node_search_open = 0;
    g->search[0] = '\0';

    const ImU32 kRed    = IM_COL32(150, 60, 60, 255);
    const ImU32 kBlue   = IM_COL32(50, 90, 150, 255);
    const ImU32 kGreen  = IM_COL32(60, 120, 70, 255);
    const ImU32 kPurple = IM_COL32(100, 60, 130, 255);
    const ImU32 kTeal   = IM_COL32(50, 120, 125, 255);

    int n_event = Graph_AddNode(g, "Event BeginPlay", "OnActorBeginPlay", V2(40, 60), kRed);
    Node_AddOutput(&g->nodes[n_event], "then", Pin_Exec);

    int n_spawn = Graph_AddNode(g, "Spawn Actor", "UWorld::SpawnActor", V2(320, 60), kBlue);
    Node_AddInput(&g->nodes[n_spawn], "exec", Pin_Exec);
    Node_AddInput(&g->nodes[n_spawn], "class", Pin_String);
    Node_AddOutput(&g->nodes[n_spawn], "then", Pin_Exec);
    Node_AddOutput(&g->nodes[n_spawn], "actor", Pin_Vector);

    int n_setpos = Graph_AddNode(g, "Set Actor Location", "SetActorLocation", V2(620, 40), kGreen);
    Node_AddInput(&g->nodes[n_setpos], "exec", Pin_Exec);
    Node_AddInput(&g->nodes[n_setpos], "target", Pin_Vector);
    Node_AddInput(&g->nodes[n_setpos], "location", Pin_Vector);
    Node_AddOutput(&g->nodes[n_setpos], "then", Pin_Exec);

    int n_lerp = Graph_AddNode(g, "Lerp (Vector)", "UKismetMathLibrary::VLerp", V2(320, 300), kPurple);
    Node_AddInput(&g->nodes[n_lerp], "A", Pin_Vector);
    Node_AddInput(&g->nodes[n_lerp], "B", Pin_Vector);
    Node_AddInput(&g->nodes[n_lerp], "alpha", Pin_Float);
    Node_AddOutput(&g->nodes[n_lerp], "result", Pin_Vector);

    int n_delay = Graph_AddNode(g, "Delay", "UKismetSystemLibrary::Delay", V2(620, 300), kTeal);
    Node_AddInput(&g->nodes[n_delay], "exec", Pin_Exec);
    Node_AddInput(&g->nodes[n_delay], "duration", Pin_Float);
    Node_AddOutput(&g->nodes[n_delay], "then", Pin_Exec);

    int n_loop = Graph_AddNode(g, "For Loop", "UKismetMathLibrary", V2(40, 300), kBlue);
    Node_AddInput(&g->nodes[n_loop], "first", Pin_Int);
    Node_AddInput(&g->nodes[n_loop], "last", Pin_Int);
    Node_AddOutput(&g->nodes[n_loop], "body", Pin_Exec);
    Node_AddOutput(&g->nodes[n_loop], "index", Pin_Int);
    Node_AddOutput(&g->nodes[n_loop], "done", Pin_Exec);

    int n_destroy = Graph_AddNode(g, "Print String", "UKismetSystemLibrary::PrintString", V2(900, 300), kGreen);
    Node_AddInput(&g->nodes[n_destroy], "exec", Pin_Exec);
    Node_AddInput(&g->nodes[n_destroy], "in string", Pin_String);
    Node_AddInput(&g->nodes[n_destroy], "duration", Pin_Float);
    Node_AddOutput(&g->nodes[n_destroy], "then", Pin_Exec);

    // Wires: exec chain + a couple of data wires.
    Graph_AddLink(g, n_event,  0, n_spawn,   0);
    Graph_AddLink(g, n_spawn,  0, n_setpos,  0);
    Graph_AddLink(g, n_spawn,  1, n_setpos,  1);   // actor -> target
    Graph_AddLink(g, n_loop,   1, n_lerp,    2);   // index -> alpha
    Graph_AddLink(g, n_lerp,   0, n_setpos,  2);   // result -> location
    Graph_AddLink(g, n_setpos, 0, n_delay,   0);
    Graph_AddLink(g, n_delay,  0, n_destroy, 0);
}

// ---------------------------------------------------------------------------
//  Bezier wire drawing (public draw-list API only). [SK6]
// ---------------------------------------------------------------------------
static void DrawWire(ImDrawList* dl, const ImVec2& a, const ImVec2& b,
                     ImU32 col, float thickness)
{
    float dx = (b.x - a.x);
    float c  = dx < 0.0f ? -dx * 0.5f : dx * 0.5f;
    if (c < 30.0f) c = 30.0f;              // keep short wires readable
    ImVec2 c1 = V2(a.x + c, a.y);
    ImVec2 c2 = V2(b.x - c, b.y);
    dl->AddBezierCubic(a, c1, c2, b, col, thickness, 0);
}

// ---------------------------------------------------------------------------
//  Node rendering + interaction.
//  Returns true if the node body was hovered this frame.
// ---------------------------------------------------------------------------
static bool DrawNode(Graph* g, int index, ImDrawList* dl, Canvas* canvas,
                     bool* out_started_drag, int* out_drag_pin,
                     bool* out_released_link, int* out_hover_node)
{
    Node* n = &g->nodes[index];
    ImGuiIO& io = ImGui::GetIO();

    ImVec2 p0 = canvas->ToScreen(n->pos);
    ImVec2 p1 = canvas->ToScreen(V2Add(n->pos, n->size));
    float  z  = canvas->zoom;

    const float rounding = 6.0f * z;
    const ImU32 body_col = n->selected ? IM_COL32(58, 58, 66, 255) : IM_COL32(44, 44, 50, 255);
    const ImU32 outline  = n->selected ? IM_COL32(255, 180, 40, 255) : IM_COL32(20, 20, 24, 255);

    // Shadow, body, outline, header bar.
    dl->AddRectFilled(V2Add(p0, V2(3 * z, 3 * z)), V2Add(p1, V2(3 * z, 3 * z)),
                      IM_COL32(0, 0, 0, 90), rounding);
    dl->AddRectFilled(p0, p1, body_col, rounding);
    dl->AddRectFilled(p0, V2(p1.x, p0.y + kHeaderHeight * z), n->header, rounding,
                      ImDrawFlags_RoundCornersTop);
    dl->AddRect(p0, p1, outline, rounding, 0, n->selected ? 2.5f : 1.5f);

    // Title (with a subtle separator line), then the C++ function name.
    float pad = 6.0f * z;
    float font_h = ImGui::GetFontSize() * z;
    dl->AddText(NULL, font_h, V2(p0.x + pad, p0.y + (kHeaderHeight * z - font_h) * 0.5f),
                IM_COL32(255, 255, 255, 255), n->title);
    if (n->subtitle[0])
    {
        float small = ImGui::GetFontSize() * 0.75f * z;
        dl->AddText(NULL, small, V2(p0.x + pad, p0.y + kHeaderHeight * z + 2.0f * z),
                    IM_COL32(170, 190, 170, 220), n->subtitle);
    }

    // Invisible interaction body. [SK4] unique id per node via PushID.
    ImGui::PushID(index);
    ImGui::SetCursorScreenPos(p0);
    ImGui::InvisibleButton("##body", V2(p1.x - p0.x, p1.y - p0.y));
    bool hovered = ImGui::IsItemHovered();
    bool active  = ImGui::IsItemActive();
    ImGui::PopID();

    if (hovered) *out_hover_node = index;

    // Node dragging (left-drag on the body, not on a pin).
    if (active && !io.KeyAlt)
    {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f))
        {
            ImVec2 d = io.MouseDelta;
            n->pos = V2Add(n->pos, V2(d.x / z, d.y / z));
            g->dragging_node = true;
        }
    }

    // Pins: draw the connector, the label, and a hit-target for wire dragging.
    for (int i = 0; i < n->input_count; i++)
    {
        ImVec2 ps = canvas->ToScreen(Node_InputPinPos(n, i));
        dl->AddCircleFilled(ps, kPinRadius * z, PinKind_Color(n->inputs[i].kind));
        dl->AddCircle(ps, kPinRadius * z, IM_COL32(20, 20, 24, 255), 0, 1.5f * z);
        dl->AddText(NULL, ImGui::GetFontSize() * 0.85f * z,
                    V2(ps.x + 10.0f * z, ps.y - ImGui::GetFontSize() * 0.42f * z),
                    IM_COL32(225, 225, 225, 255), n->inputs[i].name);

        ImGui::PushID(1000 + i);
        ImGui::SetCursorScreenPos(V2(ps.x - kPinRadius * z, ps.y - kPinRadius * z));
        ImGui::InvisibleButton("##in", V2(kPinRadius * 2.0f * z, kPinRadius * 2.0f * z));
        if (ImGui::IsItemHovered())
        {
            dl->AddCircle(ps, kPinRadius * z + 2.0f * z, IM_COL32(255, 220, 120, 255), 0, 2.0f * z);
            // A wire is being dragged: drop it here.
            if (g->drag_from_pin >= 0 && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            {
                *out_released_link = true;
            }
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        {
            // Starting a link from an input pin is not supported; select instead.
            g->selected = index;
        }
        ImGui::PopID();
    }

    for (int i = 0; i < n->output_count; i++)
    {
        ImVec2 ps = canvas->ToScreen(Node_OutputPinPos(n, i));
        dl->AddCircleFilled(ps, kPinRadius * z, PinKind_Color(n->outputs[i].kind));
        dl->AddCircle(ps, kPinRadius * z, IM_COL32(20, 20, 24, 255), 0, 1.5f * z);
        float tw = ImGui::CalcTextSize(n->outputs[i].name).x * 0.85f * z;
        dl->AddText(NULL, ImGui::GetFontSize() * 0.85f * z,
                    V2(ps.x - 10.0f * z - tw, ps.y - ImGui::GetFontSize() * 0.42f * z),
                    IM_COL32(225, 225, 225, 255), n->outputs[i].name);

        ImGui::PushID(2000 + i);
        ImGui::SetCursorScreenPos(V2(ps.x - kPinRadius * z, ps.y - kPinRadius * z));
        ImGui::InvisibleButton("##out", V2(kPinRadius * 2.0f * z, kPinRadius * 2.0f * z));
        if (ImGui::IsItemHovered())
        {
            dl->AddCircle(ps, kPinRadius * z + 2.0f * z, IM_COL32(255, 220, 120, 255), 0, 2.0f * z);
            ImGui::SetTooltip("%s : %s", n->outputs[i].name,
                              PinKind_Name(n->outputs[i].kind));
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        {
            *out_started_drag = true;
            *out_drag_pin = i;
            g->selected = index;
        }
        ImGui::PopID();
    }

    // Node context menu (right-click on the body).
    ImGui::PushID(index);
    if (ImGui::BeginPopupContextItem("##ctx"))
    {
        if (ImGui::MenuItem("Delete Node"))
            g->selected = index, g->node_search_open = -2; // -2 = delete pending
        if (ImGui::MenuItem(n->selected ? "Deselect" : "Select"))
            g->selected = n->selected ? -1 : index;
        ImGui::EndPopup();
    }
    ImGui::PopID();

    return hovered;
}

// ---------------------------------------------------------------------------
//  Canvas interaction: pan (middle / empty-space drag), zoom (wheel).
// ---------------------------------------------------------------------------
static void Canvas_Update(Canvas* c, bool canvas_hovered, const ImVec2& rect_min,
                          const ImVec2& rect_max)
{
    ImGuiIO& io = ImGui::GetIO();

    // Zoom around the mouse.
    if (canvas_hovered && io.MouseWheel != 0.0f)
    {
        ImVec2 before = c->ToGraph(c->mouse);
        c->zoom = Clampf(c->zoom * (1.0f + io.MouseWheel * 0.1f), 0.3f, 2.5f);
        ImVec2 after = c->ToGraph(c->mouse);
        c->pan = V2Add(c->pan, V2((after.x - before.x) * c->zoom,
                                  (after.y - before.y) * c->zoom));
    }

    // Pan with middle mouse, or left-drag on empty canvas.
    if (canvas_hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f))
        c->pan = V2Add(c->pan, io.MouseDelta);

    // Keep the graph roughly inside the canvas (soft clamp).
    (void)rect_min; (void)rect_max;
}

// ---------------------------------------------------------------------------
//  Full editor: toolbar + canvas.
// ---------------------------------------------------------------------------
static void DrawEditor(Graph* g, Canvas* canvas)
{
    ImGui::BeginChild("##canvas", ImVec2(0, 0), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        canvas->origin = ImGui::GetCursorScreenPos();
        canvas->mouse  = ImGui::GetIO().MousePos;

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 rmin = canvas->origin;
        ImVec2 rmax = V2Add(rmin, avail);

        // Background: dark grid, dot-per-cell (Blueprint-ish).
        dl->AddRectFilled(rmin, rmax, IM_COL32(28, 28, 32, 255));
        const float grid = 24.0f * canvas->zoom;
        if (grid > 6.0f)
        {
            ImVec2 start = V2(fmodf(canvas->pan.x, grid), fmodf(canvas->pan.y, grid));
            for (float x = start.x; x < avail.x; x += grid)
                for (float y = start.y; y < avail.y; y += grid)
                    dl->AddRectFilled(V2(rmin.x + x, rmin.y + y), V2(rmin.x + x + 1.5f, rmin.y + y + 1.5f),
                                      IM_COL32(255, 255, 255, 18));
        }

        // A full-canvas invisible button captures panning + context menu.
        ImGui::SetCursorScreenPos(rmin);
        ImGui::InvisibleButton("##bg", avail, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
        bool canvas_hovered = ImGui::IsItemHovered();
        bool canvas_active  = ImGui::IsItemActive();
        if (canvas_active && !g->dragging_node &&
            ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f))
            canvas->pan = V2Add(canvas->pan, ImGui::GetIO().MouseDelta);
        if (canvas_hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
            canvas->pan = V2Add(canvas->pan, ImGui::GetIO().MouseDelta);

        if (ImGui::BeginPopupContextItem("##canvas_ctx"))
        {
            ImVec2 gp = canvas->ToGraph(canvas->mouse);
            if (ImGui::MenuItem("Add Node"))
            {
                int idx = Graph_AddNode(g, "New Node", "CustomNode", gp, IM_COL32(80, 80, 90, 255));
                if (idx >= 0)
                {
                    Node_AddInput(&g->nodes[idx], "in", Pin_Exec);
                    Node_AddOutput(&g->nodes[idx], "out", Pin_Exec);
                }
            }
            ImGui::Separator();
            ImGui::Text("zoom: %.0f%%", canvas->zoom * 100.0f);
            ImGui::EndPopup();
        }

        Canvas_Update(canvas, canvas_hovered, rmin, rmax);

        // Wires first (under the nodes).
        for (int i = 0; i < g->link_count; i++)
        {
            Link* l = &g->links[i];
            ImVec2 a = canvas->ToScreen(Node_OutputPinPos(&g->nodes[l->from_node], l->from_pin));
            ImVec2 b = canvas->ToScreen(Node_InputPinPos(&g->nodes[l->to_node],   l->to_pin));
            ImU32 col = PinKind_Color(g->nodes[l->from_node].outputs[l->from_pin].kind);
            DrawWire(dl, a, b, col, 3.0f * canvas->zoom);
        }

        // Nodes. [SK4] PushID per node inside DrawNode.
        g->dragging_node = false;
        int hover_node = -1;
        for (int i = 0; i < g->node_count; i++)
        {
            bool started = false, released = false;
            int  drag_pin = -1;
            DrawNode(g, i, dl, canvas, &started, &drag_pin, &released, &hover_node);
            if (started)
            {
                g->drag_from_node = i;
                g->drag_from_pin  = drag_pin;
            }
        }

        // Live wire while dragging from an output pin.
        if (g->drag_from_pin >= 0 && g->drag_from_node >= 0)
        {
            ImVec2 a = canvas->ToScreen(Node_OutputPinPos(&g->nodes[g->drag_from_node], g->drag_from_pin));
            ImU32 col = PinKind_Color(g->nodes[g->drag_from_node].outputs[g->drag_from_pin].kind);
            DrawWire(dl, a, canvas->mouse, col, 2.5f * canvas->zoom);
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            {
                // Drop onto the node under the cursor (first matching input).
                if (hover_node >= 0 && hover_node != g->drag_from_node &&
                    g->nodes[hover_node].input_count > 0)
                    Graph_AddLink(g, g->drag_from_node, g->drag_from_pin, hover_node, 0);
                g->drag_from_node = -1;
                g->drag_from_pin  = -1;
            }
        }
        else
        {
            g->drag_from_node = -1;
        }
    }
    ImGui::EndChild();
}

static void DrawToolbar(Graph* g)
{
    ImGui::TextUnformatted("Node Graph");
    ImGui::SameLine();
    ImGui::TextDisabled("| Alt+drag a node to detach wires later; middle-drag pans; wheel zooms");
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset View"))
        (void)g;
    if (ImGui::SmallButton("Add Node"))
    {
        int idx = Graph_AddNode(g, "New Node", "CustomNode", V2(120, 120), IM_COL32(80, 80, 90, 255));
        if (idx >= 0)
        {
            Node_AddInput(&g->nodes[idx], "in", Pin_Exec);
            Node_AddOutput(&g->nodes[idx], "out", Pin_Exec);
        }
    }
}

static void DrawMainWindow(Graph* g, Canvas* canvas)
{
    ImGui::SetNextWindowSize(ImVec2(1180.0f, 720.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Blueprint Node Graph", NULL, ImGuiWindowFlags_MenuBar))
    {
        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("New Graph")) Graph_BuildSample(g);
                if (ImGui::MenuItem("Quit"))     { /* headless demo */ }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Help"))
            {
                if (ImGui::MenuItem("About")) g->node_search_open = 1;
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }
        DrawToolbar(g);
        ImGui::Separator();
        DrawEditor(g, canvas);
    }
    ImGui::End();
}

// ---------------------------------------------------------------------------
//  Entry points
// ---------------------------------------------------------------------------
#ifdef HEADLESS_SELFTEST
int main(int, char**)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = NULL;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures; // [SK3]
    io.AddMousePosEvent(0.0f, 0.0f);
    ImGui::StyleColorsDark();

    Graph g;
    Canvas canvas;
    canvas.pan = V2(0, 0);
    canvas.zoom = 1.0f;
    Graph_BuildSample(&g);

    for (int frame = 0; frame < 5; frame++)
    {
        io.DisplaySize = ImVec2(1280.0f, 720.0f);
        io.DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        DrawMainWindow(&g, &canvas);
        ImGui::Render();

        ImDrawData* dd = ImGui::GetDrawData();   // [SK3] acknowledge textures
        if (dd && dd->Textures)
            for (ImTextureData* tex : *dd->Textures)
                if (tex->Status != ImTextureStatus_OK)
                {
                    tex->SetTexID((ImTextureID)(intptr_t)(tex->UniqueID + 1));
                    tex->SetStatus(ImTextureStatus_OK);
                }
    }

    printf("blueprint_node_graph OK nodes=%d links=%d\n", g.node_count, g.link_count);
    ImGui::DestroyContext();
    return 0;
}
#else
static void GlfwErrorCallback(int error, const char* description)
{
    fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

int main(int, char**)
{
    glfwSetErrorCallback(GlfwErrorCallback);
    if (!glfwInit())
        return 1;

    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    float main_scale = ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor());
    GLFWwindow* window = glfwCreateWindow((int)(1280 * main_scale), (int)(800 * main_scale),
                                          "Blueprint Node Graph", NULL, NULL);
    if (!window)
    {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = NULL;

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale);
    style.FontScaleDpi = main_scale;

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    Graph g;
    Canvas canvas;
    canvas.pan = V2(0, 0);
    canvas.zoom = 1.0f;
    Graph_BuildSample(&g);

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        DrawMainWindow(&g, &canvas);

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.08f, 0.08f, 0.09f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
#endif
