#pragma once
#include <windows.h>
#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <vector>
#include <mutex>
#include <queue>
#include <condition_variable>

namespace ipc {

struct Message {
    uint32_t id = 0;
    std::string method;
    std::string payload; // JSON
};

using Handler = std::function<std::string(const Message&)>;

class PipeServer {
public:
    explicit PipeServer(const std::wstring& pipe_name);
    ~PipeServer();

    // Start listening (blocking)
    void run();

    // Start in background thread
    void start_async();

    // Stop server
    void stop();

    // Register handler for method
    void on(const std::string& method, Handler handler);

    // Send response to client
    bool respond(HANDLE pipe, uint32_t msg_id, const std::string& payload, bool error = false);

    // Broadcast to all connected clients
    void broadcast(const std::string& method, const std::string& payload);

    bool is_running() const { return running_.load(); }

private:
    void accept_loop();
    void client_loop(HANDLE pipe);
    static DWORD WINAPI instance_thread(LPVOID param);

    std::wstring pipe_name_;
    std::atomic<bool> running_{false};
    std::thread accept_thread_;
    std::vector<std::thread> client_threads_;
    std::mutex handlers_mutex_;
    std::unordered_map<std::string, Handler> handlers_;
    std::mutex clients_mutex_;
    std::vector<HANDLE> client_pipes_;
};

class PipeClient {
public:
    explicit PipeClient(const std::wstring& pipe_name);
    ~PipeClient();

    bool connect(uint32_t timeout_ms = 5000);
    void disconnect();

    // Send request, wait for response
    std::optional<std::string> call(const std::string& method, const std::string& payload,
                                    uint32_t timeout_ms = 10000);

    // Fire and forget
    bool notify(const std::string& method, const std::string& payload);

    bool is_connected() const { return connected_.load(); }

private:
    std::wstring pipe_name_;
    HANDLE pipe_ = INVALID_HANDLE_VALUE;
    std::atomic<bool> connected_{false};
    std::mutex send_mutex_;
    uint32_t next_id_ = 1;

    bool send_message(uint32_t id, const std::string& method, const std::string& payload);
    std::optional<std::string> read_response(uint32_t expected_id, uint32_t timeout_ms);
};

// JSON helpers (minimal, no external deps)
namespace json {
    std::string make_object(std::initializer_list<std::pair<std::string, std::string>> fields);
    std::string make_array(const std::vector<std::string>& items);
    std::string escape(const std::string& s);
    std::optional<std::string> get_field(const std::string& json, const std::string& field);
    std::vector<std::string> get_array(const std::string& json, const std::string& field);
}

} // namespace ipc