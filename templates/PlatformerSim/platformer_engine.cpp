// platformer_engine.cpp — level construction, deterministic simulation, telemetry.
#include "platformer_core.h"
#include <algorithm>

// ============================================================================
// Level 1-1 inspired layout. 224x15 tiles, row 13-14 ground, two pits.
// ============================================================================
void BuildLevel1(Level& lv, const EngineConfig& cfg)
{
    const int W = cfg.map_w, H = cfg.map_h;
    lv.alloc(W, H);
    lv.enemies.clear();
    lv.coins.clear();

    auto ground = [&](int x0, int x1) { // inclusive column range, rows H-2..H-1
        for (int x = x0; x <= x1; x++)
            for (int y = H - 2; y < H; y++) lv.set(x, y, Tile::Ground);
    };
    ground(0, 68);              // long run-up
    // pit: cols 69-71 (3 tiles, jumpable with margin)
    ground(72, 152);
    // pit: cols 153-155
    ground(156, W - 1);

    // Question-block row (reachable: block bottoms at 10*.tile, jump peak 102px)
    lv.set(16, 9, Tile::Question);
    lv.set(17, 9, Tile::Brick);
    lv.set(18, 9, Tile::Question);
    lv.set(78, 9, Tile::Question);
    lv.set(79, 9, Tile::Brick);
    lv.set(105, 9, Tile::Question);
    lv.set(120, 9, Tile::Question);
    lv.set(121, 9, Tile::Brick);

    // Pipes (2 tiles wide, solid). Heights 2 and 3 tiles.
    auto pipe = [&](int x, int top_row) {
        lv.set(x, top_row, Tile::PipeTL);     lv.set(x + 1, top_row, Tile::PipeTR);
        for (int y = top_row + 1; y < H - 2; y++) {
            lv.set(x, y, Tile::PipeBL);       lv.set(x + 1, y, Tile::PipeBR);
        }
    };
    pipe(28, 11);
    pipe(38, 10);   // 3-tall: forces a real jump
    pipe(57, 11);
    pipe(138, 11);

    // Staircase before the flag (steps of 1 tile: trivially jumpable)
    for (int i = 0; i < 5; i++)
        for (int y = H - 3 - i; y <= H - 3; y++)
            lv.set(196 + i, y, Tile::Brick);

    // Flagpole: solid base on the ground, pole column above, trigger AABB.
    lv.set(205, H - 3, Tile::FlagBase);
    for (int y = 4; y <= H - 4; y++) lv.set(205, y, Tile::FlagPole);
    lv.flag_pos = {205.f * cfg.tile, (H - 3) * cfg.tile};
    lv.flag_trigger = {{205.f * cfg.tile - 6.f, 4.f * cfg.tile},
                       {cfg.tile + 12.f, (H - 4) * cfg.tile - 4.f * cfg.tile}};

    // Goombas: (spawn col, patrol x0, patrol x1) in tiles
    struct GSpawn { int col, x0, x1; };
    const GSpawn spawns[] = {
        {22, 20, 26}, {50, 45, 55}, {80, 76, 90}, {97, 92, 102},
        {107, 103, 115}, {130, 125, 136}, {160, 157, 166}, {175, 170, 182},
    };
    for (const GSpawn& s : spawns) {
        Enemy e;
        e.pos = {s.col * cfg.tile, (H - 3) * cfg.tile - 20.f};
        e.vel = {-float(cfg.enemy_speed), 0.f};
        e.patrol_min_x = s.x0 * cfg.tile;
        e.patrol_max_x = s.x1 * cfg.tile;
        lv.enemies.push_back(e);
    }

    // Coins: rows above ground, an arc over each pit, one on each pipe top.
    auto coin = [&](int tx, int ty) {
        Coin c;
        c.pos = {(tx + 0.5f) * cfg.tile, (ty + 0.5f) * cfg.tile};
        lv.coins.push_back(c);
    };
    for (int x : {8, 10, 12}) coin(x, 11);
    for (int x : {24, 26}) coin(x, 9);            // over first pipe
    for (int x = 68; x <= 72; x++) coin(x, 9);    // arc over pit 1
    for (int x : {85, 87, 89}) coin(x, 11);
    for (int x = 152; x <= 156; x++) coin(x, 9);  // arc over pit 2
    for (int x : {168, 170, 172, 174}) coin(x, 11);
    coin(29, 9); coin(39, 8);                     // pipe tops
}

