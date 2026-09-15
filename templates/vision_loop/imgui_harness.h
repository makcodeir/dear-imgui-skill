// imgui_harness.h - embedded control API for automated visual verification
// of Dear ImGui apps. Header-only, POSIX-only (AF_UNIX), no deps beyond the
// ImGui public API. Targets ImGui 1.92+ (1.93 tree verified).
//
// DESIGN: the app registers (a) flat model variables and (b) the live rect of
// each interesting widget every frame. A driver connects to the Unix socket,
// sends one command per line, gets one "OK k=v ..." / "ERR reason" line back.
//   * `set`  -> direct model write (deterministic state setup)
//   * `press` -> synthesized io.AddMousePosEvent/AddMouseButtonEvent at the
//     widget's registered center, spread over 3 frames (hover/down/up). This
//     exercises REAL hit-testing and rendering, not a backdoor.
//   * `sync N` -> server-side barrier: response deferred until frame >= N.
//     This is how a capture is taken AFTER the UI state settles.
//
// PLACEMENT CONTRACT (verified against imgui_impl_glfw.cpp): the GLFW backend
// re-queues the OS mouse position inside ImGui_ImplGlfw_NewFrame(), so
// Poll() MUST be called AFTER both backend _NewFrame() calls and BEFORE
// ImGui::NewFrame() for synthetic events to be last in io's queue and win.
//
//   Harness::Init("/tmp/x.sock");                    // after CreateContext
//   while (running) {
//     renderer->_NewFrame(); platform->_NewFrame();
//     Harness::Poll();                               // io events land here
//     ImGui::NewFrame();
//     Harness::BeginActions();
//     ... emit UI; after each testable widget: Harness::Action("inc");
//     ImGui::Render(); present();
//     Harness::EndFrame();                           // frame++, deferred syncs
//   }
//   Harness::Shutdown();                             // unlink socket
//
// Vars: Harness::VarInt("count", &count); etc. - registered once.
// Single client, non-blocking, 3/10 budget: fixed arrays, no threads.

#ifndef IMGUI_HARNESS_H
#define IMGUI_HARNESS_H

