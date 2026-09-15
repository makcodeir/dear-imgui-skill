// App.h — declares ide::App (implemented in App.cpp + Panels.cpp).
// Instantiated once in main.cpp; lifetime = process.
#pragma once
#include "Model.h"

namespace ide {

struct App
{
    Project     project;
    std::vector<EditorDoc> docs;    // [0] = welcome stub (file_id -1)
    int         active_doc = 0;
    bool        project_dirty = false;
    std::vector<Task> tasks;        // Kanban board items
    Selection   selection;
    Panels      panels;
    Console     console;
    Prefs       prefs;
    std::string find_text;           // editor find field
    std::string toolbar_search;      // toolbar search field
    bool        pending_exit = false;

    // ---- behavior (App.cpp) ----
    void        Init();
    void        Apply(const Action& a);          // execute an intent
    void        Log(Severity s, const std::string& text);
    const FileNode* FindFile(int id) const;
    FileNode*       FindFile(int id);
    int         CountFilesUnder(int folder_idx, bool recursive) const;
    void        SimulateBuild();                 // appends build log lines
    void        SimulateRun();
    void        DoSave();
    void        HandleShortcuts();               // chords + Escape cascade
    void        ExecuteCommand(int idx);         // command palette entries
    void        Draw();                          // full UI (Panels.cpp)
};

int  CommandCount();
const char* CommandName(int idx);
const char* CommandShortcut(int idx);

// UI popup requests + Escape cascade (implemented in Panels.cpp, backed by
// UI-only state living there: which modal wants to open next frame, etc.)
void RequestCommandPalette();
void RequestPreferences();
void RequestAbout();
void HandleEscape();

} // namespace ide