// ============================================================================
// Baseline input macro. Pre-planned = computed ONCE from the known level
// geometry before any live/replay run (a deterministic probe simulation with
// a greedy obstacle-look-ahead controller), then delivered as a blind
// FrameInput sequence. Replaying it reproduces the probe's trajectory
// bit-for-bit because the sim is deterministic.
// Controller rules: hold RIGHT+RUN; when a pit, wall/pipe/step, ?-block row,
// or goomba appears within 2.5 tiles ahead and we are grounded, jump and hold
// for 22 frames (release just after the apex => no jump-cut height loss).
// ============================================================================
std::vector<FrameInput> BuildBaselineInputs()
{
    EngineConfig cfg;
    GameState gs;
    gs.reset(cfg);

    std::vector<FrameInput> script;
    script.reserve(2400);
    int jump_hold = 0;

    for (int f = 0; f < 3600; f++) {
        FrameInput fi;
        fi.frame_number = f;
        fi.right = true;
        fi.run = true;

        int ptx = int((gs.pos.x + gs.cfg.player_size.x * 0.5f) / gs.cfg.tile);
        int foot_row = int((gs.pos.y + gs.cfg.player_size.y) / gs.cfg.tile);
        bool hazard_ahead = false;
        for (int dx = 1; dx <= 3 && !hazard_ahead; dx++) {
            int cx = ptx + dx;
            if (cx < 0 || cx >= gs.cfg.map_w) break;
            bool col_has_ground = false;
            for (int ty = 0; ty < gs.cfg.map_h; ty++) {
                if (!gs.level.solid(gs.level.at(cx, ty))) continue;
                col_has_ground = true;
                if (ty <= foot_row - 1) { hazard_ahead = true; break; } // wall/pipe/step/?
            }
            if (!col_has_ground) hazard_ahead = true; // bottomless pit
        }
        for (const Enemy& e : gs.level.enemies) {
            if (!e.alive) continue;
            float dxp = e.pos.x - gs.pos.x;
            if (dxp > 0.f && dxp < 2.5f * gs.cfg.tile &&
                std::fabs(e.pos.y - gs.pos.y) < gs.cfg.tile)
                hazard_ahead = true;
        }

        if (gs.on_ground && hazard_ahead && jump_hold == 0) jump_hold = 22;
        if (jump_hold > 0) { fi.jump = true; jump_hold--; }

        script.push_back(fi);
        StepFrame(gs, fi);
        if (gs.won || !gs.alive) break;
    }
    return script;
}

// ============================================================================
// Simulation
// ============================================================================
static const float FIXED_DT = 1.0f / 60.0f;

static bool TileSolid(const Level& lv, Tile t) { return lv.solid(t); }

static void LogEvent(GameState& gs, FrameOutcome o)
{
    gs.outcome = o;
    gs.outcome_frame = gs.frame;
    gs.events.push_back({gs.frame, o});
}

// Sweep pos.x by dx against the tilemap; clamps on first solid contact.
static void MoveX(GameState& gs, float dx, bool& blocked, bool& input_into_wall)
{
    Level& lv = gs.level;
    gs.pos.x += dx;
    AABB box{{gs.pos.x, gs.pos.y}, gs.cfg.player_size};
    int tx0 = int(std::floor(box.xMin() / gs.cfg.tile));
    int tx1 = int(std::floor((box.xMax() - 0.001f) / gs.cfg.tile));
    int ty0 = int(std::floor(box.yMin() / gs.cfg.tile));
    int ty1 = int(std::floor((box.yMax() - 0.001f) / gs.cfg.tile));
    for (int ty = ty0; ty <= ty1; ty++) {
        for (int tx = tx0; tx <= tx1; tx++) {
            if (tx < 0 || tx >= gs.cfg.map_w || ty < 0 || ty >= gs.cfg.map_h) continue;
            if (!TileSolid(lv, lv.at(tx, ty))) continue;
            float wall = dx > 0 ? tx * gs.cfg.tile
                                : (tx + 1) * gs.cfg.tile; // x of contact face
            gs.pos.x = dx > 0 ? wall - gs.cfg.player_size.x : wall;
            gs.vel.x = 0.f;
            blocked = true;
            input_into_wall = (dx > 0 && gs.input.right) || (dx < 0 && gs.input.left);
            return; // resolved; one contact is enough for axis-separated sweeps
        }
    }
}

