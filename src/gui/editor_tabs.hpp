#pragma once
#include <string>
#include <vector>
#include <memory>
#include <filesystem>
#include <chrono>
#include <fstream>
#include "imgui/imgui_impl.h"
#include "ipc/pipe_client.hpp"

namespace gui {

struct ScriptTab {
    std::string id;              // Unique identifier
    std::string name;            // Display name (filename or "Untitled")
    std::string path;            // Full file path (empty if unsaved)
    std::string content;         // Script content
    bool modified = false;       // Has unsaved changes
    bool is_new = true;          // Never saved
    std::chrono::system_clock::time_point last_modified;

    ScriptTab() { new_tab(); }
    explicit ScriptTab(const std::string& file_path) { load(file_path); }

    void new_tab(const std::string& name = "Untitled") {
        static int counter = 0;
        id = "tab_" + std::to_string(++counter) + "_" + std::to_string(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        this->name = name;
        path.clear();
        content.clear();
        modified = false;
        is_new = true;
        last_modified = std::chrono::system_clock::now();
    }

    bool load(const std::string& file_path) {
        std::ifstream f(file_path, std::ios::binary);
        if (!f) return false;
        content.assign(std::istreambuf_iterator<char>(f), {});
        path = file_path;
        name = std::filesystem::path(file_path).filename().string();
        modified = false;
        is_new = false;
        last_modified = std::chrono::system_clock::now();
        return true;
    }

    bool save() {
        if (path.empty()) return false;
        return save_as(path);
    }

    bool save_as(const std::string& new_path) {
        std::ofstream f(new_path, std::ios::binary);
        if (!f) return false;
        f.write(content.data(), content.size());
        path = new_path;
        name = std::filesystem::path(new_path).filename().string();
        modified = false;
        is_new = false;
        last_modified = std::chrono::system_clock::now();
        return true;
    }

    void set_content(const std::string& new_content) {
        content = new_content;
        modified = true;
        last_modified = std::chrono::system_clock::now();
    }

    void append_content(const std::string& addition) {
        content += addition;
        modified = true;
        last_modified = std::chrono::system_clock::now();
    }

    // JSON serialization for session persistence
    std::string to_json() const;
    static ScriptTab from_json(const std::string& json);
};

class TabEditor {
public:
    TabEditor();
    ~TabEditor();

    // Main render call - returns true if window should close
    bool render();

    // IPC connection
    void set_pipe_client(std::shared_ptr<ipc::PipeClient> client) { pipe_client_ = client; }
    bool connect_to_executor(const std::wstring& pipe_name = L"\\\\.\\pipe\\roblox_executor");

    // Tab operations
    void new_tab();
    void close_tab(size_t index);
    void close_tab_by_id(const std::string& id);
    void rename_tab(size_t index, const std::string& new_name);
    void duplicate_tab(size_t index);
    size_t get_tab_index(const std::string& id) const;

    // File operations
    bool open_file_dialog();
    bool save_file_dialog(size_t index);
    bool save_as_file_dialog(size_t index);
    void save_all();

    // Session persistence
    void save_session();      // Save tabs to Documents/RobloxExecutor/session.json
    void load_session();      // Load tabs from session file
    std::string get_session_path() const;

    // Execution
    void execute_current_tab();
    void execute_tab(size_t index);
    void inject_into_roblox();

    // State
    size_t active_tab_index() const { return active_tab_; }
    const std::vector<ScriptTab>& tabs() const { return tabs_; }
    ScriptTab& current_tab() { return tabs_[active_tab_]; }
    const ScriptTab& current_tab() const { return tabs_[active_tab_]; }

    // Settings
    void set_font_size(float size) { font_size_ = size; }
    void set_theme_dark(bool dark) { theme_dark_ = dark; }

private:
    // Rendering
    void render_menu_bar();
    void render_tab_bar();
    void render_editor(size_t index);
    void render_output_console();
    void render_status_bar();
    void render_find_replace();

    // Helpers
    void apply_theme();
    void update_window_title();
    bool confirm_close_modified(size_t index);
    std::string get_display_name(size_t index) const;

    // Syntax highlighting (basic)
    void render_highlighted_editor(ScriptTab& tab);

    // JSON helpers
    std::string escape_json(const std::string& s) const;
    std::string unescape_json(const std::string& s) const;

    // Data
    std::vector<ScriptTab> tabs_;
    size_t active_tab_ = 0;
    std::shared_ptr<ipc::PipeClient> pipe_client_;
    bool executor_connected_ = false;

    // UI state
    bool show_output_ = true;
    bool show_find_ = false;
    std::string find_text_;
    std::string replace_text_;
    std::vector<std::string> output_lines_;
    std::string status_text_ = "Ready";
    float font_size_ = 14.0f;
    bool theme_dark_ = true;

    // Window
    bool should_close_ = false;
    std::string window_title_ = "Roblox Executor";
};

} // namespace gui