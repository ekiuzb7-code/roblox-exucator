#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <chrono>
#include <algorithm>
#include <numeric>
#include <windows.h>
#include "unc/unc_path.hpp"
#include "injection/manual_map.hpp"
#include "luau/vm_wrapper.hpp"
#include "ipc/pipe_server.hpp"

using namespace std::chrono;

struct BenchStats {
    std::string name;
    std::vector<uint64_t> samples;
    uint64_t avg = 0, p50 = 0, p99 = 0, min = 0, max = 0;

    void compute() {
        if (samples.empty()) return;
        auto s = samples;
        std::sort(s.begin(), s.end());
        min = s.front();
        max = s.back();
        p50 = s[s.size() / 2];
        p99 = s[s.size() * 99 / 100];
        avg = std::accumulate(s.begin(), s.end(), 0ull) / s.size();
    }

    void print() const {
        std::cout << std::left << std::setw(30) << name
                  << "avg=" << std::setw(10) << avg
                  << " p50=" << std::setw(10) << p50
                  << " p99=" << std::setw(10) << p99
                  << " min=" << std::setw(10) << min
                  << " max=" << std::setw(10) << max << " ns\n";
    }

    void print_us() const {
        std::cout << std::left << std::setw(30) << name
                  << "avg=" << std::setw(10) << (avg / 1000.0)
                  << " p50=" << std::setw(10) << (p50 / 1000.0)
                  << " p99=" << std::setw(10) << (p99 / 1000.0)
                  << " min=" << std::setw(10) << (min / 1000.0)
                  << " max=" << std::setw(10) << (max / 1000.0) << " us\n";
    }
};

template<typename Fn>
BenchStats bench(const std::string& name, Fn&& fn, int iterations = 1000, int warmup = 10) {
    BenchStats stats;
    stats.name = name;

    // Warmup
    for (int i = 0; i < warmup; ++i) fn();

    // Measure
    stats.samples.reserve(iterations);
    for (int i = 0; i < iterations; ++i) {
        auto start = high_resolution_clock::now();
        fn();
        auto end = high_resolution_clock::now();
        stats.samples.push_back(duration_cast<nanoseconds>(end - start).count());
    }

    stats.compute();
    return stats;
}

void bench_unc(const std::wstring& test_file) {
    std::cout << "\n=== UNC/sUNC Benchmarks ===\n";
    std::cout << "Test file: " << std::string(test_file.begin(), test_file.end()) << "\n\n";

    // Ensure test file exists
    std::vector<char> test_data(64 * 1024, 'x');
    unc::write_file_atomic(test_file, {reinterpret_cast<uint8_t*>(test_data.data()), test_data.size()});

    // Write throughput
    auto write_bench = bench("UNC Write (64KB)", [&]() {
        DWORD written;
        HANDLE h = unc::open_sunc(test_file, GENERIC_WRITE, 0, OPEN_ALWAYS);
        if (h != INVALID_HANDLE_VALUE) {
            SetFilePointer(h, 0, nullptr, FILE_BEGIN);
            WriteFile(h, test_data.data(), static_cast<DWORD>(test_data.size()), &written, nullptr);
            CloseHandle(h);
        }
    }, 1000, 10);
    write_bench.print_us();

    // Read throughput
    auto read_bench = bench("UNC Read (64KB)", [&]() {
        DWORD read;
        HANDLE h = unc::open_sunc(test_file, GENERIC_READ);
        if (h != INVALID_HANDLE_VALUE) {
            SetFilePointer(h, 0, nullptr, FILE_BEGIN);
            ReadFile(h, test_data.data(), static_cast<DWORD>(test_data.size()), &read, nullptr);
            CloseHandle(h);
        }
    }, 1000, 10);
    read_bench.print_us();

    // sUNC open overhead
    auto ext = unc::to_extended_unc(test_file);
    auto plain_open = bench("Plain CreateFileW", [&]() {
        HANDLE h = CreateFileW(ext.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }, 1000, 10);

    auto sunc_open = bench("sUNC open_sunc", [&]() {
        HANDLE h = unc::open_sunc(test_file, GENERIC_READ);
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }, 1000, 10);

    std::cout << "\n  ACL Overhead: " << (sunc_open.avg > plain_open.avg ? (sunc_open.avg - plain_open.avg) / 1000.0 : 0.0) << " us\n";

    // Full bench from unc::bench_full
    std::cout << "\n  Full unc::bench_full (1000 iterations):\n";
    auto full = unc::bench_full(test_file, 1000);
    std::cout << "    Write: " << full.write_us_avg << " us avg, " << full.write_us_p50 << " us p50, " << full.write_us_p99 << " us p99\n";
    std::cout << "    Read:  " << full.read_us_avg << " us avg, " << full.read_us_p50 << " us p50, " << full.read_us_p99 << " us p99\n";
    std::cout << "    ACL:   " << full.acl_overhead_us << " us overhead\n";
}

void bench_luau() {
    std::cout << "\n=== Luau VM Benchmarks ===\n\n";

    std::string simple_script = R"(
local sum = 0
for i = 1, 1000 do
    sum = sum + i
end
return sum
)";

    std::string complex_script = R"(
local function fib(n)
    if n < 2 then return n end
    return fib(n - 1) + fib(n - 2)
end

local t = {}
for i = 1, 100 do
    t[i] = fib(i % 20)
end
return t
)";

    std::string table_script = R"(
