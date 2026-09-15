// Model.h — flat data model + Action intents for dreamIDE ("Project Desk").
// Plain structs, indices/IDs only (no cross-frame pointers into vectors).
#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace ide {

// ---------- project model ----------
struct FileNode
{
    int     id = 0;              // unique, never reused within a project
    std::string name;            // display name, e.g. "engine.cpp"
    bool    is_folder = false;
    int     parent_id = -1;      // parent folder's id; -1 = root level
    std::string content;         // file body (used when opened)
    std::string last_modified = "2026-09-14 10:12";
    bool    read_only = false;
    int     size_bytes = 0;      // computed at creation
};

struct Project
{
    std::string              name = "Untitled";
    std::string              config = "Debug";
    std::vector<FileNode>    files;        // flat; tree via parent indices
    int                      next_id = 1;
    int                      open_root_count = 0; // cosmetic: folders drawn expanded
};

// ---------- tasks ----------
enum class TaskStatus : int { Backlog = 0, InProgress, Review, Done };
enum class Priority : int { Low = 0, Medium, High, Critical };

struct Task
{
    int         id = 0;
    std::string title, description, assignee;
    TaskStatus  status = TaskStatus::Backlog;
    Priority    priority = Priority::Medium;
    float       progress = 0.0f;     // 0..1
};

static const char* const kStatusNames[4] = { "Backlog", "In Progress", "Review", "Done" };
static const char* const kPriorityNames[4] = { "Low", "Medium", "High", "Critical" };

// ---------- console ----------
enum class Severity : int { Info = 0, Warning, Error, Success };
struct LogEntry { Severity sev; std::string text; };

// ---------- preferences ----------
struct Prefs
{
    float ui_scale = 1.0f;           // -> style.FontScaleMain
    bool  show_debug = false;        // debug grid overlay
    bool  autosave = false;
    int   theme = 0;                 // 0 dark, 1 light
    int   editor_font = 15;          // px
    int   console_font = 14;         // px
};

// ---------- editor documents ----------
struct EditorDoc
{
    int         file_id = -1;        // -1 for the welcome stub
    std::string title;               // tab caption (without dirty mark)
    std::string text;
    bool        dirty = false;
};

// ---------- selection (tagged union by index) ----------
struct Selection
{
    int kind = 0;    // 0 none, 1 file, 2 task
    int file_id = -1;
    int task_id = -1;
};

// ---------- console ----------
struct Console
{
    std::vector<LogEntry> entries;
    bool  auto_scroll = true;
    bool  show[4] = { true, true, true, true };  // Info/Warn/Error/Success
    int   unsimulated_builds = 0;    // Build presses turned into log lines
    int   unsimulated_runs = 0;
};

// ---------- actions: the single write path from UI to state ----------
struct Action
{
    enum Type : int {
        None, NewProject, Save, SaveAs, OpenProject, Exit,
        TogglePanel, SelectFile, SelectTask, CloseDoc, CloseApp,
        Build, Run, Config, Rename, Delete, Duplicate, NewFile,
        AddTask, MoveTask, DeleteTask, ExecCommand, ReopenWelcome, OpenDoc
    } type = None;

    Action() = default;
    Action(Type t, int panel_ = -1, int id_ = -1, int column_ = -1, int command_ = -1,
           std::string text_ = {})
        : type(t), panel(panel_), id(id_), column(column_), command(command_),
          text(std::move(text_)) {}

    // arguments (only the ones the intent needs)
    int    panel = -1;         // TogglePanel: 0 explorer 1 board 2 console 3 inspector
    int    id = -1;            // Select*/CloseDoc/MoveTask: file or task id
    int    column = -1;        // MoveTask target status
    int    command = -1;       // ExecCommand index
    std::string text;          // Rename / Config / NewFile name
};

// ---------- panels / app flags ----------
struct Panels { bool explorer = true, board = true, console = true, inspector = true; };

} // namespace ide
