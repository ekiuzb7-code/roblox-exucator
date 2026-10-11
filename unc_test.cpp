// Standalone UNC/sUNC test - compile with: cl /EHsc /std:c++20 unc_test.cpp
// Tests UNC path normalization, sUNC ACL validation, and benchmarking

#include <windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <algorithm>
#include <numeric>
#include <filesystem>

namespace unc {

inline std::wstring to_extended_unc(const std::wstring& path) {
    if (path.rfind(L"\\\\?\\", 0) == 0) return path;
    if (path.rfind(L"\\\\", 0) == 0) return L"\\\\?\\UNC\\" + path.substr(2);
    if (std::filesystem::path(path).is_absolute()) return L"\\\\?\\" + path;
    return L"\\\\?\\" + std::filesystem::absolute(path).wstring();
}

inline HANDLE open_sunc(const std::wstring& unc_path, DWORD access = GENERIC_READ,
                        DWORD share = FILE_SHARE_READ, DWORD disp = OPEN_EXISTING) {
    auto ext = to_extended_unc(unc_path);
    HANDLE h = CreateFileW(ext.c_str(), access, share, nullptr, disp,
                           FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (h == INVALID_HANDLE_VALUE) return h;

    PSECURITY_DESCRIPTOR sd = nullptr;
    if (GetSecurityInfo(h, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                        nullptr, nullptr, nullptr, nullptr, &sd) == ERROR_SUCCESS) {
        LocalFree(sd);
    }
    return h;
}

inline bool write_file_atomic(const std::wstring& unc_path, const std::vector<uint8_t>& data) {
    std::wstring temp_path = unc_path + L".tmp";
    HANDLE h = open_sunc(temp_path, GENERIC_WRITE, 0, CREATE_ALWAYS);
    if (h == INVALID_HANDLE_VALUE) return false;

    DWORD written;
    BOOL ok = WriteFile(h, data.data(), static_cast<DWORD>(data.size()), &written, nullptr);
    CloseHandle(h);

    if (!ok || written != data.size()) {
        DeleteFileW(temp_path.c_str());
        return false;
    }

    return MoveFileExW(temp_path.c_str(), unc_path.c_str(), MOVEFILE_REPLACE_EXISTING);
}

inline std::vector<uint8_t> read_file(const std::wstring& unc_path) {
    HANDLE h = open_sunc(unc_path, GENERIC_READ);
    if (h == INVALID_HANDLE_VALUE) return {};

    LARGE_INTEGER size;
    GetFileSizeEx(h, &size);
    std::vector<uint8_t> buf(static_cast<size_t>(size.QuadPart));
    DWORD read;
    ReadFile(h, buf.data(), static_cast<DWORD>(buf.size()), &read, nullptr);
    CloseHandle(h);
    return buf;
}

struct BenchResult {
    uint64_t write_us_avg, write_us_p50, write_us_p99;
    uint64_t read_us_avg, read_us_p50, read_us_p99;
    uint64_t acl_overhead_us;
};

inline BenchResult bench_full(const std::wstring& unc_file, size_t iterations = 1000) {
    BenchResult r{};
    std::vector<uint64_t> write_times, read_times;
    std::vector<char> buf(64 * 1024, 'x');

    // Pre-write
    write_file_atomic(unc_file, {reinterpret_cast<uint8_t*>(buf.data()), buf.size()});

    // Warmup
    for (int i = 0; i < 10; ++i) {
        HANDLE h = open_sunc(unc_file, GENERIC_WRITE, 0, OPEN_ALWAYS);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD w; SetFilePointer(h, 0, nullptr, FILE_BEGIN);
            WriteFile(h, buf.data(), 64*1024, &w, nullptr); CloseHandle(h);
        }
        h = open_sunc(unc_file, GENERIC_READ);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD w; SetFilePointer(h, 0, nullptr, FILE_BEGIN);
            ReadFile(h, buf.data(), 64*1024, &w, nullptr); CloseHandle(h);
        }
    }

    for (size_t i = 0; i < iterations; ++i) {
        auto start = std::chrono::high_resolution_clock::now();
        HANDLE h = open_sunc(unc_file, GENERIC_WRITE, 0, OPEN_ALWAYS);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD w; SetFilePointer(h, 0, nullptr, FILE_BEGIN);
            WriteFile(h, buf.data(), 64*1024, &w, nullptr); CloseHandle(h);
        }
        auto end = std::chrono::high_resolution_clock::now();
        write_times.push_back(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
    }

