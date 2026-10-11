#include "pipe_server.hpp"
#include <sstream>
#include <algorithm>

namespace ipc {

PipeServer::PipeServer(const std::wstring& pipe_name) : pipe_name_(pipe_name) {}

PipeServer::~PipeServer() {
    stop();
}

void PipeServer::run() {
    running_.store(true);
    accept_loop();
}

void PipeServer::start_async() {
    if (running_.exchange(true)) return;
    accept_thread_ = std::thread(&PipeServer::accept_loop, this);
}

void PipeServer::stop() {
    running_.store(false);

    // Close all client pipes to unblock reads
    std::lock_guard lock(clients_mutex_);
    for (HANDLE h : client_pipes_) {
        if (h != INVALID_HANDLE_VALUE) {
            CancelIoEx(h, nullptr);
            CloseHandle(h);
        }
    }
    client_pipes_.clear();

    // Close listening pipe (created in accept_loop)
    // Note: we'd need to store the listening handle to close it properly

    if (accept_thread_.joinable()) accept_thread_.join();
    for (auto& t : client_threads_) {
        if (t.joinable()) t.join();
    }
    client_threads_.clear();
}

void PipeServer::on(const std::string& method, Handler handler) {
    std::lock_guard lock(handlers_mutex_);
    handlers_[method] = std::move(handler);
}

bool PipeServer::respond(HANDLE pipe, uint32_t msg_id, const std::string& payload, bool error) {
    std::string response = json::make_object({
        {"id", std::to_string(msg_id)},
        {"error", error ? "true" : "false"},
        {"payload", payload}
    });

    DWORD written;
    return WriteFile(pipe, response.c_str(), static_cast<DWORD>(response.size()), &written, nullptr)
        && written == response.size();
}

void PipeServer::broadcast(const std::string& method, const std::string& payload) {
    std::string msg = json::make_object({
        {"method", method},
        {"payload", payload}
    });

    std::lock_guard lock(clients_mutex_);
    for (HANDLE pipe : client_pipes_) {
        if (pipe != INVALID_HANDLE_VALUE) {
            DWORD written;
            WriteFile(pipe, msg.c_str(), static_cast<DWORD>(msg.size()), &written, nullptr);
        }
    }
}

void PipeServer::accept_loop() {
    while (running_.load()) {
        HANDLE pipe = CreateNamedPipeW(
            pipe_name_.c_str(),
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            65536, 65536, 0, nullptr);

        if (pipe == INVALID_HANDLE_VALUE) {
            Sleep(100);
            continue;
        }

        BOOL connected = ConnectNamedPipe(pipe, nullptr) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
        if (!connected) {
            CloseHandle(pipe);
            continue;
        }

        {
            std::lock_guard lock(clients_mutex_);
            client_pipes_.push_back(pipe);
        }

        client_threads_.emplace_back(&PipeServer::client_loop, this, pipe);
    }
}

void PipeServer::client_loop(HANDLE pipe) {
    char buffer[65536];
    DWORD read;

    while (running_.load()) {
        BOOL ok = ReadFile(pipe, buffer, sizeof(buffer) - 1, &read, nullptr);
        if (!ok || read == 0) break;

        buffer[read] = '\0';
        std::string json_msg(buffer);

        // Parse minimal JSON
        uint32_t msg_id = 0;
        std::string method, payload;

        auto id_opt = json::get_field(json_msg, "id");
        if (id_opt) msg_id = static_cast<uint32_t>(std::stoul(*id_opt));

        method = json::get_field(json_msg, "method").value_or("");
        payload = json::get_field(json_msg, "payload").value_or("");

        if (method.empty()) {
            respond(pipe, msg_id, "missing method", true);
            continue;
        }

        Handler handler;
        {
            std::lock_guard lock(handlers_mutex_);
            auto it = handlers_.find(method);
            if (it != handlers_.end()) handler = it->second;
        }

        if (handler) {
            std::string result = handler({msg_id, method, payload});
            respond(pipe, msg_id, result, false);
        } else {
            respond(pipe, msg_id, "method not found: " + method, true);
        }
    }

    // Cleanup
    CloseHandle(pipe);
    std::lock_guard lock(clients_mutex_);
    client_pipes_.erase(std::remove(client_pipes_.begin(), client_pipes_.end(), pipe), client_pipes_.end());
}

DWORD WINAPI PipeServer::instance_thread(LPVOID param) {
    auto* args = reinterpret_cast<std::pair<PipeServer*, HANDLE>*>(param);
    args->first->client_loop(args->second);
    delete args;
    return 0;
}

// PipeClient implementation
PipeClient::PipeClient(const std::wstring& pipe_name) : pipe_name_(pipe_name) {}

PipeClient::~PipeClient() {
    disconnect();
}

bool PipeClient::connect(uint32_t timeout_ms) {
    if (connected_.load()) return true;

    DWORD start = GetTickCount();
    while (GetTickCount() - start < timeout_ms) {
        pipe_ = CreateFileW(pipe_name_.c_str(), GENERIC_READ | GENERIC_WRITE,
                            0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (pipe_ != INVALID_HANDLE_VALUE) {
            connected_.store(true);
            return true;
        }
        if (GetLastError() != ERROR_PIPE_BUSY) break;
        WaitNamedPipeW(pipe_name_.c_str(), 100);
    }
    return false;
}

void PipeClient::disconnect() {
    if (pipe_ != INVALID_HANDLE_VALUE) {
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
    }
    connected_.store(false);
}

std::optional<std::string> PipeClient::call(const std::string& method, const std::string& payload,
                                            uint32_t timeout_ms) {
    if (!connected_.load() && !connect()) return std::nullopt;

    uint32_t msg_id = next_id_++;
    std::lock_guard lock(send_mutex_);

    if (!send_message(msg_id, method, payload)) {
        connected_.store(false);
        return std::nullopt;
    }

    return read_response(msg_id, timeout_ms);
}

bool PipeClient::notify(const std::string& method, const std::string& payload) {
    if (!connected_.load() && !connect()) return false;

    uint32_t msg_id = next_id_++;
    std::lock_guard lock(send_mutex_);
    return send_message(msg_id, method, payload);
}

bool PipeClient::send_message(uint32_t id, const std::string& method, const std::string& payload) {
    std::string msg = json::make_object({
        {"id", std::to_string(id)},
        {"method", method},
        {"payload", payload}
    });

    DWORD written;
    return WriteFile(pipe_, msg.c_str(), static_cast<DWORD>(msg.size()), &written, nullptr)
        && written == msg.size();
}

std::optional<std::string> PipeClient::read_response(uint32_t expected_id, uint32_t timeout_ms) {
    DWORD start = GetTickCount();
    char buffer[65536];

    while (GetTickCount() - start < timeout_ms) {
        DWORD read;
        BOOL ok = ReadFile(pipe_, buffer, sizeof(buffer) - 1, &read, nullptr);
        if (!ok || read == 0) {
            connected_.store(false);
            return std::nullopt;
        }

        buffer[read] = '\0';
        std::string response(buffer);

        auto id_opt = json::get_field(response, "id");
        if (id_opt && std::stoul(*id_opt) == expected_id) {
            auto error = json::get_field(response, "error");
            if (error && *error == "true") {
                return json::get_field(response, "payload");
            }
            return json::get_field(response, "payload");
        }
        // Not our response, keep reading
    }
    return std::nullopt;
}

// JSON helpers (minimal implementation)
namespace json {

std::string escape(const std::string& s) {
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

std::string make_object(std::initializer_list<std::pair<std::string, std::string>> fields) {
    std::string out = "{";
    bool first = true;
    for (auto& f : fields) {
        if (!first) out += ",";
        out += "\"" + escape(f.first) + "\":";
        // Check if value looks like a JSON primitive (bool, number, null) or string
        if (f.second == "true" || f.second == "false" || f.second == "null" ||
            (f.second.size() > 0 && isdigit(f.second[0])) ||
            (f.second.size() > 1 && f.second[0] == '-' && isdigit(f.second[1]))) {
            out += f.second;
        } else if (f.second.size() >= 2 && f.second[0] == '{') {
            out += f.second; // nested object
        } else if (f.second.size() >= 2 && f.second[0] == '[') {
            out += f.second; // array
        } else {
            out += "\"" + escape(f.second) + "\"";
        }
        first = false;
    }
    out += "}";
    return out;
}

std::string make_array(const std::vector<std::string>& items) {
    std::string out = "[";
    for (size_t i = 0; i < items.size(); ++i) {
        if (i) out += ",";
        out += "\"" + escape(items[i]) + "\"";
    }
    out += "]";
    return out;
}

std::optional<std::string> get_field(const std::string& json, const std::string& field) {
    std::string key = "\"" + field + "\"";
    size_t pos = json.find(key);
    if (pos == std::string::npos) return std::nullopt;

    pos = json.find(':', pos);
    if (pos == std::string::npos) return std::nullopt;
    ++pos;

    // Skip whitespace
    while (pos < json.size() && isspace(json[pos])) ++pos;

    if (pos >= json.size()) return std::nullopt;

    if (json[pos] == '"') {
        // String value
        ++pos;
        size_t start = pos;
        while (pos < json.size() && json[pos] != '"') {
            if (json[pos] == '\\') pos += 2;
            else ++pos;
        }
        if (pos >= json.size()) return std::nullopt;
        std::string val = json.substr(start, pos - start);
        // Unescape
        std::string unescaped;
        for (size_t i = 0; i < val.size(); ++i) {
            if (val[i] == '\\' && i + 1 < val.size()) {
                char c = val[i + 1];
                if (c == '"' || c == '\\' || c == '/') unescaped += c;
                else if (c == 'b') unescaped += '\b';
                else if (c == 'f') unescaped += '\f';
                else if (c == 'n') unescaped += '\n';
                else if (c == 'r') unescaped += '\r';
                else if (c == 't') unescaped += '\t';
                else if (c == 'u' && i + 5 < val.size()) {
                    // Unicode escape - simplified
                    i += 5;
                } else {
                    unescaped += val[i + 1];
                }
                ++i;
            } else {
                unescaped += val[i];
            }
        }
        return unescaped;
    } else if (json[pos] == '{' || json[pos] == '[') {
        // Object or array - find matching bracket
        char open = json[pos];
        char close = (open == '{') ? '}' : ']';
        int depth = 0;
        size_t start = pos;
        while (pos < json.size()) {
            if (json[pos] == open) ++depth;
            else if (json[pos] == close) {
                --depth;
                if (depth == 0) {
                    ++pos;
                    break;
                }
            }
            ++pos;
        }
        return json.substr(start, pos - start);
    } else {
        // Number, boolean, null
        size_t start = pos;
        while (pos < json.size() && (isalnum(json[pos]) || json[pos] == '.' || json[pos] == '-')) ++pos;
        return json.substr(start, pos - start);
    }
}

std::vector<std::string> get_array(const std::string& json, const std::string& field) {
    std::vector<std::string> result;
    auto arr_opt = get_field(json, field);
    if (!arr_opt) return result;

    std::string arr = *arr_opt;
    size_t pos = 0;
    while (pos < arr.size()) {
        while (pos < arr.size() && isspace(arr[pos])) ++pos;
        if (pos >= arr.size()) break;
        if (arr[pos] == ']') break;
        if (arr[pos] == ',') { ++pos; continue; }

        if (arr[pos] == '"') {
            ++pos;
            size_t start = pos;
            while (pos < arr.size() && arr[pos] != '"') {
                if (arr[pos] == '\\') pos += 2;
                else ++pos;
            }
            result.push_back(arr.substr(start, pos - start));
            ++pos;
        } else {
            size_t start = pos;
            while (pos < arr.size() && arr[pos] != ',' && arr[pos] != ']') ++pos;
            result.push_back(arr.substr(start, pos - start));
        }
    }
    return result;
}

} // namespace json
} // namespace ipc