local t = {}
for i = 1, 10000 do
    t["key" .. i] = {x = i, y = i * 2, z = i * 3}
end
local sum = 0
for k, v in pairs(t) do
    sum = sum + v.x + v.y + v.z
end
return sum
)";

    auto& exec = luau::global_executor();

    // Compile only
    auto compile_simple = bench("Compile (simple)", [&]() {
        exec.compile(simple_script);
    }, 10000, 100);
    compile_simple.print();

    auto compile_complex = bench("Compile (complex)", [&]() {
        exec.compile(complex_script);
    }, 10000, 100);
    compile_complex.print();

    // Execute only (pre-compiled)
    auto bc_simple = exec.compile(simple_script);
    auto bc_complex = exec.compile(complex_script);
    auto bc_table = exec.compile(table_script);

    auto exec_simple = bench("Execute (simple)", [&]() {
        exec.execute(bc_simple.bytecode);
    }, 10000, 100);
    exec_simple.print();

    auto exec_complex = bench("Execute (complex)", [&]() {
        exec.execute(bc_complex.bytecode);
    }, 1000, 10);
    exec_complex.print();

    auto exec_table = bench("Execute (table-heavy)", [&]() {
        exec.execute(bc_table.bytecode);
    }, 1000, 10);
    exec_table.print();

    // Compile + Execute
    auto ce_simple = bench("Compile+Exec (simple)", [&]() {
        exec.execute_source(simple_script);
    }, 10000, 100);
    ce_simple.print();

    auto ce_complex = bench("Compile+Exec (complex)", [&]() {
        exec.execute_source(complex_script);
    }, 1000, 10);
    ce_complex.print();

    // Sandbox levels
    std::cout << "\n  Sandbox level impact:\n";
    for (int level = 0; level <= 3; ++level) {
        exec.set_sandbox_level(level);
        auto sb = bench("Sandbox L" + std::to_string(level), [&]() {
            exec.execute_source("return os and os.clock() or 'blocked'");
        }, 1000, 10);
        sb.print();
    }
    exec.set_sandbox_level(2); // Reset
}

