#include <windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include "unc/unc_path.hpp"
#include "injection/manual_map.hpp"
#include "luau/vm_wrapper.hpp"
#include "ipc/pipe_server.hpp"

namespace fs = std::filesystem;

static const wchar_t* PIPE_NAME = L"\\\\.\\pipe\\roblox_executor";

void print_usage() {
    std::cout << "Roblox Executor v1.0\n";
    std::cout << "Usage:\n";
    std::cout << "  executor.exe --inject <pid|process_name> <dll_path>     Inject DLL\n";
    std::cout << "  executor.exe --execute <script.lua>                       Execute Luau script\n";
    std::cout << "  executor.exe --server                                     Start IPC server\n";
    std::cout << "  executor.exe --bench [unc|inject|luau] [path]           Run benchmarks\n";
    std::cout << "  executor.exe --self-test                                  Run internal tests\n";
    std::cout << "  executor.exe --version                                    Show version\n";
}

int cmd_inject(int argc, wchar_t* argv[]) {
    if (argc < 4) {
        std::wcerr << L"Usage: --inject <pid|name> <dll_path>\n";
        return 1;
    }

    DWORD pid = 0;
    std::wstring target = argv[2];
    std::wstring dll_path = argv[3];

    // Try as PID first
    try {
        pid = std::stoul(target);
    } catch (...) {
        // Try as process name
        pid = inject::find_process(target);
        if (!pid) {
            std::wcerr << L"Process not found: " << target << L"\n";
            return 1;
        }
    }

    std::wcout << L"Injecting into PID " << pid << L" from " << dll_path << L"...\n";

    uintptr_t base = inject::inject_from_file(pid, dll_path);
    if (base) {
        std::wcout << L"Injected at 0x" << std::hex << base << L"\n";
        return 0;
    } else {
        std::wcerr << L"Injection failed\n";
        return 1;
    }
}

int cmd_execute(int argc, wchar_t* argv[]) {
    if (argc < 3) {
        std::wcerr << L"Usage: --execute <script.lua>\n";
        return 1;
    }

    std::wstring script_path = argv[2];
    auto script_data = unc::read_file(script_path);
    if (script_data.empty()) {
        std::wcerr << L"Failed to read script: " << script_path << L"\n";
        return 1;
    }

    std::string source(reinterpret_cast<char*>(script_data.data()), script_data.size());
    auto result = luau::run_script(source);

    if (result.success) {
        std::cout << "Script executed successfully\n";
        for (const auto& ret : result.returns) {
            std::cout << "  Return: " << ret << "\n";
        }
        return 0;
    } else {
        std::cerr << "Execution failed: " << result.error << "\n";
        return 1;
    }
}

int cmd_server(int argc, wchar_t* argv[]) {
    std::cout << "Starting IPC server on " << PIPE_NAME << "...\n";

    ipc::PipeServer server(PIPE_NAME);

    // Register handlers
    server.on("execute", [](const ipc::Message& msg) -> std::string {
        auto result = luau::run_script(msg.payload);
        if (result.success) {
            return ipc::json::make_object({
                {"success", "true"},
                {"returns", ipc::json::make_array(result.returns)}
            });
        }
        return ipc::json::make_object({
            {"success", "false"},
            {"error", result.error}
        });
    });

    server.on("inject", [](const ipc::Message& msg) -> std::string {
        // Parse payload as JSON: {"pid": 1234, "dll": "path"}
        auto pid_str = ipc::json::get_field(msg.payload, "pid");
        auto dll_str = ipc::json::get_field(msg.payload, "dll");
        if (!pid_str || !dll_str) {
            return ipc::json::make_object({{"success", "false"}, {"error", "invalid payload"}});
        }

        DWORD pid = static_cast<DWORD>(std::stoul(*pid_str));
        std::wstring dll_path(dll_str->begin(), dll_str->end());
        uintptr_t base = inject::inject_from_file(pid, dll_path);

        if (base) {
            char buf[32];
            sprintf_s(buf, "0x%llx", base);
            return ipc::json::make_object({{"success", "true"}, {"base", buf}});
        }
        return ipc::json::make_object({{"success", "false"}, {"error", "injection failed"}});
    });

    server.on("bench", [](const ipc::Message& msg) -> std::string {
        auto type = ipc::json::get_field(msg.payload, "type");
        auto path = ipc::json::get_field(msg.payload, "path");

        if (type && *type == "unc" && path) {
            std::wstring wpath(path->begin(), path->end());
            auto result = unc::bench_full(wpath, 100);
            return ipc::json::make_object({
                {"write_avg_us", std::to_string(result.write_us_avg)},
                {"write_p50_us", std::to_string(result.write_us_p50)},
                {"write_p99_us", std::to_string(result.write_us_p99)},
                {"read_avg_us", std::to_string(result.read_us_avg)},
                {"read_p50_us", std::to_string(result.read_us_p50)},
                {"read_p99_us", std::to_string(result.read_us_p99)},
                {"acl_overhead_us", std::to_string(result.acl_overhead_us)}
            });
        }
        return ipc::json::make_object({{"success", "false"}, {"error", "invalid bench request"}});
    });

    server.on("version", [](const ipc::Message&) -> std::string {
        return ipc::json::make_object({{"version", "1.0.0"}, {"build", "Release"}});
    });

    server.run(); // Blocks
    return 0;
}

