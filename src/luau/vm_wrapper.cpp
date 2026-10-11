#include "vm_wrapper.hpp"
#include <sstream>
#include <iomanip>

namespace luau {

ExecutorState::ExecutorState() {
    L_ = luaL_newstate();
    if (!L_) return;

    luaL_openlibs(L_);

    // Replace print with our wrapper (captures output)
    lua_pushcfunction(L_, print_wrapper);
    lua_setglobal(L_, "print");

    lua_pushcfunction(L_, warn_wrapper);
    lua_setglobal(L_, "warn");

    apply_sandbox();
}

ExecutorState::~ExecutorState() {
    if (L_) lua_close(L_);
}

ExecutorState::ExecutorState(ExecutorState&& other) noexcept
    : L_(other.L_), sandbox_level_(other.sandbox_level_),
      bytecode_cache_(std::move(other.bytecode_cache_)) {
    other.L_ = nullptr;
}

ExecutorState& ExecutorState::operator=(ExecutorState&& other) noexcept {
    if (this != &other) {
        if (L_) lua_close(L_);
        L_ = other.L_;
        sandbox_level_ = other.sandbox_level_;
        bytecode_cache_ = std::move(other.bytecode_cache_);
        other.L_ = nullptr;
    }
    return *this;
}

void ExecutorState::apply_sandbox() {
    if (!L_) return;

    // Level 1: Basic - remove os, io, debug
    if (sandbox_level_ >= 1) {
        lua_pushnil(L_); lua_setglobal(L_, "os");
        lua_pushnil(L_); lua_setglobal(L_, "io");
        lua_pushnil(L_); lua_setglobal(L_, "debug");
        lua_pushnil(L_); lua_setglobal(L_, "loadfile");
        lua_pushnil(L_); lua_setglobal(L_, "dofile");
    }

    // Level 2: Strict - remove require, module, package
    if (sandbox_level_ >= 2) {
        lua_pushnil(L_); lua_setglobal(L_, "require");
        lua_pushnil(L_); lua_setglobal(L_, "module");
        lua_getglobal(L_, "package");
        if (lua_istable(L_, -1)) {
            lua_pushnil(L_); lua_setfield(L_, -2, "loadlib");
            lua_pushnil(L_); lua_setfield(L_, -2, "seeall");
        }
        lua_pop(L_, 1);
    }

    // Level 3: Paranoid - restrict string, table, math metatables
    if (sandbox_level_ >= 3) {
        // Wrap metatable access
        lua_getglobal(L_, "getmetatable");
        if (lua_isfunction(L_, -1)) {
            lua_pushcfunction(L_, [](lua_State* L) -> int {
                // Block metatable access on protected types
                if (lua_type(L, 1) == LUA_TSTRING || lua_type(L, 1) == LUA_TTABLE) {
                    lua_pushnil(L);
                    return 1;
                }
                return lua_getmetatable(L, 1);
            });
            lua_setglobal(L_, "getmetatable");
        }
        lua_pop(L_, 1);

        lua_pushnil(L_); lua_setglobal(L_, "setmetatable");
        lua_pushnil(L_); lua_setglobal(L_, "rawget");
        lua_pushnil(L_); lua_setglobal(L_, "rawset");
        lua_pushnil(L_); lua_setglobal(L_, "rawequal");
    }
}

void ExecutorState::set_sandbox_level(int level) {
    sandbox_level_ = std::max(0, std::min(3, level));
    apply_sandbox();
}

void ExecutorState::add_global(const char* name, lua_CFunction func) {
    if (!L_ || !name) return;
    std::lock_guard lock(mutex_);
    lua_pushcfunction(L_, func);
    lua_setglobal(L_, name);
}

void ExecutorState::remove_global(const char* name) {
    if (!L_ || !name) return;
    std::lock_guard lock(mutex_);
    lua_pushnil(L_);
    lua_setglobal(L_, name);
}

CompileResult ExecutorState::compile(const std::string& source, const char* name,
                                     const CompileOptions* options) {
    CompileResult result;
    if (!L_) { result.error = "Lua state not initialized"; return result; }

    std::lock_guard lock(mutex_);

    CompileOptions opts;
    if (options) opts = *options;
    opts.optimizationLevel = 2; // O2
    opts.debugLevel = 1;        // Line info only
    opts.vectorLib = true;
    opts.vectorCtor = true;
    opts.vectorType = "vector";

    size_t bc_size = 0;
    const char* bc = luau_compile(source.c_str(), source.size(), &opts, &bc_size);

    if (!bc) {
        result.error = "Compilation failed";
        return result;
    }

    result.bytecode.assign(bc, bc + bc_size);
    result.success = true;
    return result;
}

ExecResult ExecutorState::execute(const std::vector<uint8_t>& bytecode) {
    ExecResult result;
    if (!L_) { result.error = "Lua state not initialized"; return result; }

    std::lock_guard lock(mutex_);

    int status = luaL_loadbuffer(L_, reinterpret_cast<const char*>(bytecode.data()),
                                 bytecode.size(), "@executor");
    if (status != LUA_OK) {
        result.error = lua_tostring(L_, -1);
        lua_pop(L_, 1);
        return result;
    }

    status = lua_pcall(L_, 0, LUA_MULTRET, 0);
    if (status != LUA_OK) {
        result.error = lua_tostring(L_, -1);
        lua_pop(L_, 1);
        return result;
    }

    // Capture return values
    int nresults = lua_gettop(L_);
    for (int i = 1; i <= nresults; ++i) {
        std::ostringstream oss;
        switch (lua_type(L_, i)) {
            case LUA_TNIL: oss << "nil"; break;
            case LUA_TBOOLEAN: oss << (lua_toboolean(L_, i) ? "true" : "false"); break;
            case LUA_TNUMBER: oss << lua_tonumber(L_, i); break;
            case LUA_TSTRING: oss << lua_tostring(L_, i); break;
            case LUA_TTABLE: oss << "table"; break;
            case LUA_TFUNCTION: oss << "function"; break;
            case LUA_TUSERDATA: oss << "userdata"; break;
            case LUA_TTHREAD: oss << "thread"; break;
            case LUA_TLIGHTUSERDATA: oss << "lightuserdata"; break;
            default: oss << "unknown";
        }
        result.returns.push_back(oss.str());
    }
    lua_pop(L_, nresults);

    result.success = true;
    return result;
}

ExecResult ExecutorState::execute_source(const std::string& source, const char* name) {
    auto compile_result = compile(source, name);
    if (!compile_result.success) {
        return {false, compile_result.error, {}};
    }
    return execute(compile_result.bytecode);
}

uint64_t ExecutorState::bench_compile_exec(const std::string& src, int runs) {
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < runs; ++i) {
        execute_source(src);
    }
    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count() / runs;
}

