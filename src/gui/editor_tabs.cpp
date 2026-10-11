#include "editor_tabs.hpp"
#include "ipc/pipe_client.hpp"
#include <fstream>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>

namespace gui {

TabEditor::TabEditor() {
    apply_theme();
    load_session(); // Load saved tabs on startup
}

TabEditor::~TabEditor() {
    save_session(); // Save tabs on close
    if (pipe_client_) pipe_client_->disconnect();
}

void TabEditor::apply_theme() {
    if (theme_dark_) {
        ImGui::StyleColorsDark();
    } else {
        ImGui::StyleColorsLight();
    }
}

bool TabEditor::render() {
    ImGui::NewFrame();

    render_menu_bar();
    render_tab_bar();

    if (!tabs_.empty()) {
        render_editor(active_tab_);
    }

    if (show_output_) {
        render_output_console();
    }

    if (show_find_) {
        render_find_replace();
    }

    render_status_bar();

    ImGui::EndFrame();
    ImGui::Render();

    return should_close_;
}

void TabEditor::render_menu_bar() {
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Tab", "Ctrl+N")) new_tab();
            if (ImGui::MenuItem("Open...", "Ctrl+O")) open_file_dialog();
            ImGui::Separator();
            if (ImGui::MenuItem("Save", "Ctrl+S", false, !tabs_.empty() && tabs_[active_tab_].modified)) {
                if (tabs_[active_tab_].path.empty()) save_as_file_dialog(active_tab_);
                else tabs_[active_tab_].save();
            }
            if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S", false, !tabs_.empty())) {
                save_as_file_dialog(active_tab_);
            }
            if (ImGui::MenuItem("Save All", "Ctrl+Alt+S", false, std::any_of(tabs_.begin(), tabs_.end(), [](const auto& t){ return t.modified; }))) {
                save_all();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Close Tab", "Ctrl+W", false, !tabs_.empty())) {
                close_tab(active_tab_);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) should_close_ = true;
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z")) {}
            if (ImGui::MenuItem("Redo", "Ctrl+Y")) {}
            ImGui::Separator();
            if (ImGui::MenuItem("Cut", "Ctrl+X")) {}
            if (ImGui::MenuItem("Copy", "Ctrl+C")) {}
            if (ImGui::MenuItem("Paste", "Ctrl+V")) {}
            ImGui::Separator();
            if (ImGui::MenuItem("Find/Replace", "Ctrl+F")) show_find_ = true;
            if (ImGui::MenuItem("Duplicate Tab", "Ctrl+D", false, !tabs_.empty())) duplicate_tab(active_tab_);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Execute")) {
            bool can_execute = !tabs_.empty() && !tabs_[active_tab_].content.empty();
            if (ImGui::MenuItem("Run Script", "F5", false, can_execute && executor_connected_)) execute_current_tab();
            if (ImGui::MenuItem("Inject into Roblox", "F6", false, executor_connected_)) inject_into_roblox();
            ImGui::Separator();
            if (ImGui::MenuItem("Connect to Executor...", nullptr, false, !executor_connected_)) {
                connect_to_executor();
            }
            if (ImGui::MenuItem("Disconnect", nullptr, false, executor_connected_)) {
                if (pipe_client_) pipe_client_->disconnect();
                executor_connected_ = false;
                status_text_ = "Disconnected";
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Output Console", nullptr, show_output_)) show_output_ = !show_output_;
            ImGui::Separator();
            if (ImGui::MenuItem("Dark Theme", nullptr, theme_dark_)) { theme_dark_ = true; apply_theme(); }
            if (ImGui::MenuItem("Light Theme", nullptr, !theme_dark_)) { theme_dark_ = false; apply_theme(); }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("About")) {
                // TODO: About dialog
            }
            ImGui::EndMenu();
        }

        ImGui::EndMenuBar();
    }
}