static void MoveY(GameState& gs, float dy, bool& landed, bool& headhit)
{
    Level& lv = gs.level;
    gs.pos.y += dy;
    AABB box{{gs.pos.x, gs.pos.y}, gs.cfg.player_size};
    int tx0 = int(std::floor(box.xMin() / gs.cfg.tile));
    int tx1 = int(std::floor((box.xMax() - 0.001f) / gs.cfg.tile));
    int ty0 = int(std::floor(box.yMin() / gs.cfg.tile));
    int ty1 = int(std::floor((box.yMax() - 0.001f) / gs.cfg.tile));
    for (int ty = ty0; ty <= ty1; ty++) {
        for (int tx = tx0; tx <= tx1; tx++) {
            if (tx < 0 || tx >= gs.cfg.map_w || ty < 0 || ty >= gs.cfg.map_h) continue;
            Tile t = lv.at(tx, ty);
            if (!TileSolid(lv, t)) continue;
            if (dy > 0) { // landing
                gs.pos.y = ty * gs.cfg.tile - gs.cfg.player_size.y;
                gs.vel.y = 0.f;
                landed = true;
            } else {      // head bump
                gs.pos.y = (ty + 1) * gs.cfg.tile;
                gs.vel.y = 0.f;
                headhit = true;
                if (t == Tile::Question) {
                    lv.set(tx, ty, Tile::Used);
                    Coin c;
                    c.pos = {(tx + 0.5f) * gs.cfg.tile, ty * gs.cfg.tile - 10.f};
                    c.from_block = true;
                    lv.coins.push_back(c);
                    gs.coins++; gs.score += 200; gs.question_hits++;
                    LogEvent(gs, FrameOutcome::QUESTION_HIT);
                }
            }
            return;
        }
    }
}

