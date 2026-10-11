#pragma once
#include <windows.h>
#include <string>
#include <optional>
#include <mutex>
#include <atomic>
#include "pipe_server.hpp" // For json namespace

namespace ipc {

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

} // namespace ipc