void TabEditor::render_tab_bar() {
    ImGuiTabBarFlags flags = ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_AutoSelectNewTabs | ImGuiTabBarFlags_TabListPopupButton;

    if (ImGui::BeginTabBar("ScriptTabs", flags)) {
        for (size_t i = 0; i < tabs_.size(); ++i) {
            ScriptTab& tab = tabs_[i];
            bool is_active = (i == active_tab_);

            ImGuiTabItemFlags tab_flags = ImGuiTabItemFlags_None;
            if (tab.modified) tab_flags |= ImGuiTabItemFlags_UnsavedDocument;

            bool open = true;
            std::string label = get_display_name(i);

            if (ImGui::BeginTabItem(label.c_str(), &open, tab_flags)) {
                active_tab_ = i;
                ImGui::EndTabItem();
            }

            if (!open) {
                close_tab(i);
                break; // Tab was closed, array changed
            }
        }

        // Execute button (trailing, before Inject)
        bool can_execute = !tabs_.empty() && !tabs_[active_tab_].content.empty();
        if (executor_connected_ && can_execute) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Run")) {
                execute_current_tab();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Execute script (F5)");
        }

        // Inject button (trailing, before +)
        if (executor_connected_) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Inject")) {
                inject_into_roblox();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Inject into Roblox (F6)");
        }

        // "+" button to add new tab (Chrome-style, trailing)
        if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip)) {
            new_tab();
        }

        ImGui::EndTabBar();
    }
}

std::string TabEditor::get_display_name(size_t index) const {
    const auto& tab = tabs_[index];
    std::string name = tab.name;
    if (tab.modified) name += "*";
    return name;
}

void TabEditor::render_editor(size_t index) {
    ScriptTab& tab = tabs_[index];

    ImGui::BeginChild("EditorRegion", ImVec2(0, -100), true, ImGuiWindowFlags_HorizontalScrollbar);

    // Font
    ImGui::PushFont(ImGui::GetFont());
    ImGui::GetFont()->Scale = font_size_ / 14.0f;

    // Editor with syntax highlighting
    static char buffer[1024 * 1024]; // 1MB buffer
    if (buffer[0] == 0 || strcmp(buffer, tab.content.c_str()) != 0) {
        strncpy_s(buffer, tab.content.c_str(), sizeof(buffer) - 1);
    }

    ImGuiInputTextFlags editor_flags = ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_Multiline;
    ImVec2 size = ImGui::GetContentRegionAvail();
    size.y = std::max(size.y, 200.0f);

    if (ImGui::InputTextMultiline("##editor", buffer, sizeof(buffer), size, editor_flags)) {
        tab.set_content(buffer);
    }

    ImGui::PopFont();
    ImGui::EndChild();
}

void TabEditor::render_output_console() {
    ImGui::BeginChild("OutputConsole", ImVec2(0, 150), true);
    ImGui::Text("Output Console");
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) output_lines_.clear();
    ImGui::Separator();

    for (const auto& line : output_lines_) {
        ImGui::TextUnformatted(line.c_str());
    }

    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20) {
        ImGui::SetScrollHereY(1.0f);
    }

    ImGui::EndChild();
}

void TabEditor::render_find_replace() {
    ImGui::SetNextWindowSize(ImVec2(400, 120), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Find/Replace", &show_find_)) {
        ImGui::InputText("Find", &find_text_);
        ImGui::InputText("Replace", &replace_text_);

        if (ImGui::Button("Find Next")) {
            // TODO: Implement find in current tab
        }
        ImGui::SameLine();
        if (ImGui::Button("Replace")) {
            // TODO: Implement replace in current tab
        }
        ImGui::SameLine();
        if (ImGui::Button("Replace All")) {
            // TODO: Implement replace all in current tab
        }
    }
    ImGui::End();
}

void TabEditor::render_status_bar() {
    ImGui::BeginChild("StatusBar", ImVec2(0, 22), false, ImGuiWindowFlags_NoScrollbar);
    ImGui::Text("%s", status_text_.c_str());
    ImGui::SameLine(ImGui::GetWindowWidth() - 200);
    if (executor_connected_) {
        ImGui::TextColored(ImVec4(0, 1, 0, 1), "Connected");
    } else {
        ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "Disconnected");
    }
    ImGui::SameLine(ImGui::GetWindowWidth() - 100);
    ImGui::Text("Ln %zu, Col %zu", 1, 1); // TODO: Actual cursor position
    ImGui::EndChild();
}

void TabEditor::new_tab() {
    tabs_.emplace_back();
    active_tab_ = tabs_.size() - 1;
    status_text_ = "New tab created";
}

void TabEditor::close_tab(size_t index) {
    if (index >= tabs_.size()) return;

    if (tabs_[index].modified && !confirm_close_modified(index)) {
        return;
    }

    tabs_.erase(tabs_.begin() + index);
    if (tabs_.empty()) {
        new_tab();
    } else if (active_tab_ >= tabs_.size()) {
        active_tab_ = tabs_.size() - 1;
    }
    status_text_ = "Tab closed";
}

