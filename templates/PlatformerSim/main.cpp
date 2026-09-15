// main.cpp — GUI app: platformer viewport + autonomous AI verification cycle.
//
// Cycle (state machine, runs automatically at startup):
//   Replay  : feeds the pre-planned baseline FrameInput macro to the sim,
//             one input per rendered frame (spectator-speed, ~60 Hz).
//   Analyze : serializes full-run telemetry snapshots + event log to JSON,
//             POSTs it to an OpenAI-compatible endpoint, parses the verdict,
//             logs PLAYABLE / NOT PLAYABLE into the console panel.
//   Done    : hands the controller to the live player. [R] restarts the cycle.
#include "platformer_core.h"
#include "imgui.h"
#include <cstdio>
#include <string>
#include <vector>

namespace {

struct App {
    EngineConfig cfg;
    GameState gs;
    std::vector<FrameInput> baseline;
    size_t replay_frame = 0;
    bool replay_active = false;

    enum class Phase { Replay, Analyze, Done } phase = Phase::Replay;
    int run_index = 1;

    std::string api_key, model = "gpt-4o-mini", base_url;
    AiVerdict verdict;
    bool verdict_ready = false;

    std::vector<std::string> log;
    bool prev_r = false;

    void push(const std::string& s) {
        char b[24];
        snprintf(b, sizeof b, "[R%d f%04d] ", run_index, gs.frame);
        log.push_back(b + s);
        if (log.size() > 200) log.erase(log.begin());
    }
};

void RunAutoCycle(App& a)
{
    // --- Replay -> Analyze transition ---
    a.gs.reset(a.cfg);
    a.replay_frame = 0;
    a.replay_active = true;
    a.verdict_ready = false;
    a.phase = App::Phase::Replay;
    a.push("baseline input script loaded (" +
           std::to_string(a.baseline.size()) + " frames) — replaying...");
}

void FinishReplay(App& a)
{
    a.replay_active = false;
    char b[160];
    snprintf(b, sizeof b, "replay done: frames=%d outcome=%s score=%d coins=%d hp=%d",
             a.gs.frame, OutcomeStr(a.gs.outcome), a.gs.score, a.gs.coins, a.gs.health);
    a.push(b);
    for (const auto& e : a.gs.events)
        a.push(std::string("  event @f") + std::to_string(e.frame) + ": " + OutcomeStr(e.kind));
    a.phase = App::Phase::Analyze;
}

void RunAnalysis(App& a)
{
    a.phase = App::Phase::Done;
    a.push("querying AI auditor (" + a.model + ")...");

    // Assemble the full payload: snapshots + event log.
    // (Re-use the deterministic driver for a pristine full-run serialization.)
    ReplayResult r = RunReplay(a.cfg, a.baseline, 30, true);
    std::string payload = "{\"run\":" + std::to_string(a.run_index) +
                          ",\"snapshots\":" + r.snapshots +
                          ",\"events\":" + r.events + "}";
    if (FILE* f = fopen("/tmp/platformer_telemetry.json", "wb")) {
        fwrite(payload.data(), 1, payload.size(), f);
        fclose(f);
        a.push("telemetry dumped to /tmp/platformer_telemetry.json (" +
               std::to_string(payload.size()) + " bytes)");
    }

    a.verdict = QueryAiPlayability(payload, a.api_key, a.model, a.base_url);
    if (!a.verdict.ok) {
        a.push("AI query FAILED: " + a.verdict.error);
        a.push("TEST ASSERTION: INCONCLUSIVE (no verdict)");
        return;
    }
    a.verdict_ready = true;
    char b[128];
    snprintf(b, sizeof b, "AI verdict: is_playable=%s confidence=%.2f",
             a.verdict.is_playable ? "true" : "false", a.verdict.confidence);
    a.push(b);
    for (const std::string& s : a.verdict.bottlenecks)
        a.push("  bottleneck: " + s);
    for (const std::string& s : a.verdict.suggested_inputs)
        a.push("  suggested input: " + s);

    // --- automated assertion ---
    if (a.verdict.is_playable && r.won) {
        a.push("TEST ASSERTION: PASS — level mechanically playable & beatable");
    } else if (!a.verdict.is_playable) {
        a.push("TEST ASSERTION: FAIL — auditor reports the level NOT playable");
        a.push("action: adjust level geometry / input script and re-run [R]");
    } else {
        a.push("TEST ASSERTION: FAIL — auditor says playable but the scripted " 
               "run did not reach the flag (outcome " +
               std::string(OutcomeStr(r.outcome)) + ")");
    }
}

} // namespace

int main()
{
    App a;
    a.baseline = BuildBaselineInputs();
    a.gs.reset(a.cfg);
    a.api_key = ""; // QueryAiPlayability falls back to $OPENAI_API_KEY
    a.push("ImGui Platformer — AI Verification Harness");
    a.push("controls: arrows/WASD move, Space jump, Shift run, [R] restart cycle");
    RunAutoCycle(a);

    RenderHarness* rh = RenderInit();
    if (!rh) {
        fprintf(stderr, "FATAL: RenderInit failed (GLFW/GL)\n");
        return 1;
    }

    while (true) {
        FrameInput live;
        if (!RenderPump(rh, live)) break;

        // restart cycle on R edge
        bool r_now = ImGui::IsKeyPressed(ImGuiKey_R, false);
        if (r_now && !a.prev_r) {
            a.run_index++;
            RunAutoCycle(a);
        }
        a.prev_r = r_now;

        if (a.phase == App::Phase::Replay) {
            if (a.replay_frame < a.baseline.size()) {
                FrameInput fi = a.baseline[a.replay_frame++];
                FrameOutcome o = StepFrame(a.gs, fi);
                if (o == FrameOutcome::REACHED_GOAL || !a.gs.alive ||
                    a.gs.frame >= a.cfg.time_limit_frames)
                    FinishReplay(a);
            } else {
                FinishReplay(a);
            }
        } else if (a.phase == App::Phase::Analyze) {
            RunAnalysis(a); // blocking HTTP call (one-time, logged)
        } else {
            // Done: live play with the keyboard
            if (a.gs.won || !a.gs.alive) {
                // frozen until R restarts; still render
            } else {
                StepFrame(a.gs, live);
            }
        }

        RenderFrame(rh, a.gs, a.replay_active, a.log);
    }

    RenderShutdown(rh);
    return 0;
}