uint64_t ExecutorState::bench_compile(const std::string& src, int runs) {
    CompileOptions opts{};
    opts.optimizationLevel = 2;
    opts.debugLevel = 1;
    opts.vectorLib = true;
    opts.vectorCtor = true;
    opts.vectorType = "vector";

    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < runs; ++i) {
        size_t bc_size = 0;
        luau_compile(src.c_str(), src.size(), &opts, &bc_size);
    }
    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count() / runs;
}

uint64_t ExecutorState::bench_execute(const std::vector<uint8_t>& bytecode, int runs) {
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < runs; ++i) {
        std::lock_guard lock(mutex_);
        luaL_loadbuffer(L_, reinterpret_cast<const char*>(bytecode.data()), bytecode.size(), "@bench");
        lua_pcall(L_, 0, 0, 0);
    }
    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count() / runs;
}

int ExecutorState::print_wrapper(lua_State* L) {
    int n = lua_gettop(L);
    lua_getglobal(L, "tostring");
    for (int i = 1; i <= n; ++i) {
        lua_pushvalue(L, -1);
        lua_pushvalue(L, i);
        lua_call(L, 1, 1);
        const char* s = lua_tostring(L, -1);
        if (s) OutputDebugStringA(s);
        if (i < n) OutputDebugStringA("\t");
        lua_pop(L, 1);
    }
    OutputDebugStringA("\n");
    return 0;
}

int ExecutorState::warn_wrapper(lua_State* L) {
    int n = lua_gettop(L);
    lua_getglobal(L, "tostring");
    for (int i = 1; i <= n; ++i) {
        lua_pushvalue(L, -1);
        lua_pushvalue(L, i);
        lua_call(L, 1, 1);
        const char* s = lua_tostring(L, -1);
        if (s) OutputDebugStringA(s);
        if (i < n) OutputDebugStringA("\t");
        lua_pop(L, 1);
    }
    OutputDebugStringA("\n");
    return 0;
}

ExecutorState& global_executor() {
    static ExecutorState instance;
    return instance;
}

} // namespace luau