bool TabEditor::confirm_close_modified(size_t index) {
    // Simple confirmation - in real app would show modal dialog
    return true; // For now, always allow
}

void TabEditor::rename_tab(size_t index, const std::string& new_name) {
    if (index < tabs_.size() && !new_name.empty()) {
        tabs_[index].name = new_name;
        status_text_ = "Tab renamed to " + new_name;
    }
}

void TabEditor::duplicate_tab(size_t index) {
    if (index >= tabs_.size()) return;
    ScriptTab new_tab = tabs_[index];
    new_tab.id = "tab_" + std::to_string(tabs_.size() + 1) + "_" +
                 std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::system_clock::now().time_since_epoch()).count());
    new_tab.name += " (copy)";
    new_tab.path.clear();
    new_tab.is_new = true;
    tabs_.insert(tabs_.begin() + index + 1, std::move(new_tab));
    active_tab_ = index + 1;
    status_text_ = "Tab duplicated";
}

size_t TabEditor::get_tab_index(const std::string& id) const {
    for (size_t i = 0; i < tabs_.size(); ++i) {
        if (tabs_[i].id == id) return i;
    }
    return SIZE_MAX;
}

bool TabEditor::open_file_dialog() {
    OPENFILENAMEW ofn = {};
    wchar_t szFile[MAX_PATH] = {0};

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"Luau Files\0*.lua;*.luau\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT;
    ofn.lpstrTitle = L"Open Luau Script";

    if (GetOpenFileNameW(&ofn)) {
        std::wstring wpath(ofn.lpstrFile);
        // Handle multi-select
        size_t offset = 0;
        while (offset < wpath.size()) {
            std::wstring file = wpath.substr(offset);
            if (file.empty()) break;
            if (tabs_[active_tab_].content.empty() && tabs_[active_tab_].path.empty() && !tabs_[active_tab_].modified) {
                tabs_[active_tab_].load(std::string(file.begin(), file.end()));
            } else {
                ScriptTab tab;
                tab.load(std::string(file.begin(), file.end()));
                tabs_.push_back(std::move(tab));
                active_tab_ = tabs_.size() - 1;
            }
            offset += file.size() + 1;
        }
        status_text_ = "File(s) opened";
        return true;
    }
    return false;
}

bool TabEditor::save_file_dialog(size_t index) {
    if (index >= tabs_.size()) return false;
    if (tabs_[index].path.empty()) return save_as_file_dialog(index);
    return tabs_[index].save();
}

bool TabEditor::save_as_file_dialog(size_t index) {
    if (index >= tabs_.size()) return false;

    OPENFILENAMEW ofn = {};
    wchar_t szFile[MAX_PATH] = {0};

    // Default filename
    std::wstring default_name(tabs_[index].name.begin(), tabs_[index].name.end());
    if (default_name.empty()) default_name = L"script.lua";
    wcscpy_s(szFile, default_name.c_str());

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"Luau Files\0*.lua;*.luau\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
    ofn.lpstrTitle = L"Save Luau Script";
    ofn.lpstrDefExt = L"lua";

    if (GetSaveFileNameW(&ofn)) {
        std::string path(ofn.lpstrFile, ofn.lpstrFile + wcslen(ofn.lpstrFile));
        if (tabs_[index].save_as(path)) {
            status_text_ = "Saved: " + path;
            return true;
        }
    }
    return false;
}

void TabEditor::save_all() {
    int saved = 0;
    for (size_t i = 0; i < tabs_.size(); ++i) {
        if (tabs_[i].modified) {
            if (tabs_[i].path.empty()) {
                save_as_file_dialog(i);
            } else {
                tabs_[i].save();
            }
            ++saved;
        }
    }
    status_text_ = "Saved " + std::to_string(saved) + " file(s)";
}

bool TabEditor::connect_to_executor(const std::wstring& pipe_name) {
    pipe_client_ = std::make_shared<ipc::PipeClient>(pipe_name);
    if (pipe_client_->connect(5000)) {
        executor_connected_ = true;
        status_text_ = "Connected to executor";
        // Test connection
        auto result = pipe_client_->call("version", "");
        if (result) {
            output_lines_.push_back("[Executor] Connected: " + *result);
        }
        return true;
    }
    pipe_client_.reset();
    executor_connected_ = false;
    status_text_ = "Failed to connect to executor";
    return false;
}

