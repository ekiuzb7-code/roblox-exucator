#pragma once
#include <string>
#include <vector>
#include <chrono>
#include <optional>
#include <mutex>
#include <unordered_map>

extern "C" {
#include "lua.h"
#include "lualib.h"
#include "luacode.h"
#include "luavm.h"
}

namespace luau {

struct CompileResult {
    std::vector<uint8_t> bytecode;
    std::string error;
    bool success = false;
};

struct ExecResult {
    bool success = false;
    std::string error;
    std::vector<std::string> returns; // stringified return values
};

class ExecutorState {
public:
    ExecutorState();
    ~ExecutorState();

    // Non-copyable, movable
    ExecutorState(const ExecutorState&) = delete;
    ExecutorState& operator=(const ExecutorState&) = delete;
    ExecutorState(ExecutorState&&) noexcept;
    ExecutorState& operator=(ExecutorState&&) noexcept;

    // Compile Luau source to bytecode
    CompileResult compile(const std::string& source, const char* name = "@executor",
                          const CompileOptions* options = nullptr);

    // Execute bytecode
    ExecResult execute(const std::vector<uint8_t>& bytecode);

    // Compile + execute in one call
    ExecResult execute_source(const std::string& source, const char* name = "@executor");

    // Sandbox configuration
    void set_sandbox_level(int level); // 0=none, 1=basic, 2=strict, 3=paranoid
    void add_global(const char* name, lua_CFunction func);
    void remove_global(const char* name);

    // Benchmark: average ns per compile+exec over runs
    uint64_t bench_compile_exec(const std::string& src, int runs = 1000);

    // Benchmark: compile only
    uint64_t bench_compile(const std::string& src, int runs = 10000);

    // Benchmark: execute only (pre-compiled)
    uint64_t bench_execute(const std::vector<uint8_t>& bytecode, int runs = 10000);

    // Get Lua state for advanced usage
    lua_State* state() { return L_; }

private:
    lua_State* L_ = nullptr;
    std::mutex mutex_;
    int sandbox_level_ = 2;
    std::unordered_map<std::string, std::vector<uint8_t>> bytecode_cache_;

    void apply_sandbox();
    static int print_wrapper(lua_State* L);
    static int warn_wrapper(lua_State* L);
};

// Global executor instance (thread-safe)
ExecutorState& global_executor();

// High-level API
inline ExecResult run_script(const std::string& source) {
    return global_executor().execute_source(source);
}

inline CompileResult compile_script(const std::string& source) {
    return global_executor().compile(source);
}

} // namespace luau