#include "imgui.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace Harness
{

enum { kMaxVars = 16, kMaxActions = 16, kMaxLine = 512 };

// ---------------------------------------------------------------- registries
struct Var
{
    const char* Name;
    char Kind; // 'i', 'f', 'b'
    void* Ptr;
};

struct ActionRec
{
    char Name[64];
    float Cx, Cy; // click center
};

static Var g_vars[kMaxVars];
static int g_nvars = 0;
static ActionRec g_actions[kMaxActions];
static int g_nactions = 0;

// ---------------------------------------------------------------- socket state
static int g_listen_fd = -1;
static int g_client_fd = -1;
static char g_sockpath[256] = { 0 };
static char g_line[kMaxLine]; // inbound partial line
static int g_linelen = 0;
static unsigned long g_frame = 0;
static long g_sync_target = -1; // deferred `sync` reply, -1 = none
static bool g_want_quit = false;

// ------------------------------------------------- synthetic-input sequencer
static bool g_has_mouse = false; // sticky injected cursor (re-queued each frame)
static float g_mx = 0.0f, g_my = 0.0f;
static int g_click_phase = 0;    // 0 idle, 1 hover pending, 2 down pending, 3 up pending

// ---------------------------------------------------------------- lifecycle

// Bind + listen. Unlinks a stale socket file first. Returns false on error
// (perror'd). `path` must be shorter than sun_path (~108 chars on Linux).
inline bool Init(const char* path)
{
    if (strlen(path) >= sizeof(((struct sockaddr_un*)0)->sun_path))
    {
        fprintf(stderr, "harness: socket path too long: %s\n", path);
        return false;
    }
    snprintf(g_sockpath, sizeof(g_sockpath), "%s", path);
    unlink(g_sockpath); // stale socket from a crashed run
    g_listen_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (g_listen_fd < 0) { perror("harness: socket"); return false; }
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);
    if (bind(g_listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0)
    {
        perror("harness: bind"); close(g_listen_fd); g_listen_fd = -1; return false;
    }
    if (listen(g_listen_fd, 1) < 0)
    {
        perror("harness: listen"); close(g_listen_fd); g_listen_fd = -1; return false;
    }
    return true;
}

inline void Shutdown()
{
    if (g_client_fd >= 0) { close(g_client_fd); g_client_fd = -1; }
    if (g_listen_fd >= 0) { close(g_listen_fd); g_listen_fd = -1; }
    if (g_sockpath[0]) unlink(g_sockpath);
}

inline bool WantQuit() { return g_want_quit; }
inline unsigned long Frame() { return g_frame; }

// ---------------------------------------------------------------- registration

inline void VarInt(const char* name, int* p)
{ if (g_nvars < kMaxVars) { g_vars[g_nvars++] = Var{ name, 'i', (void*)p }; } }
inline void VarFloat(const char* name, float* p)
{ if (g_nvars < kMaxVars) { g_vars[g_nvars++] = Var{ name, 'f', (void*)p }; } }
inline void VarBool(const char* name, bool* p)
{ if (g_nvars < kMaxVars) { g_vars[g_nvars++] = Var{ name, 'b', (void*)p }; } }

// Call once per frame BEFORE emitting UI (rects are live-layout, rebuilt 1x/frame).
inline void BeginActions() { g_nactions = 0; }

// Register the CURRENT item (call right after emitting the widget).
inline void Action(const char* name)
{
    if (g_nactions >= kMaxActions) return;
    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    ActionRec& s = g_actions[g_nactions++];
    snprintf(s.Name, sizeof(s.Name), "%s", name);
    s.Cx = (a.x + b.x) * 0.5f;
    s.Cy = (a.y + b.y) * 0.5f;
}

// Register an explicit point (canvas targets, custom-drawn buttons).
inline void ActionAt(const char* name, float cx, float cy)
{
    if (g_nactions >= kMaxActions) return;
    ActionRec& s = g_actions[g_nactions++];
    snprintf(s.Name, sizeof(s.Name), "%s", name);
    s.Cx = cx; s.Cy = cy;
}

// ---------------------------------------------------------------- formatting

inline void AppendVar(char* buf, size_t bufsz, const Var& v)
{
    const size_t len = strlen(buf);
    switch (v.Kind)
    {
    case 'i': snprintf(buf + len, bufsz - len, " %.63s=%d", v.Name, *(int*)v.Ptr); break;
    case 'f': snprintf(buf + len, bufsz - len, " %.63s=%.2f", v.Name, *(float*)v.Ptr); break;
    case 'b': snprintf(buf + len, bufsz - len, " %.63s=%d", v.Name, *(bool*)v.Ptr ? 1 : 0); break;
    }
}

inline void AppendFrame(char* buf, size_t bufsz)
{
    snprintf(buf, bufsz, "OK frame=%lu", g_frame);
}

// ---------------------------------------------------------------- dispatch

inline const ActionRec* FindAction(const char* name)
{
    for (int i = 0; i < g_nactions; i++)
        if (strcmp(g_actions[i].Name, name) == 0)
            return &g_actions[i];
    return nullptr;
}

inline Var* FindVar(const char* name)
{
    for (int i = 0; i < g_nvars; i++)
        if (strcmp(g_vars[i].Name, name) == 0)
            return &g_vars[i];
    return nullptr;
}

inline void StartClick(float x, float y, char* reply, size_t n)
{
    g_mx = x; g_my = y; g_has_mouse = true;
    g_click_phase = 1; // hover this frame, down next, up after
    snprintf(reply, n, "OK queued_through=%lu", g_frame + 2);
}

inline void Dispatch(const char* line)
{
    if (g_client_fd < 0) return;
    char reply[kMaxLine];
    reply[0] = 0;
    char cmd[64], a1[64], a2[64];
    int nf = sscanf(line, "%63s %63s %63s", cmd, a1, a2);

    if (strcmp(cmd, "ping") == 0 || strcmp(cmd, "frame") == 0)
        AppendFrame(reply, sizeof(reply));
    else if (strcmp(cmd, "state") == 0)
    {
        AppendFrame(reply, sizeof(reply));
        for (int i = 0; i < g_nvars; i++)
            AppendVar(reply, sizeof(reply), g_vars[i]);
    }
    else if (nf >= 2 && strcmp(cmd, "get") == 0)
    {
        Var* v = FindVar(a1);
        if (!v) snprintf(reply, sizeof(reply), "ERR unknown var %s", a1);
        else { AppendFrame(reply, sizeof(reply)); AppendVar(reply, sizeof(reply), *v); }
    }
    else if (nf >= 3 && strcmp(cmd, "set") == 0)
    {
        Var* v = FindVar(a1);
        if (!v) snprintf(reply, sizeof(reply), "ERR unknown var %s", a1);
        else if (v->Kind == 'i') { *(int*)v->Ptr = atoi(a2); AppendFrame(reply, sizeof(reply)); snprintf(reply + strlen(reply), sizeof(reply) - strlen(reply), " applied_frame=%lu", g_frame + 1); }
        else if (v->Kind == 'f') { *(float*)v->Ptr = (float)atof(a2); AppendFrame(reply, sizeof(reply)); snprintf(reply + strlen(reply), sizeof(reply) - strlen(reply), " applied_frame=%lu", g_frame + 1); }
        else if (v->Kind == 'b') { *(bool*)v->Ptr = (strcmp(a2, "1") == 0 || strcmp(a2, "true") == 0 || strcmp(a2, "on") == 0); AppendFrame(reply, sizeof(reply)); snprintf(reply + strlen(reply), sizeof(reply) - strlen(reply), " applied_frame=%lu", g_frame + 1); }
    }
    else if (nf >= 2 && strcmp(cmd, "press") == 0)
    {
        const ActionRec* s = FindAction(a1);
        if (!s) snprintf(reply, sizeof(reply), "ERR unknown action %s", a1);
        else StartClick(s->Cx, s->Cy, reply, sizeof(reply));
    }
    else if (nf >= 3 && strcmp(cmd, "clickat") == 0)
        StartClick((float)atof(a1), (float)atof(a2), reply, sizeof(reply));
    else if (nf >= 2 && strcmp(cmd, "sync") == 0)
    {
        long target = atol(a1);
        if ((long)g_frame >= target) AppendFrame(reply, sizeof(reply));
        else { g_sync_target = target; return; } // defer; answered in EndFrame
    }
    else if (strcmp(cmd, "register_dump") == 0)
    {
        AppendFrame(reply, sizeof(reply));
        for (int i = 0; i < g_nactions; i++)
        {
            char piece[96];
            snprintf(piece, sizeof(piece), " %.63s=(%.0f,%.0f)", g_actions[i].Name, g_actions[i].Cx, g_actions[i].Cy);
            strncat(reply, piece, sizeof(reply) - strlen(reply) - 1);
        }
    }
    else if (strcmp(cmd, "quit") == 0)
    {
        g_want_quit = true;
        snprintf(reply, sizeof(reply), "OK bye");
    }
    else
        snprintf(reply, sizeof(reply), "ERR unknown command: %.200s", line);

    if (reply[0])
    {
        strcat(reply, "\n");
        ssize_t ignored = write(g_client_fd, reply, strlen(reply));
        (void)ignored;
    }
}

// ---------------------------------------------------------------- per-frame calls

// Call AFTER backend _NewFrame()s, BEFORE ImGui::NewFrame().
inline void Poll()
{
    ImGuiIO& io = ImGui::GetIO();

    // 1) accept / read (non-blocking)
    if (g_listen_fd >= 0)
    {
        struct pollfd p[2];
        int nfds = 1;
        p[0].fd = g_listen_fd; p[0].events = POLLIN; p[0].revents = 0;
        if (g_client_fd >= 0)
        {
            p[1].fd = g_client_fd; p[1].events = POLLIN; p[1].revents = 0;
            nfds = 2;
        }
        if (poll(p, nfds, 0) > 0)
        {
            if (p[0].revents & POLLIN)
            {
                int fd = accept(g_listen_fd, nullptr, nullptr);
                if (fd >= 0)
                {
                    if (g_client_fd >= 0) close(g_client_fd); // replace old client
                    g_client_fd = fd;
                    fcntl(fd, F_SETFL, O_NONBLOCK);
                    g_linelen = 0;
                }
            }
            if (nfds == 2 && (p[1].revents & (POLLIN | POLLHUP)))
            {
                char buf[256];
                ssize_t got = read(g_client_fd, buf, sizeof(buf));
                if (got <= 0)
                {
                    close(g_client_fd); g_client_fd = -1; g_linelen = 0; g_sync_target = -1;
                }
                else
                {
                    for (ssize_t i = 0; i < got; i++)
                    {
                        if (buf[i] == '\n')
                        {
                            g_line[g_linelen] = 0;
                            if (g_linelen > 0 && g_client_fd >= 0)
                                Dispatch(g_line);
                            g_linelen = 0;
                        }
                        else if (g_linelen < kMaxLine - 1)
                            g_line[g_linelen++] = buf[i];
                    }
                }
            }
        }
    }

    // 2) inject synthetic input LAST (after backend events, before NewFrame)
    if (g_has_mouse)
        io.AddMousePosEvent(g_mx, g_my);
    switch (g_click_phase)
    {
    case 1: g_click_phase = 2; break;                         // hover frame done
    case 2: io.AddMouseButtonEvent(0, true);  g_click_phase = 3; break;
    case 3: io.AddMouseButtonEvent(0, false); g_click_phase = 0; break;
    default: break;
    }
}

// Call once per loop iteration after present.
inline void EndFrame()
{
    g_frame++;
    if (g_sync_target >= 0 && (long)g_frame >= g_sync_target && g_client_fd >= 0)
    {
        g_sync_target = -1;
        char reply[64];
        AppendFrame(reply, sizeof(reply));
        strcat(reply, "\n");
        ssize_t ignored = write(g_client_fd, reply, strlen(reply));
        (void)ignored;
    }
}

} // namespace Harness

#endif // IMGUI_HARNESS_H