FrameOutcome StepFrame(GameState& gs, const FrameInput& inp)
{
    if (gs.outcome == FrameOutcome::REACHED_GOAL || !gs.alive)
        return gs.outcome; // terminal: frozen for stable telemetry/render

    const EngineConfig& cfg = gs.cfg;
    gs.input = inp;            // remembered for MoveX wall-pin check
    gs.frame++;

    // --- timers ---
    gs.coyote = std::max(0, gs.coyote - 1);
    gs.jump_buffer = std::max(0, gs.jump_buffer - 1);
    if (gs.invuln > 0) gs.invuln--;

    // --- jump buffering (edge-triggered) ---
    if (inp.jump && !gs.prev_jump) gs.jump_buffer = int(cfg.jump_buffer_frames);
    gs.prev_jump = inp.jump;

    // --- horizontal momentum ---
    float dir = (inp.right ? 1.f : 0.f) - (inp.left ? 1.f : 0.f);
    float max_speed = inp.run ? cfg.max_run_held : cfg.max_run;
    float accel = gs.on_ground ? cfg.run_accel : cfg.air_accel;
    if (dir != 0.f) {
        gs.vel.x += dir * accel * FIXED_DT;
        gs.vel.x = std::clamp(gs.vel.x, -max_speed, max_speed);
    } else {
        float drag = (gs.on_ground ? cfg.ground_friction : cfg.air_drag) * FIXED_DT;
        if (std::fabs(gs.vel.x) <= drag) gs.vel.x = 0.f;
        else gs.vel.x -= std::copysign(drag, gs.vel.x);
    }

    // --- jump execution (buffer + coyote) ---
    if (gs.jump_buffer > 0 && (gs.on_ground || gs.coyote > 0)) {
        gs.vel.y = -cfg.jump_vel;
        gs.on_ground = false;
        gs.coyote = 0;
        gs.jump_buffer = 0;
        gs.jump_cut_done = false;
        gs.jump_held_frames = 0;
    }

    // --- variable jump height: cut velocity when released while rising ---
    if (gs.vel.y < 0.f && !gs.jump_cut_done) {
        if (inp.jump) gs.jump_held_frames++;
        if (!inp.jump && gs.jump_held_frames >= 1) {
            gs.vel.y *= cfg.jump_cut_mul;
            gs.jump_cut_done = true;
        }
    }

    // --- gravity ---
    gs.vel.y = std::min(gs.vel.y + cfg.gravity * FIXED_DT, cfg.terminal_v);

    // --- X axis sweep ---
    bool blocked = false, input_into_wall = false;
    MoveX(gs, gs.vel.x * FIXED_DT, blocked, input_into_wall);
    if (blocked && input_into_wall) {
        gs.wall_pin_timer += FIXED_DT;
        if (gs.wall_pin_timer > 0.75f) {
            LogEvent(gs, FrameOutcome::BLOCKED_BY_WALL);
            gs.wall_pin_timer = 0.f; // diagnostic event; TIME_UP is the hard stop
        }
    } else {
        gs.wall_pin_timer = 0.f;
    }

    // --- Y axis sweep (vertical priority: resolve after X so corners behave) ---
    bool was_grounded = gs.on_ground;
    gs.on_ground = false;
    bool landed = false, headhit = false;
    MoveY(gs, gs.vel.y * FIXED_DT, landed, headhit);
    if (landed) {
        gs.on_ground = true;
        gs.coyote = int(cfg.coyote_frames);
    } else if (was_grounded && gs.vel.y >= 0.f) {
        // walked off a ledge this frame -> start coyote grace
        gs.coyote = int(cfg.coyote_frames);
    }

    // --- hazards / pit ---
    AABB box{{gs.pos.x, gs.pos.y}, cfg.player_size};
    int ptx0 = int(std::floor(box.xMin() / cfg.tile)), ptx1 = int(std::floor((box.xMax() - 0.001f) / cfg.tile));
    int pty0 = int(std::floor(box.yMin() / cfg.tile)), pty1 = int(std::floor((box.yMax() - 0.001f) / cfg.tile));
    for (int ty = pty0; ty <= pty1 && gs.alive; ty++)
        for (int tx = ptx0; tx <= ptx1 && gs.alive; tx++) {
            if (tx < 0 || tx >= cfg.map_w || ty < 0 || ty >= cfg.map_h) continue;
            if (gs.level.hazard(gs.level.at(tx, ty))) {
                gs.alive = false;
                LogEvent(gs, FrameOutcome::HIT_HAZARD);
            }
        }
    if (gs.pos.y > cfg.map_h * cfg.tile + 120.f) { // below the world
        gs.alive = false;
        LogEvent(gs, FrameOutcome::FALLEN_INTO_PIT);
        return gs.outcome;
    }

    // --- coins ---
    for (Coin& c : gs.level.coins) {
        if (c.collected) continue;
        if (std::fabs(c.pos.x - (gs.pos.x + cfg.player_size.x * 0.5f)) < cfg.tile * 0.6f &&
            std::fabs(c.pos.y - (gs.pos.y + cfg.player_size.y * 0.5f)) < cfg.tile * 0.7f) {
            c.collected = true;
            gs.coins++; gs.score += 100;
        }
    }

    // --- enemies: patrol + stomp / damage ---
    float prev_bottom = box.yMax();
    for (Enemy& e : gs.level.enemies) {
        if (!e.alive) { e.dead_timer += FIXED_DT; continue; }
        e.pos.x += e.vel.x * FIXED_DT;
        if (e.pos.x < e.patrol_min_x) { e.pos.x = e.patrol_min_x; e.vel.x = std::fabs(e.vel.x); }
        if (e.pos.x > e.patrol_max_x) { e.pos.x = e.patrol_max_x; e.vel.x = -std::fabs(e.vel.x); }
        AABB eb{{e.pos.x, e.pos.y}, {20.f, 20.f}};
        if (!box.overlaps(eb)) continue;
        bool stomp = gs.vel.y > 0.f && prev_bottom <= eb.pos.y + 10.f;
        if (stomp) {
            e.alive = false;
            e.stomped_flash = true;
            e.dead_timer = 0.f;
            gs.goombas_stomped++;
            gs.score += 100;
            gs.vel.y = -cfg.jump_vel * 0.5f; // bounce
            LogEvent(gs, FrameOutcome::STOMPED_ENEMY);
        } else if (gs.invuln == 0) {
            gs.health--;
            gs.invuln = 90; // 1.5 s
            float knock = gs.pos.x < e.pos.x ? -1.f : 1.f;
            gs.vel.x = knock * 140.f;
            gs.vel.y = -220.f;
            if (gs.health <= 0) {
                gs.alive = false;
                LogEvent(gs, FrameOutcome::KILLED_BY_ENEMY);
                return gs.outcome;
            }
        }
    }

    // --- goal ---
    if (box.overlaps(gs.level.flag_trigger)) {
        gs.won = true;
        gs.score += 1000 + std::max(0, cfg.time_limit_frames - gs.frame) / 10;
        LogEvent(gs, FrameOutcome::REACHED_GOAL);
        return gs.outcome;
    }

    // --- camera: follow player with deadzone, never scroll left ---
    {
        const float view_w = 20.f * cfg.tile; // 20 tiles visible
        float player_cx = gs.pos.x + cfg.player_size.x * 0.5f;
        float target = player_cx - view_w * 0.55f;
        if (target > gs.camera.x) gs.camera.x = target;
        gs.camera.x = std::clamp(gs.camera.x, 0.f,
                                 cfg.map_w * cfg.tile - view_w);
    }

    // --- time limit ---
    if (gs.frame >= cfg.time_limit_frames)
        LogEvent(gs, FrameOutcome::TIME_UP);
    return gs.outcome;
}