void TabEditor::execute_current_tab() {
    if (tabs_.empty() || !executor_connected_) return;
    execute_tab(active_tab_);
}

void TabEditor::execute_tab(size_t index) {
    if (index >= tabs_.size() || !executor_connected_) return;

    ScriptTab& tab = tabs_[index];
    if (tab.content.empty()) {
        output_lines_.push_back("[Error] Empty script");
        return;
    }

    output_lines_.push_back("[Executor] Running: " + tab.name);

    // Escape content for JSON
    std::string escaped = tab.content;
    // Simple escape for JSON string
    std::string json_payload = "\"" + escaped + "\""; // Simplified

    auto result = pipe_client_->call("execute", tab.content);
    if (result) {
        output_lines_.push_back("[Executor] Result: " + *result);
    } else {
        output_lines_.push_back("[Error] Execution failed or timeout");
    }
}

void TabEditor::inject_into_roblox() {
    if (!executor_connected_) {
        output_lines_.push_back("[Error] Not connected to executor");
        return;
    }

    // Find Roblox process
    DWORD pid = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe{};
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(snap, &pe)) {
            do {
                if (_wcsicmp(pe.szExeFile, L"RobloxPlayerBeta.exe") == 0) {
                    pid = pe.th32ProcessID;
                    break;
                }
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
    }

    if (pid == 0) {
        output_lines_.push_back("[Error] RobloxPlayerBeta.exe not found");
        return;
    }

    // Get injector DLL path
    wchar_t exe_path[MAX_PATH];
    GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    std::filesystem::path dll_path = std::filesystem::path(exe_path).parent_path() / "injector.dll";

    std::string payload = ipc::json::make_object({
        {"pid", std::to_string(pid)},
        {"dll", std::string(dll_path.begin(), dll_path.end())}
    });

    output_lines_.push_back("[Executor] Injecting into PID " + std::to_string(pid));

    auto result = pipe_client_->call("inject", payload);
    if (result) {
        output_lines_.push_back("[Executor] Injection result: " + *result);
    } else {
        output_lines_.push_back("[Error] Injection failed or timeout");
    }
}

// Session persistence
std::string TabEditor::get_session_path() const {
    wchar_t docs_path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, 0, docs_path))) {
        std::filesystem::path session_dir = std::filesystem::path(docs_path) / "RobloxExecutor";
        std::filesystem::create_directories(session_dir);
        return (session_dir / "session.json").string();
    }
    return "session.json"; // fallback
}

void TabEditor::save_session() {
    std::string session_path = get_session_path();
    std::ofstream f(session_path);
    if (!f) {
        status_text_ = "Failed to save session";
        return;
    }

    f << "{\n";
    f << "  \"active_tab\": " << active_tab_ << ",\n";
    f << "  \"tabs\": [\n";

    for (size_t i = 0; i < tabs_.size(); ++i) {
        const auto& tab = tabs_[i];
        f << "    {\n";
        f << "      \"id\": \"" << escape_json(tab.id) << "\",\n";
        f << "      \"name\": \"" << escape_json(tab.name) << "\",\n";
        f << "      \"path\": \"" << escape_json(tab.path) << "\",\n";
        f << "      \"content\": \"" << escape_json(tab.content) << "\",\n";
        f << "      \"modified\": " << (tab.modified ? "true" : "false") << ",\n";
        f << "      \"is_new\": " << (tab.is_new ? "true" : "false") << "\n";
        f << "    }";
        if (i + 1 < tabs_.size()) f << ",";
        f << "\n";
    }

    f << "  ]\n";
    f << "}\n";

    status_text_ = "Session saved to " + session_path;
}