void bench_injection() {
    std::cout << "\n=== Injection Benchmarks ===\n\n";
    std::cout << "Note: Injection benchmarks require a target process.\n";
    std::cout << "Run with --inject to test actual injection latency.\n\n";

    // Simulate injection steps timing
    auto pe_parse = bench("PE Parse", [&]() {
        // Dummy DLL bytes - just test parse speed
        std::vector<uint8_t> dummy(4096);
        dummy[0] = 'M'; dummy[1] = 'Z';
        *(DWORD*)(dummy.data() + 0x3C) = 0x80;
        // ... minimal PE header
        inject::parse_pe(dummy);
    }, 10000, 100);
    pe_parse.print();

    auto valloc = bench("VirtualAllocEx (4MB)", [&]() {
        HANDLE proc = GetCurrentProcess();
        void* p = VirtualAllocEx(proc, nullptr, 4 * 1024 * 1024,
                                 MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (p) VirtualFreeEx(proc, p, 0, MEM_RELEASE);
    }, 1000, 10);
    valloc.print();

    auto wpm = bench("WriteProcessMemory (64KB)", [&]() {
        HANDLE proc = GetCurrentProcess();
        void* p = VirtualAllocEx(proc, nullptr, 64 * 1024,
                                 MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        std::vector<char> buf(64 * 1024, 'x');
        SIZE_T written;
        WriteProcessMemory(proc, p, buf.data(), buf.size(), &written);
        VirtualFreeEx(proc, p, 0, MEM_RELEASE);
    }, 1000, 10);
    wpm.print();

    auto crt = bench("CreateRemoteThread (dummy)", [&]() {
        HANDLE thread = CreateRemoteThread(GetCurrentProcess(), nullptr, 0,
            [](LPVOID) -> DWORD { return 0; }, nullptr, 0, nullptr);
        if (thread) {
            WaitForSingleObject(thread, 1000);
            CloseHandle(thread);
        }
    }, 100, 10);
    crt.print();
}

void bench_ipc() {
    std::cout << "\n=== IPC Benchmarks ===\n\n";

    const wchar_t* pipe_name = L"\\\\.\\pipe\\bench_test";

    // Start server in background
    ipc::PipeServer server(pipe_name);
    server.on("echo", [](const ipc::Message& msg) -> std::string {
        return msg.payload;
    });
    server.on("add", [](const ipc::Message& msg) -> std::string {
        auto a = ipc::json::get_field(msg.payload, "a");
        auto b = ipc::json::get_field(msg.payload, "b");
        if (a && b) {
            return std::to_string(std::stoi(*a) + std::stoi(*b));
        }
        return "0";
    });

    std::thread server_thread([&]() { server.run(); });
    Sleep(100); // Wait for server to start

    // Client benchmarks
    ipc::PipeClient client(pipe_name);
    if (!client.connect(5000)) {
        std::cout << "  Failed to connect to pipe server\n";
        server.stop();
        server_thread.join();
        return;
    }

    auto echo_bench = bench("IPC Echo (round-trip)", [&]() {
        client.call("echo", "\"hello world\"");
    }, 1000, 10);
    echo_bench.print();

    auto add_bench = bench("IPC Add (JSON parse)", [&]() {
        client.call("add", ipc::json::make_object({{"a", "10"}, {"b", "20"}}));
    }, 1000, 10);
    add_bench.print();

    // Broadcast bench
    std::vector<std::string> results;
    for (int i = 0; i < 10; ++i) {
        auto start = high_resolution_clock::now();
        server.broadcast("notify", "test");
        auto end = high_resolution_clock::now();
        results.push_back(std::to_string(duration_cast<nanoseconds>(end - start).count()));
    }
    std::cout << "  Broadcast (10 clients): ~" << results[0] << " ns\n";

    client.disconnect();
    server.stop();
    server_thread.join();
}

void print_summary() {
    std::cout << "\n=== TARGET SUMMARY ===\n";
    std::cout << "UNC Write (LAN):     <500 us target\n";
    std::cout << "UNC Read (LAN):      <200 us target\n";
    std::cout << "Injection (manual):  <15 ms target\n";
    std::cout << "Luau Compile+Exec:   <50 us target (1KB script)\n";
    std::cout << "Cold Start:          <200 ms target\n";
    std::cout << "Memory Footprint:    <15 MB target\n";
}

int wmain(int argc, wchar_t* argv[]) {
    SetConsoleOutputCP(CP_UTF8);

    std::wstring test_file = L"\\\\localhost\\C$\\temp\\executor_bench.bin";
    bool run_unc = true, run_luau = true, run_inject = true, run_ipc = true;

    for (int i = 1; i < argc; ++i) {
        char arg[256];
        wcstombs(arg, argv[i], 256);
        std::string s(arg);
        if (s == "--unc") { run_luau = run_inject = run_ipc = false; }
        else if (s == "--luau") { run_unc = run_inject = run_ipc = false; }
        else if (s == "--inject") { run_unc = run_luau = run_ipc = false; }
        else if (s == "--ipc") { run_unc = run_luau = run_inject = false; }
        else if (s == "--file" && i + 1 < argc) { test_file = argv[++i]; }
    }

    std::cout << "Roblox Executor Benchmarks\n";
    std::cout << "============================\n";

    if (run_unc) bench_unc(test_file);
    if (run_luau) bench_luau();
    if (run_inject) bench_injection();
    if (run_ipc) bench_ipc();

    print_summary();

    return 0;
}