void SampleTileGrid(const GameState& gs, char out[15 * 15 + 1])
{
    const int R = 15, HALF = R / 2;
    int ptx = int(std::floor((gs.pos.x + gs.cfg.player_size.x * 0.5f) / gs.cfg.tile));
    int pty = int(std::floor((gs.pos.y + gs.cfg.player_size.y * 0.5f) / gs.cfg.tile));
    int k = 0;
    for (int dy = -HALF; dy <= HALF; dy++) {
        for (int dx = -HALF; dx <= HALF; dx++) {
            int tx = ptx + dx, ty = pty + dy;
            char c = '.';
            if (tx >= 0 && tx < gs.cfg.map_w && ty >= 0 && ty < gs.cfg.map_h) {
                switch (gs.level.at(tx, ty)) {
                    case Tile::Ground:  c = 'G'; break;
                    case Tile::Brick:   c = 'B'; break;
                    case Tile::Question:c = '?'; break;
                    case Tile::Used:    c = 'U'; break;
                    case Tile::PipeTL: case Tile::PipeTR:
                    case Tile::PipeBL: case Tile::PipeBR: c = '|'; break;
                    case Tile::Lava:    c = '#'; break;
                    case Tile::FlagPole: c = 'F'; break;
                    case Tile::FlagBase: c = 'F'; break;
                    default: break;
                }
            }
            out[k++] = c;
        }
    }
    out[k] = '\0';
}

const char* OutcomeStr(FrameOutcome o)
{
    switch (o) {
        case FrameOutcome::None: return "NONE";
        case FrameOutcome::FALLEN_INTO_PIT: return "FALLEN_INTO_PIT";
        case FrameOutcome::HIT_HAZARD: return "HIT_HAZARD";
        case FrameOutcome::STOMPED_ENEMY: return "STOMPED_ENEMY";
        case FrameOutcome::BLOCKED_BY_WALL: return "BLOCKED_BY_WALL";
        case FrameOutcome::QUESTION_HIT: return "QUESTION_HIT";
        case FrameOutcome::REACHED_GOAL: return "REACHED_GOAL";
        case FrameOutcome::TIME_UP: return "TIME_UP";
        case FrameOutcome::KILLED_BY_ENEMY: return "KILLED_BY_ENEMY";
    }
    return "NONE";
}

// ============================================================================
// Telemetry serialization (hand-rolled JSON; no external dependency)
// ============================================================================
static std::string F2(float v) {
    char b[32];
    snprintf(b, sizeof b, "%.2f", v);
    return b;
}