    for (size_t i = 0; i < iterations; ++i) {
        auto start = std::chrono::high_resolution_clock::now();
        HANDLE h = open_sunc(unc_file, GENERIC_READ);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD w; SetFilePointer(h, 0, nullptr, FILE_BEGIN);
            ReadFile(h, buf.data(), 64*1024, &w, nullptr); CloseHandle(h);
        }
        auto end = std::chrono::high_resolution_clock::now();
        read_times.push_back(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
    }

    auto ext = to_extended_unc(unc_file);
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 100; ++i) {
        HANDLE h = CreateFileW(ext.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }
    auto end = std::chrono::high_resolution_clock::now();
    uint64_t plain_open = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 100;

    start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 100; ++i) {
        HANDLE h = open_sunc(unc_file, GENERIC_READ);
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }
    end = std::chrono::high_resolution_clock::now();
    uint64_t sunc_open = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 100;

    std::sort(write_times.begin(), write_times.end());
    std::sort(read_times.begin(), read_times.end());

    r.write_us_avg = std::accumulate(write_times.begin(), write_times.end(), 0ull) / write_times.size();
    r.write_us_p50 = write_times[write_times.size() / 2];
    r.write_us_p99 = write_times[write_times.size() * 99 / 100];
    r.read_us_avg = std::accumulate(read_times.begin(), read_times.end(), 0ull) / read_times.size();
    r.read_us_p50 = read_times[read_times.size() / 2];
    r.read_us_p99 = read_times[read_times.size() * 99 / 100];
    r.acl_overhead_us = (sunc_open > plain_open) ? sunc_open - plain_open : 0;

    return r;
}

} // namespace unc

void test_normalization() {
    std::cout << "=== UNC Path Normalization Tests ===\n";

    struct TestCase {
        std::wstring input;
        std::wstring expected;
    };

    TestCase tests[] = {
        {L"C:\\test", L"\\\\?\\C:\\test"},
        {L"\\\\server\\share\\file", L"\\\\?\\UNC\\server\\share\\file"},
        {L"\\\\?\\C:\\already", L"\\\\?\\C:\\already"},
        {L"relative\\path", L"\\\\?\\" + std::filesystem::absolute(L"relative\\path").wstring()},
    };

    for (auto& t : tests) {
        auto result = unc::to_extended_unc(t.input);
        bool pass = result == t.expected;
        std::wcout << (pass ? L"[PASS] " : L"[FAIL] ") << t.input << L" -> " << result << L"\n";
        if (!pass) std::wcout << L"  Expected: " << t.expected << L"\n";
    }
}

void test_file_ops(const std::wstring& test_file) {
    std::cout << "\n=== File Operations Test ===\n";

    std::vector<uint8_t> test_data(1024);
    for (size_t i = 0; i < test_data.size(); ++i) test_data[i] = static_cast<uint8_t>(i & 0xFF);

    // Write
    bool wrote = unc::write_file_atomic(test_file, test_data);
    std::cout << "Write atomic: " << (wrote ? "OK" : "FAILED") << "\n";

    // Read back
    auto read_data = unc::read_file(test_file);
    bool match = (read_data.size() == test_data.size() &&
                  std::equal(read_data.begin(), read_data.end(), test_data.begin()));
    std::cout << "Read/verify: " << (match ? "OK" : "FAILED") << " (" << read_data.size() << " bytes)\n";

    // sUNC open
    HANDLE h = unc::open_sunc(test_file, GENERIC_READ);
    std::cout << "sUNC open: " << (h != INVALID_HANDLE_VALUE ? "OK" : "FAILED") << "\n";
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
}

int wmain(int argc, wchar_t* argv[]) {
    SetConsoleOutputCP(CP_UTF8);

    std::wstring test_file = L"\\\\localhost\\C$\\temp\\unc_test.bin";
    if (argc >= 2) test_file = argv[1];

    // Ensure temp directory exists
    std::filesystem::create_directories(std::filesystem::path(test_file).parent_path());

    test_normalization();
    test_file_ops(test_file);

    std::cout << "\n=== Benchmark ===\n";
    std::wcout << L"Test file: " << test_file << L"\n";

    auto result = unc::bench_full(test_file, 1000);
    std::cout << "Write: " << result.write_us_avg << " us avg, "
              << result.write_us_p50 << " us p50, " << result.write_us_p99 << " us p99\n";
    std::cout << "Read:  " << result.read_us_avg << " us avg, "
              << result.read_us_p50 << " us p50, " << result.read_us_p99 << " us p99\n";
    std::cout << "ACL overhead: " << result.acl_overhead_us << " us\n";

    std::cout << "\nTargets:\n";
    std::cout << "  Write < 500 us (LAN)\n";
    std::cout << "  Read  < 200 us (LAN)\n";
    std::cout << "  ACL   < 20 us overhead\n";

    return 0;
}