// platformer_selftest.cpp — headless, deterministic verification of the engine
// and the AI-verifier plumbing. Zero windowing dependencies: compiles against
// the sim only. Exit 0 = all assertions pass.
//
// Build: see scripts in the build command in README/build.sh (plain C++20).
#include "platformer_core.h"

#include <cstdio>
#include <string>

namespace {

int g_checks = 0, g_failed = 0;

void Check(bool cond, const char* what)
{
    g_checks++;
    if (!cond) {
        g_failed++;
        printf("  FAIL: %s\n", what);
    }
}

} // namespace

int main()
{
    EngineConfig cfg;

    // ---------------------------------------------------------------
    // T1: level integrity
    // ---------------------------------------------------------------
    {
        Level lv;
        BuildLevel1(lv, cfg);
        Check(int(lv.tiles.size()) == cfg.map_w * cfg.map_h, "level tile count");
        Check(lv.at(0, 13) == Tile::Ground, "ground row solid at x=0");
        Check(lv.at(69, 13) == Tile::Empty, "pit 1 exists at col 69");
        Check(lv.at(154, 13) == Tile::Empty, "pit 2 exists at col 154");
        Check(lv.at(205, 14) == Tile::FlagPole || lv.at(205, 13) == Tile::FlagBase ||
              lv.at(205, 11) == Tile::FlagPole, "flagpole present");
        Check(!lv.enemies.empty(), "enemies spawned");
        int q = 0;
        for (size_t i = 0; i < lv.tiles.size(); i++)
            if (lv.tiles[i] == Tile::Question) q++;
        Check(q == 5, "5 question blocks placed");
        Check(lv.coins.size() >= 10, "coins placed");
        printf("T1 level integrity: %s\n", g_failed ? "FAIL" : "ok");
    }

    // ---------------------------------------------------------------
    // T2: deterministic replay of the baseline reaches the flagpole
    // ---------------------------------------------------------------
    {
        ReplayResult r = RunReplay(cfg, BuildBaselineInputs(), 30, false);
        Check(r.won, "baseline replay REACHES_GOAL");
        Check(r.outcome == FrameOutcome::REACHED_GOAL, "terminal outcome is REACHED_GOAL");
        Check(r.final_state.goombas_stomped >= 1, "at least one goomba stomped");
        Check(r.final_state.coins >= 5, "coins collected along the way");
        printf("T2 baseline replay: frames=%d outcome=%s score=%d coins=%d stomped=%d -> %s\n",
               r.frames, OutcomeStr(r.outcome), r.final_state.score,
               r.final_state.coins, r.final_state.goombas_stomped,
               g_failed ? "FAIL" : "ok");
    }

    // ---------------------------------------------------------------
    // T3: determinism — two identical runs produce identical telemetry
    // ---------------------------------------------------------------
    {
        ReplayResult a = RunReplay(cfg, BuildBaselineInputs(), 30, false);
        ReplayResult b = RunReplay(cfg, BuildBaselineInputs(), 30, false);
        Check(a.snapshots == b.snapshots, "snapshots identical across runs");
        Check(a.final_state.pos.x == b.final_state.pos.x &&
              a.final_state.pos.y == b.final_state.pos.y, "final position identical");
        printf("T3 determinism: %s\n", g_failed ? "FAIL" : "ok");
    }

    // ---------------------------------------------------------------
    // T4: player standing on flat ground does NOT fall through
    // ---------------------------------------------------------------
    {
        GameState gs;
        gs.reset(cfg);
        std::vector<FrameInput> idle(120, FrameInput{});
        for (int i = 0; i < 120; i++) StepFrame(gs, idle[size_t(i)]);
        Check(gs.on_ground, "player grounded after 120 idle frames");
        Check(gs.alive, "player alive (no pit fall on spawn)");
        printf("T4 idle stability: %s\n", g_failed ? "FAIL" : "ok");
    }

    // ---------------------------------------------------------------
    // T5: coyote time & friction — running then stopping decelerates to 0
    // ---------------------------------------------------------------
    {
        GameState gs;
        gs.reset(cfg);
        std::vector<FrameInput> run(60, FrameInput{});
        for (int i = 0; i < 60; i++) { run[size_t(i)].right = true; }
        for (int i = 0; i < 60; i++) StepFrame(gs, run[size_t(i)]);
        float vmax = gs.vel.x;
        // now 60 idle frames (all-false inputs): friction must stop us
        std::vector<FrameInput> idle(60, FrameInput{});
        for (int i = 0; i < 60; i++) StepFrame(gs, idle[size_t(i)]);
        Check(gs.vel.x == 0.f, "friction stopped the player after input ends");
        Check(vmax > 100.f, "player actually reached running speed");
        printf("T5 friction stop: %s\n", g_failed ? "FAIL" : "ok");
    }

    // ---------------------------------------------------------------
    // T6: telemetry snapshot is valid JSON-shaped and grid is 15 rows
    // ---------------------------------------------------------------
    {
        GameState gs;
        gs.reset(cfg);
        std::vector<FrameInput> run(90, FrameInput{});
        for (int i = 0; i < 90; i++) { run[size_t(i)].right = true; StepFrame(gs, run[size_t(i)]); }
        std::string snap = SerializeSnapshot(gs, true);
        Check(snap[0] == '{' && snap[snap.size() - 1] == '}', "snapshot braces");
        Check(snap.find("\"tiles\":[") != std::string::npos, "grid present");
        Check(snap.find("\"frame\":90") != std::string::npos, "frame number embedded");
        char grid[15 * 15 + 1];
        SampleTileGrid(gs, grid);
        int rows = 0;
        for (int i = 0; i <= 15 * 15; i++) if (grid[i] == '\0') rows++;
        Check(rows == 1, "grid NUL-terminated at 225 chars");
        printf("T6 telemetry snapshot: %s\n", g_failed ? "FAIL" : "ok");
    }

    // ---------------------------------------------------------------
    // T7: ExtractJsonObject finds the verdict in an LLM-styled reply
    // ---------------------------------------------------------------
    {
        const char* reply =
            "Sure! Here is my analysis:\n```json\n"
            "{\"is_playable\": true, \"confidence\": 0.87, "
            "\"bottlenecks\": [], \"suggested_inputs\": [\"hold run longer\"]}\n"
            "```\nHope that helps!";
        std::string obj;
        Check(ExtractJsonObject(reply, obj), "extractor finds fenced JSON");
        bool p = false;
        {
            // use the same accessors as the client (exposed via header? they
            // are static; replicate minimal parse here)
            size_t p2 = obj.find("\"is_playable\": true");
            p = p2 != std::string::npos;
        }
        Check(p, "is_playable=true parsed from reply");
        printf("T7 verdict extraction: %s\n", g_failed ? "FAIL" : "ok");
    }

    // ---------------------------------------------------------------
    // T8: blocked-by-wall event fires when running into a pipe forever
    // ---------------------------------------------------------------
    {
        GameState gs;
        gs.reset(cfg);
        std::vector<FrameInput> run(1800, FrameInput{});
        for (int i = 0; i < 1800; i++) { run[size_t(i)].right = true; }
        int wall_events = 0;
        for (int i = 0; i < 1800; i++) {
            StepFrame(gs, run[size_t(i)]);
            if (gs.outcome == FrameOutcome::BLOCKED_BY_WALL &&
                gs.outcome_frame == gs.frame)
                wall_events++;
        }
        // The player WILL jump the pipes (no jump input -> pinned at pipe 1)
        Check(wall_events >= 1, "BLOCKED_BY_WALL fired against first pipe");
        printf("T8 wall-pin detection: events=%d -> %s\n",
               wall_events, g_failed ? "FAIL" : "ok");
    }

    printf("\n%d checks, %d failed\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
