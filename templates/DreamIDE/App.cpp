// App.cpp — state construction, action application, console simulation, commands.
#include "App.h"
#include "imgui.h"
#include <algorithm>
#include <cstring>
#include <utility>

namespace ide {

static void PushFile(App& app, const char* name, int parent_id, const char* content = "")
{
    FileNode n;
    n.id = app.project.next_id++;
    n.name = name;
    n.parent_id = parent_id;
    n.content = content;
    n.size_bytes = (int)strlen(content);
    app.project.files.push_back(n);
}

static int PushFolder(App& app, const char* name, int parent_id)
{
    FileNode n;
    n.id = app.project.next_id++;
    n.name = name;
    n.parent_id = parent_id;
    n.is_folder = true;
    app.project.files.push_back(n);
    return app.project.next_id - 1;    // folder id
}

void App::Init()
{
    // ----- project tree (Project Alpha) -----
    project.name = "Project Alpha";
    int src      = PushFolder(*this, "src", -1);
    int include_ = PushFolder(*this, "include", -1);
    int assets   = PushFolder(*this, "assets", -1);
    PushFolder(*this, "textures", assets);
    PushFolder(*this, "sounds", assets);

    PushFile(*this, "main.cpp", src,
        "#include <engine.h>\n#include <renderer.h>\n\nint main(int argc, char** argv)\n{\n"
        "    (void)argc; (void)argv;\n\n    Engine engine;\n    if (!engine.Startup())\n        return 1;\n\n"
        "    Renderer renderer;\n    renderer.Attach(&engine);\n\n    while (engine.Running())\n    {\n"
        "    engine.PollEvents();\n        renderer.DrawFrame();\n    }\n\n    engine.Shutdown();\n    return 0;\n}\n");
    PushFile(*this, "engine.cpp", src,
        "#include <engine.h>\n\nbool Engine::Startup()\n{\n    frame = 0;\n    running = true;\n"
        "    return true;\n}\n\nvoid Engine::PollEvents()\n{\n    // TODO: window input pump\n    ++frame;\n"
        "    float deltaTime = 0.0f; // FIXME: unused variable\n    (void)deltaTime;\n}\n");
    PushFile(*this, "renderer.cpp", src,
        "#include <renderer.h>\n\nvoid Renderer::DrawFrame()\n{\n    // Present the current frame.\n"
        "    swap_chain->Present();\n}\n");
    PushFile(*this, "engine.h", include_,
        "#pragma once\n\nclass Renderer;\n\nclass Engine\n{\npublic:\n    bool Startup();\n    void PollEvents();\n"
        "    void Shutdown();\n    bool Running() const { return running; }\n\nprivate:\n    bool running = false;\n"
        "    int  frame = 0;\n};\n");
    PushFile(*this, "renderer.h", include_,
        "# pragma once\n\nclass Engine;\n\nclass Renderer\n{\npublic:\n    void Attach(Engine* e);\n"
        "    void DrawFrame();\n\nprivate:\n    Engine* engine = nullptr;\n};\n");
    PushFile(*this, "README.md", -1,
        "# Project Alpha\n\nA small engine sandbox used as the sample project for dreamIDE.\n\n"
        "## Build\n\n    ./build.sh\n\n## Layout\n\n- src/        application sources\n"
        "- include/    public headers\n- assets/     textures and sounds\n");

    // ----- opened documents (welcome + two files) -----
    docs.push_back({ -1, "Welcome",
        "Welcome to Project Desk\n"
        "=======================\n\n"
        "This is a small developer/project management workspace built with Dear ImGui.\n\n"
        "Quick start:\n"
        "  *  Open a file from the Project Explorer on the left.\n"
        "  *  Press Ctrl+P for the Command Palette.\n"
        "  *  Select a task on the Task Board to edit it in the Inspector.\n\n"
        "Everything here runs locally; nothing leaves this window.\n",
        false });
    const FileNode* mainf = FindFile(3);
    const FileNode* engf  = FindFile(4);
    if (mainf) docs.push_back({ mainf->id, mainf->name, mainf->content, false });
    if (engf)  docs.push_back({ engf->id,  engf->name,  engf->content,  false });
    active_doc = 1;
    if (mainf) selection = { 1, mainf->id, -1 };

    // ----- tasks -----
    const Task seed[] = {
        { 1, "Set up CMake project", "Minimum viable CMakeLists that builds the engine sandbox.",      "alice", TaskStatus::Done,       Priority::High,     1.00f },
        { 2, "Window + input pump",  "GLFW window, keyboard/mouse events routed into the engine loop.", "bob",   TaskStatus::InProgress, Priority::High,     0.60f },
        { 3, "MoveTask placeholder", "", "", TaskStatus::Review, Priority::Low, 0.0f },
        { 4, "Asset hot-reload",     "Watch assets/ and reload textures on change.",                    "dave",  TaskStatus::Review,     Priority::Medium,   0.90f },
        { 5, "Unit-test math library","Cover vec/mat helpers; target 80% line coverage.",               "erin",  TaskStatus::Backlog,    Priority::Medium,   0.00f },
        { 6, "CI pipeline",          "GitHub Actions: build matrix + headless smoke test per PR.",      "alice", TaskStatus::Backlog,    Priority::Low,      0.00f },
        { 7, "Profiling overlays",   "Frame-time graph and GPU timestamps in the debug overlay.",       "bob",   TaskStatus::Backlog,    Priority::High,     0.00f },
    };
    for (const Task& t : seed) tasks.push_back(t);
    tasks[2].title = "Swapchain abstraction";
    tasks[2].description = "Wrap platform swapchain behind Renderer interface.";
    tasks[2].assignee = "carol";
    tasks[2].priority = Priority::Critical;
    tasks[2].progress = 0.35f;

    Log(Severity::Info,    "dreamIDE 0.1.0 started.");
    Log(Severity::Info,    "Opened workspace: Project Alpha (6 files, 5 folders).");
    Log(Severity::Success, "Indexing finished - 0 problems found.");
}

// ---------------------------------------------------------------------------
const FileNode* App::FindFile(int id) const
{
    for (const FileNode& n : project.files)
        if (n.id == id) return &n;
    return nullptr;
}

FileNode* App::FindFile(int id)
{
    for (int i = (int)project.files.size() - 1; i >= 0; --i)
        if (project.files[i].id == id) return &project.files[i];
    return nullptr;
}

int App::CountFilesUnder(int folder_id, bool recursive) const
{
    int count = 0;
    for (const FileNode& n : project.files)
    {
        if (recursive)
        {
            int p = n.parent_id;
            while (p >= 0)
            {
                if (p == folder_id) { count++; break; }
                p = FindFile(p)->parent_id;         // FindFile(p) non-null: model invariant
            }
        }
        else if (n.parent_id == folder_id)
            count++;
    }
    if (!recursive) return count;
    // subtract the folder itself if it sits under folder_id (it doesn't; guard anyway)
    return count;
}

void App::Log(Severity s, const std::string& text)
{
    console.entries.push_back({ s, text });
    if (console.entries.size() > 4096)
        console.entries.erase(console.entries.begin());
}

void App::SimulateBuild()
{
    static int build_no = 0;
    char buf[96];
    snprintf(buf, sizeof(buf), "Build started (config: %s)...", project.config.c_str());
    Log(Severity::Info, buf);
    Log(Severity::Info, "Compiling renderer.cpp");
    Log(Severity::Info, "Compiling engine.cpp");
    Log(Severity::Warning, "engine.cpp(9): warning: unused variable 'deltaTime'");
    if (++build_no % 3 == 0)
        Log(Severity::Error, "renderer.cpp(4): error: 'swap_chain' was not declared in this scope");
    else
        Log(Severity::Info, "Linking...");
    Log(Severity::Success, build_no % 3 == 0 ? "Build FAILED (1 error, 1 warning)."
                                             : "Build completed successfully.");
    project_dirty = true;
}

void App::SimulateRun()
{
    char buf[128];
    snprintf(buf, sizeof(buf), "Running %s (%s)...", project.name.c_str(), project.config.c_str());
    Log(Severity::Info, buf);
    Log(Severity::Info, "[engine] window opened 1280x720");
    Log(Severity::Info, "[engine] main loop entered");
    Log(Severity::Error, "[engine] fatal: renderer not initialized (exit code 1)");
    Log(Severity::Warning, "Process exited with code 1.");
}

void App::DoSave()
{
    for (EditorDoc& d : docs)
    {
        if (d.file_id >= 0 && d.dirty)
            if (FileNode* f = FindFile(d.file_id)) { f->content = d.text; f->size_bytes = (int)d.text.size(); }
        d.dirty = false;
    }
    project_dirty = false;
    Log(Severity::Success, "Project saved.");
}

void App::Apply(const Action& a)
{
    switch (a.type)
    {
    case Action::NewProject:
        project = Project();
        project.name = "Untitled Project";
        docs.clear();
        docs.push_back({ -1, "Welcome", "New project created.\nEdit files from the Project Explorer.\n", false });
        active_doc = 0;
        selection = { 0, -1, -1 };
        project_dirty = true;
        Log(Severity::Info, "Created new project: Untitled Project.");
        break;

    case Action::Save:    DoSave(); break;
    case Action::SaveAs:
        project.name = a.text.empty() ? "Project Copy" : a.text;
        DoSave();
        Log(Severity::Info, "Saved As: " + project.name);
        break;

    case Action::OpenProject:
        Log(Severity::Info, "Open Project: no workspace persistence in this build (demo).");
        break;

    case Action::Exit:   pending_exit = true; break;

    case Action::TogglePanel:
        switch (a.panel) {
        case 0: panels.explorer  = !panels.explorer;  break;
        case 1: panels.board     = !panels.board;     break;
        case 2: panels.console   = !panels.console;   break; // NOLINT
        case 3: panels.inspector = !panels.inspector; break;
        }
        break;

    case Action::SelectFile:  selection = { 1, a.id, -1 }; break;
    case Action::SelectTask:  selection = { 2, -1, a.id }; break;

    case Action::CloseDoc:
        for (size_t x = 0; x < docs.size(); ++x)
            if (docs[x].file_id == a.id)
            {
                if (docs[x].dirty) Log(Severity::Warning, "Closed '" + docs[x].title + "' with unsaved changes.");
                docs.erase(docs.begin() + x);
                break;
            }
        if (active_doc >= (int)docs.size()) active_doc = (int)docs.size() - 1;
        if (active_doc < 0) active_doc = 0;
        break;

    case Action::CloseApp:
        for (const EditorDoc& d : docs)
            if (d.dirty) { Log(Severity::Warning, "Quit with unsaved changes in '" + d.title + "'."); break; }
        pending_exit = true;
        break;

    case Action::Build: SimulateBuild(); break;
    case Action::Run:   SimulateRun();   break;

    case Action::Config:
        project.config = a.text;
        Log(Severity::Info, "Configuration set to " + project.config + ".");
        break;

    case Action::Rename:
        if (FileNode* f = FindFile(a.id)) { f->name = a.text; project_dirty = true; }
        break;

    case Action::Delete:
    {
        int kill_idx = -1;
        for (size_t i = 0; i < project.files.size(); ++i)
            if (project.files[i].id == a.id) { kill_idx = (int)i; break; }
        if (kill_idx < 0) break;

        const std::string name = project.files[kill_idx].name;

        // ids to remove: node + all descendants
        std::vector<int> kill_ids{ project.files[kill_idx].id };
        for (const FileNode& n : project.files)
        {
            int p = n.parent_id;
            while (p >= 0)
            {
                if (p == a.id) { kill_ids.push_back(n.id); break; }
                p = FindFile(p)->parent_id;
            }
        }

        // close affected docs
        for (int fid : kill_ids)
            for (size_t d = 0; d < docs.size();)
            {
                if (docs[d].file_id == fid)
                {
                    if (docs[d].dirty) Log(Severity::Warning, "Deleted '" + docs[d].title + "' with unsaved changes.");
                    docs.erase(docs.begin() + d);
                }
                else ++d;
            }
        if (active_doc >= (int)docs.size()) active_doc = (int)docs.size() - 1;
        if (active_doc < 0) active_doc = 0;
        if (selection.kind == 1 && FindFile(selection.file_id) == nullptr)
            selection = { 0, -1, -1 };

        // rebuild flat vector, dropping doomed nodes; parent_id needs NO remap
        std::vector<FileNode> kept;
        for (const FileNode& n : project.files)
            if (std::find(kill_ids.begin(), kill_ids.end(), n.id) == kill_ids.end())
                kept.push_back(n);
        project.files = std::move(kept);

        Log(Severity::Info, "Deleted '" + name + "'.");
        project_dirty = true;
        break;
    }

    case Action::Duplicate:
        if (const FileNode* f = FindFile(a.id))
        {
            FileNode copy = *f;
            copy.id = project.next_id++;
            copy.name = f->name + " (copy)";
            project.files.push_back(copy);
            project_dirty = true;
            Log(Severity::Info, "Duplicated '" + f->name + "'.");
        }
        break;

    case Action::NewFile:
    {
        std::string fname = a.text.empty() ? "untitled.txt" : a.text;
        PushFile(*this, fname.c_str(), a.id >= 0 ? a.id : -1, "");
        project_dirty = true;
        Log(Severity::Info, "Created " + fname + ".");
        break;
    }

    case Action::AddTask:
        tasks.push_back({ project.next_id++, a.text.empty() ? "New Task" : a.text,
                          "Describe the task here.", "unassigned",
                          TaskStatus::Backlog, Priority::Medium, 0.0f });
        selection = { 2, -1, tasks.back().id };
        break;

    case Action::MoveTask:
        for (Task& t : tasks)
            if (t.id == a.id) { t.status = (TaskStatus)a.column; break; }
        break;

    case Action::DeleteTask:
        for (size_t i = 0; i < tasks.size(); ++i)
            if (tasks[i].id == a.id) { tasks.erase(tasks.begin() + i); break; }
        if (selection.kind == 2 && selection.task_id == a.id)
            selection = { 0, -1, -1 };
        break;

    case Action::OpenDoc:
        if (const FileNode* f = FindFile(a.id))
        {
            bool exists = false;
            for (const EditorDoc& d : docs) if (d.file_id == a.id) exists = true;
            if (!exists) docs.push_back({ a.id, f->name, f->content, false });
            // activate the opened tab (last one when newly appended)
            active_doc = (int)docs.size() - 1;
            // if it already existed, find its index instead
            if (exists)
                for (int i = 0; i < (int)docs.size(); ++i)
                    if (docs[i].file_id == a.id) active_doc = i;
        }
        break;

    case Action::ExecCommand: ExecuteCommand(a.command); break;

    case Action::ReopenWelcome:
        if (docs.empty() || docs[0].file_id != -1)
            docs.insert(docs.begin(), { -1, "Welcome", "Welcome back.\nOpen a file from the Project Explorer.\n", false });
        active_doc = 0;
        break;

    case Action::None: break;
    }
}

void App::HandleShortcuts()
{
    // Chords (Ctrl+S/N/B, Ctrl+P, Ctrl+comma, F5). The typed-input guard lives
    // in Panels.cpp's shortcut entry point (it owns the popup state).
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S)) { Apply({Action::Save}); return; }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_N)) { Apply({Action::NewProject}); return; }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_B)) { Apply({Action::Build}); return; }
    if (ImGui::IsKeyChordPressed(ImGuiKey_F5))                { Apply({Action::Run}); return; }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_P)) { RequestCommandPalette(); return; }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Comma)) { RequestPreferences(); return; }

    // Escape cascade: palette > prefs > about > rename > selection (Panels.cpp).
    if (ImGui::IsKeyPressed(ImGuiKey_Escape))
        HandleEscape();
}

