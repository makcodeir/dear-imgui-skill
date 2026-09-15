// platformer_core.h — deterministic Mario-style platformer engine + AI verification harness.
//
// Subsystem map:
//   engine   : tilemap, player, enemies, camera, fixed-dt physics, AABB sweep
//   replay   : FrameInput macros, deterministic headless stepping, telemetry JSON
//   aiclient : OpenSSL HTTPS POST to any OpenAI-compatible /chat/completions
//   render   : ImDrawList procedural drawing + HUD + log console (GUI build only)
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// ============================================================================
// Math
// ============================================================================
struct Vec2 {
    float x = 0.f, y = 0.f;
};
static inline Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
static inline Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
static inline Vec2 operator*(Vec2 a, float s) { return {a.x * s, a.y * s}; }
static inline float vdist(Vec2 a, Vec2 b) {
    float dx = a.x - b.x, dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

struct AABB {
    Vec2 pos; // top-left
    Vec2 size;
    float xMin() const { return pos.x; }
    float xMax() const { return pos.x + size.x; }
    float yMin() const { return pos.y; }
    float yMax() const { return pos.y + size.y; }
    bool overlaps(const AABB& o) const {
        return xMin() < o.xMax() && xMax() > o.xMin() && yMin() < o.yMax() && yMax() > o.yMin();
    }
};

// ============================================================================
// Engine configuration (physics feel constants; units are world px, seconds)
// ============================================================================
struct EngineConfig {
    // world
    int   map_w = 224;             // tiles
    int   map_h = 15;              // tiles (camera view height == map height)
    float tile = 24.0f;            // world px per tile
    float gravity = 2000.0f;       // px/s^2
    float terminal_v = 900.0f;     // px/s downward cap
    // player
    Vec2  player_size{22.f, 26.f};
    float run_accel = 1400.f;      // ground accel
    float air_accel = 1000.f;
    float max_run = 160.f;         // px/s
    float max_run_held = 240.f;    // run button held
    float ground_friction = 1600.f;// px/s^2 decel when no input on ground
    float air_drag = 200.f;        // px/s^2 decel when no input in air
    float jump_vel = 640.f;        // px/s initial jump impulse
    float jump_cut_mul = 0.45f;    // velocity scale when jump released early
    float coyote_frames = 6;       // 0.1 s @ 60fps
    float jump_buffer_frames = 4;
    int   enemy_speed = 40;        // px/s goomba patrol
    int   time_limit_frames = 60 * 60; // 60 s level timer
};

// ============================================================================
// Tiles / entities / level
// ============================================================================
enum class Tile : uint8_t {
    Empty = 0,
    Ground,      // solid brown block
    Brick,       // solid beveled block
    Question,    // ? block: bounce + coin when hit from below, turns Used
    Used,        // spent question block
    PipeTL, PipeTR, PipeBL, PipeBR, // solid pipe (2x2 tiles)
    Lava,        // hazard: death on touch
    FlagPole,    // win trigger (pole column)
    FlagBase,    // solid block under pole
};

struct Enemy {
    Vec2 pos;        // top-left in world px
    Vec2 vel;
    float patrol_min_x = 0.f, patrol_max_x = 0.f;
    bool alive = true;
    bool stomped_flash = false; // rendered flat for a few frames
    float dead_timer = 0.f;
};

struct Coin {
    Vec2 pos;      // center in world px
    float t = 0.f; // bob phase
    bool collected = false;
    bool from_block = false; // spawned by question block
};

struct Camera {
    float x = 0.f;    // left edge in world px; y is fixed (map == viewport height)
    float deadzone = 48.f;
};

struct Level {
    int width = 0, height = 0;
    std::vector<Tile> tiles;   // width*height, row-major
    std::vector<Enemy> enemies;
    std::vector<Coin> coins;
    Vec2 flag_pos;             // base of flagpole (world px)
    AABB flag_trigger;

    void alloc(int w, int h) {
        width = w; height = h;
        tiles.assign(size_t(w) * h, Tile::Empty);
    }
    Tile at(int tx, int ty) const {
        if (tx < 0 || ty < 0 || tx >= width || ty >= height) return Tile::Empty;
        return tiles[size_t(ty) * width + tx];
    }
    void set(int tx, int ty, Tile t) {
        if (tx < 0 || ty < 0 || tx >= width || ty >= height) return;
        tiles[size_t(ty) * width + tx] = t;
    }
    bool solid(Tile t) const {
        return t == Tile::Ground || t == Tile::Brick || t == Tile::Question ||
               t == Tile::Used || t == Tile::PipeTL || t == Tile::PipeTR ||
               t == Tile::PipeBL || t == Tile::PipeBR || t == Tile::FlagBase;
    }
    bool hazard(Tile t) const { return t == Tile::Lava; }
    bool goal(Tile t) const { return t == Tile::FlagPole; }
};

// Level 1-1 inspired: flat runs, pipe hops, two pits, question blocks, goombas.
// 224 x 15 tiles. Pits (gaps in the ground rows) at columns 69-71 and 153-155.
void BuildLevel1(Level& lv, const EngineConfig& cfg);

// ============================================================================
// Frame input / deterministic replay
// ============================================================================
struct FrameInput {
    int  frame_number = 0;
    bool left = false, right = false, jump = false, run = false;
};

// Pre-planned baseline test input: run right holding run, rhythmic full-height
// hops that clear pits and pipes, reach the flagpole. Frames @60Hz, 3600 long.
std::vector<FrameInput> BuildBaselineInputs();

// ============================================================================
// Game state (single flat struct — serializable, resettable, deterministic)
// ============================================================================
enum class FrameOutcome {
    None = 0,
    FALLEN_INTO_PIT,
    HIT_HAZARD,
    STOMPED_ENEMY,
    KILLED_BY_ENEMY,
    BLOCKED_BY_WALL, // pinned against a wall while walking into it >0.75 s
    QUESTION_HIT,
    REACHED_GOAL,
    TIME_UP,
};

struct GameState;

const char* OutcomeStr(FrameOutcome o); // telemetry + log console

// ============================================================================
// Simulation (pure functions — no GL, no GLFW; GUI and headless share this)
// ============================================================================
// Step one fixed 1/60 s frame. `inp` drives the player (live keys or replay).
FrameOutcome StepFrame(GameState& gs, const FrameInput& inp);

// 15x15 char grid of tiles around the player, for telemetry ('.'=empty).
void SampleTileGrid(const GameState& gs, char out[15 * 15 + 1]);

// One telemetry snapshot (JSON object) of the CURRENT state.
std::string SerializeSnapshot(const GameState& gs, bool include_grid);

// JSON array of {frame, event} outcome records from the current run's log.
std::string SerializeEvents(const GameState& gs);

// ============================================================================
// Game state body (needs StepFrame decl above for nothing; kept flat here so
// reset() stays trivially inlined and the struct is memcpy-able for replays)
// ============================================================================
struct GameState {
    // player
    Vec2 pos{}, vel{};
    bool on_ground = false, alive = true, won = false;
    int   health = 3, coins = 0, score = 0, lives = 1;
    int   coyote = 0, jump_buffer = 0, jump_held_frames = 0;
    bool  jump_cut_done = false;
    float wall_pin_timer = 0.f;
    int   invuln = 0;      // frames of post-hit invulnerability
    // world
    Level level;
    EngineConfig cfg;
    Camera camera;
    int frame = 0;
    FrameOutcome outcome = FrameOutcome::None;
    int outcome_frame = -1;
    int question_hits = 0, goombas_stomped = 0;
    // event log (drives telemetry "events" + the ImGui console)
    struct Event { int frame; FrameOutcome kind; };
    std::vector<Event> events;
    // last input (for wall-pin attribution) + previous jump (edge detection)
    FrameInput input{};
    bool prev_jump = false;
    // deterministic PRNG (cosmetic only; sim itself never rolls dice)
    uint64_t rng = 0x12345678;

    void reset(const EngineConfig& c) {
        cfg = c;
        BuildLevel1(level, cfg);
        pos = {2.f * cfg.tile, 10.f * cfg.tile};
        vel = {0.f, 0.f};
        on_ground = true; alive = true; won = false;
        health = 3; coins = 0; score = 0; lives = 1;
        coyote = jump_buffer = jump_held_frames = 0;
        jump_cut_done = false; wall_pin_timer = 0.f; invuln = 0;
        camera.x = 0.f;
        frame = 0; outcome = FrameOutcome::None; outcome_frame = -1;
        question_hits = goombas_stomped = 0;
        events.clear();
        input = FrameInput{};
        prev_jump = false;
        rng = 0x12345678;
    }
    uint64_t next_rng() { rng = rng * 6364136223846793005ULL + 1; return rng >> 33; }
};

// Deterministic replay driver: reset -> feed inputs -> snapshot every N frames
// (plus first/last) -> stop on terminal state or the level timer.
struct ReplayResult {
    int frames = 0;
    bool won = false;
    FrameOutcome outcome = FrameOutcome::None;
    GameState final_state;
    std::string snapshots; // JSON array of snapshot objects
    std::string events;    // JSON array of {frame, event}
};
ReplayResult RunReplay(const EngineConfig& cfg, const std::vector<FrameInput>& inputs,
                       int telemetry_every, bool include_grid);

// ============================================================================
// Telemetry assembly (caller-side: step + SerializeSnapshot every N frames,
// join with commas, wrap in '[ ... ]'). Outcome events come from gs.events.
// ============================================================================

// ============================================================================
// AI verification client (OpenAI-compatible /chat/completions over HTTPS)
// ============================================================================
struct AiVerdict {
    bool ok = false;             // HTTP + parse succeeded
    bool is_playable = false;
    float confidence = 0.f;
    std::string raw_response;    // full message content
    std::string error;
    std::vector<std::string> bottlenecks;
    std::vector<std::string> suggested_inputs;
};

// `api_key`: bearer token (falls back to OPENAI_API_KEY env when empty).
// `model`: e.g. "gpt-4o-mini". `base_url` may be overridden
// (default https://api.openai.com/v1). Never throws; failures land in error.
AiVerdict QueryAiPlayability(const std::string& telemetry_json,
                             const std::string& api_key,
                             const std::string& model,
                             const std::string& base_url);

// Extract the innermost JSON object from an LLM reply that may contain prose
// or markdown fences. Returns false if none found.
bool ExtractJsonObject(const std::string& text, std::string& out);

// ============================================================================
// GUI renderer (ImGui viewport, ImDrawList primitives, HUD, log console)
// Only compiled into the GUI binary; headless self-test skips this.
// ============================================================================
struct RenderHarness {           // opaque handle (defined empty; cast down in render TU)
    virtual ~RenderHarness() = default;
};
RenderHarness* RenderInit();
// Poll events, begin the ImGui frame, fill out_live from the keyboard.
// Returns false when the window should close.
bool RenderPump(RenderHarness* rh, FrameInput& out_live);
// Draw the whole app (game canvas + HUD + AI panel + log console), end frame.
void RenderFrame(RenderHarness* rh, const GameState& gs, bool replay_active,
                 const std::vector<std::string>& log_lines);
void RenderShutdown(RenderHarness* rh);