void TabEditor::load_session() {
    std::string session_path = get_session_path();
    std::ifstream f(session_path);
    if (!f) {
        // No session file, start fresh
        new_tab();
        return;
    }

    std::string json((std::istreambuf_iterator<char>(f)), {});
    if (json.empty()) {
        new_tab();
        return;
    }

    // Simple JSON parsing for our known structure
    // Find "tabs" array
    size_t tabs_pos = json.find("\"tabs\"");
    if (tabs_pos == std::string::npos) {
        new_tab();
        return;
    }

    size_t arr_start = json.find('[', tabs_pos);
    size_t arr_end = json.find(']', arr_start);
    if (arr_start == std::string::npos || arr_end == std::string::npos) {
        new_tab();
        return;
    }

    std::string tabs_json = json.substr(arr_start + 1, arr_end - arr_start - 1);

    // Parse each tab object
    size_t pos = 0;
    while (pos < tabs_json.size()) {
        // Find next {
        size_t obj_start = tabs_json.find('{', pos);
        if (obj_start == std::string::npos) break;
        size_t obj_end = tabs_json.find('}', obj_start);
        if (obj_end == std::string::npos) break;

        std::string tab_json = tabs_json.substr(obj_start, obj_end - obj_start + 1);

        ScriptTab tab;
        tab.id = unescape_json(extract_json_field(tab_json, "id"));
        tab.name = unescape_json(extract_json_field(tab_json, "name"));
        tab.path = unescape_json(extract_json_field(tab_json, "path"));
        tab.content = unescape_json(extract_json_field(tab_json, "content"));
        tab.modified = extract_json_field(tab_json, "modified") == "true";
        tab.is_new = extract_json_field(tab_json, "is_new") == "true";

        tabs_.push_back(std::move(tab));
        pos = obj_end + 1;
    }

    // Parse active_tab
    size_t active_pos = json.find("\"active_tab\"");
    if (active_pos != std::string::npos) {
        size_t val_start = json.find(':', active_pos);
        if (val_start != std::string::npos) {
            size_t val_end = json.find_first_of(",}", val_start);
            std::string active_str = json.substr(val_start + 1, val_end - val_start - 1);
            active_tab_ = std::stoul(active_str);
            if (active_tab_ >= tabs_.size()) active_tab_ = 0;
        }
    }

    if (tabs_.empty()) new_tab();
    status_text_ = "Session loaded: " + std::to_string(tabs_.size()) + " tabs";
}

std::string TabEditor::escape_json(const std::string& s) const {
    std::string out;
    out.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    sprintf_s(buf, "\\u%04x", static_cast<unsigned char>(c));
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

std::string TabEditor::unescape_json(const std::string& s) const {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char c = s[i + 1];
            if (c == '"' || c == '\\' || c == '/') out += c;
            else if (c == 'b') out += '\b';
            else if (c == 'f') out += '\f';
            else if (c == 'n') out += '\n';
            else if (c == 'r') out += '\r';
            else if (c == 't') out += '\t';
            else if (c == 'u' && i + 5 < s.size()) {
                // Unicode - simplified
                i += 5;
            } else {
                out += s[i + 1];
            }
            ++i;
        } else {
            out += s[i];
        }
    }
    return out;
}

std::string TabEditor::extract_json_field(const std::string& json, const std::string& field) {
    std::string key = "\"" + field + "\"";
    size_t pos = json.find(key);
    if (pos == std::string::npos) return "";

    pos = json.find(':', pos);
    if (pos == std::string::npos) return "";
    ++pos;

    // Skip whitespace
    while (pos < json.size() && isspace(json[pos])) ++pos;
    if (pos >= json.size()) return "";

    if (json[pos] == '"') {
        // String value
        ++pos;
        size_t start = pos;
        while (pos < json.size() && json[pos] != '"') {
            if (json[pos] == '\\') pos += 2;
            else ++pos;
        }
        if (pos >= json.size()) return "";
        return json.substr(start, pos - start);
    } else if (json[pos] == 't' || json[pos] == 'f') {
        // true/false
        size_t start = pos;
        while (pos < json.size() && isalpha(json[pos])) ++pos;
        return json.substr(start, pos - start);
    } else {
        // Number
        size_t start = pos;
        while (pos < json.size() && (isdigit(json[pos]) || json[pos] == '.' || json[pos] == '-')) ++pos;
        return json.substr(start, pos - start);
    }
}

// ScriptTab JSON serialization
std::string ScriptTab::to_json() const {
    std::ostringstream oss;
    oss << "{";
    oss << "\"id\":\"" << id << "\",";
    oss << "\"name\":\"" << name << "\",";
    oss << "\"path\":\"" << path << "\",";
    oss << "\"content\":\"" << content << "\",";
    oss << "\"modified\":" << (modified ? "true" : "false") << ",";
    oss << "\"is_new\":" << (is_new ? "true" : "false");
    oss << "}";
    return oss.str();
}

ScriptTab ScriptTab::from_json(const std::string& json) {
    ScriptTab tab;
    // Would use the same parsing as TabEditor::load_session
    return tab;
}

} // namespace gui