int cmd_bench(int argc, wchar_t* argv[]) {
    std::string type = "all";
    std::wstring path;

    if (argc >= 3) {
        char buf[256];
        wcstombs(buf, argv[2], 256);
        type = buf;
    }
    if (argc >= 4) {
        path = argv[3];
    }

    if (type == "unc" || type == "all") {
        if (path.empty()) path = L"\\\\localhost\\C$\\temp\\bench_test.bin";
        std::wcout << L"UNC Benchmark: " << path << L"\n";
        auto result = unc::bench_full(path, 1000);
        std::wcout << L"  Write: " << result.write_us_avg << L" us avg, "
                   << result.write_us_p50 << L" us p50, " << result.write_us_p99 << L" us p99\n";
        std::wcout << L"  Read:  " << result.read_us_avg << L" us avg, "
                   << result.read_us_p50 << L" us p50, " << result.read_us_p99 << L" us p99\n";
        std::wcout << L"  ACL overhead: " << result.acl_overhead_us << L" us\n";
    }

    if (type == "luau" || type == "all") {
        std::string test_script = R"(
local sum = 0
for i = 1, 1000 do
    sum = sum + i
end
return sum
)";
        std::cout << "Luau Benchmark:\n";
        uint64_t compile_exec = luau::global_executor().bench_compile_exec(test_script, 10000);
        uint64_t compile_only = luau::global_executor().bench_compile(test_script, 100000);

        auto bc_result = luau::global_executor().compile(test_script);
        uint64_t exec_only = 0;
        if (bc_result.success) {
            exec_only = luau::global_executor().bench_execute(bc_result.bytecode, 100000);
        }

        std::cout << "  Compile+Exec: " << compile_exec << " ns\n";
        std::cout << "  Compile only: " << compile_only << " ns\n";
        std::cout << "  Exec only:    " << exec_only << " ns\n";
    }

    if (type == "inject" || type == "all") {
        std::cout << "Injection Benchmark (requires target process):\n";
        std::cout << "  Skipping - run with --inject to test\n";
    }

    return 0;
}

int cmd_self_test(int argc, wchar_t* argv[]) {
    std::cout << "Running self-tests...\n";
    int passed = 0, failed = 0;

    // Test UNC path normalization
    {
        std::wstring p1 = unc::to_extended_unc(L"C:\\test");
        std::wstring p2 = unc::to_extended_unc(L"\\\\server\\share\\file");
        std::wstring p3 = unc::to_extended_unc(L"\\\\?\\C:\\already");
        bool ok = (p1 == L"\\\\?\\C:\\test") &&
                  (p2 == L"\\\\?\\UNC\\server\\share\\file") &&
                  (p3 == L"\\\\?\\C:\\already");
        if (ok) { std::cout << "[PASS] UNC normalization\n"; ++passed; }
        else { std::cout << "[FAIL] UNC normalization\n"; ++failed; }
    }

    // Test Luau execution
    {
        auto result = luau::run_script("return 2 + 2");
        bool ok = result.success && result.returns.size() == 1 && result.returns[0] == "4";
        if (ok) { std::cout << "[PASS] Luau basic execution\n"; ++passed; }
        else { std::cout << "[FAIL] Luau basic execution\n"; ++failed; }
    }

    // Test Luau sandbox
    {
        auto result = luau::run_script("return os.clock()");
        bool ok = !result.success; // os should be nil
        if (ok) { std::cout << "[PASS] Luau sandbox (os blocked)\n"; ++passed; }
        else { std::cout << "[FAIL] Luau sandbox\n"; ++failed; }
    }

    // Test JSON
    {
        std::string obj = ipc::json::make_object({{"key", "value"}, {"num", "42"}});
        auto val = ipc::json::get_field(obj, "key");
        bool ok = val && *val == "value";
        if (ok) { std::cout << "[PASS] JSON serialization\n"; ++passed; }
        else { std::cout << "[FAIL] JSON serialization\n"; ++failed; }
    }

    std::cout << "\nResults: " << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}

int wmain(int argc, wchar_t* argv[]) {
    SetConsoleOutputCP(CP_UTF8);

    if (argc < 2) {
        print_usage();
        return 1;
    }

    std::string cmd;
    {
        char buf[256];
        wcstombs(buf, argv[1], 256);
        cmd = buf;
    }

    if (cmd == "--inject") return cmd_inject(argc, argv);
    if (cmd == "--execute") return cmd_execute(argc, argv);
    if (cmd == "--server") return cmd_server(argc, argv);
    if (cmd == "--bench") return cmd_bench(argc, argv);
    if (cmd == "--self-test") return cmd_self_test(argc, argv);
    if (cmd == "--version") {
        std::cout << "Roblox Executor v1.0.0 (Release)\n";
        return 0;
    }

    std::cerr << "Unknown command: " << cmd << "\n";
    print_usage();
    return 1;
}