std::string SerializeSnapshot(const GameState& gs, bool include_grid)
{
    std::string j;
    char grid[15 * 15 + 1];
    SampleTileGrid(gs, grid);
    char buf[512];
    snprintf(buf, sizeof buf,
             "{\"frame\":%d,\"t\":%.2f,\"player\":{\"x\":%s,\"y\":%s,\"vx\":%s,\"vy\":%s,"
             "\"grounded\":%s,\"health\":%d,\"alive\":%s},\"camera_x\":%s,"
             "\"coins\":%d,\"score\":%d,\"goal\":{\"dx\":%s,\"dist\":%s},\"enemies\":[",
             gs.frame, gs.frame / 60.0, F2(gs.pos.x).c_str(), F2(gs.pos.y).c_str(),
             F2(gs.vel.x).c_str(), F2(gs.vel.y).c_str(),
             gs.on_ground ? "true" : "false", gs.health, gs.alive ? "true" : "false",
             F2(gs.camera.x).c_str(), gs.coins, gs.score,
             F2(gs.level.flag_pos.x - gs.pos.x).c_str(),
             F2(vdist(gs.pos, gs.level.flag_pos)).c_str());
    j += buf;
    int count = 0;
    for (const Enemy& e : gs.level.enemies) {
        if (!e.alive) continue;
        float dx = e.pos.x - gs.pos.x, dy = e.pos.y - gs.pos.y;
        if (std::fabs(dx) > 320.f) continue;
        char b[160];
        snprintf(b, sizeof b, "%s{\"dx\":%s,\"dy\":%s,\"vx\":%s,\"vy\":%s}",
                 count ? "," : "", F2(dx).c_str(), F2(dy).c_str(),
                 F2(e.vel.x).c_str(), F2(e.vel.y).c_str());
        j += b;
        count++;
    }
    j += "]";
    if (include_grid) {
        j += ",\"tiles\":[";
        for (int r = 0; r < 15; r++) {
            char row[17];
            memcpy(row, grid + r * 15, 15);
            row[15] = '\0';
            j += std::string("\"") + row + "\",";
        }
        j.pop_back();
        j += "]";
    }
    j += ",\"outcome\":\"" + std::string(OutcomeStr(gs.outcome)) + "\"}";
    return j;
}

// Events array (JSON) from a completed run — appended to the payload by callers.
std::string SerializeEvents(const GameState& gs)
{
    std::string j = "[";
    for (size_t i = 0; i < gs.events.size(); i++) {
        char b[128];
        snprintf(b, sizeof b, "%s{\"frame\":%d,\"event\":\"%s\"}",
                 i ? "," : "", gs.events[i].frame, OutcomeStr(gs.events[i].kind));
        j += b;
    }
    return j + "]";
}

// ============================================================================
// Deterministic replay driver: resets state, feeds the input macro, snapshots
// every `telemetry_every` frames (plus first/last), stops on terminal state.
// ============================================================================
ReplayResult RunReplay(const EngineConfig& cfg, const std::vector<FrameInput>& inputs,
                       int telemetry_every, bool include_grid)
{
    ReplayResult r;
    r.final_state.reset(cfg);

    std::string& snaps = r.snapshots;
    snaps += "[";
    bool first = true;
    auto emit = [&](bool force) {
        if (!force && telemetry_every > 0 && r.final_state.frame % telemetry_every != 0) return;
        if (!first) snaps += ",";
        first = false;
        snaps += SerializeSnapshot(r.final_state, include_grid);
    };

    emit(true); // initial state (frame 0)
    for (const FrameInput& fi : inputs) {
        FrameOutcome o = StepFrame(r.final_state, fi);
        emit(false);
        if (o == FrameOutcome::REACHED_GOAL || !r.final_state.alive) break;
        if (r.final_state.frame >= cfg.time_limit_frames) break;
    }
    emit(true); // final state
    snaps += "]";

    r.events = SerializeEvents(r.final_state);
    r.frames = r.final_state.frame;
    r.won = r.final_state.won;
    r.outcome = r.final_state.outcome;
    return r;
}