void App::ExecuteCommand(int idx)
{
    switch (idx)
    {
    case 0: Apply({Action::NewProject}); break;
    case 1: Apply({Action::Save}); break;
    case 2: Apply({Action::Build}); break;
    case 3: Apply({Action::Run}); break;
    case 4: Apply({Action::TogglePanel, /*panel*/0}); break;
    case 5: Apply({Action::TogglePanel, /*panel*/1}); break;
    case 6: Apply({Action::TogglePanel, /*panel*/2}); break;
    case 7: Apply({Action::TogglePanel, /*panel*/3}); break;
    case 8: ide::RequestPreferences(); break;
    case 9: ide::RequestAbout(); break;
    default: break;
    }
}

// ---------------------------------------------------------------------------
// Command registry
// ---------------------------------------------------------------------------
struct Command { const char* name; const char* shortcut; };

static const Command kCommands[] = {
    { "New Project",            "Ctrl+N" },
    { "Save",                   "Ctrl+S" },
    { "Build",                  "Ctrl+B" },
    { "Run",                    "F5"     },
    { "Toggle Project Explorer", ""      },
    { "Toggle Task Board",       ""      },
    { "Toggle Output Console",   ""      },
    { "Toggle Inspector",        ""      },
    { "Open Preferences",        ""      },
    { "Show About",              ""      },
};

int  CommandCount() { return (int)(sizeof(kCommands) / sizeof(kCommands[0])); }
const char* CommandName(int idx)     { return kCommands[idx].name; }
const char* CommandShortcut(int idx) { return kCommands[idx].shortcut; }

} // namespace ide
