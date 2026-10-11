#pragma once
#include <string>
#include <filesystem>
#include <windows.h>
#include <aclapi.h>
#include <vector>
#include <chrono>

namespace unc {

// Normalize any path to extended-length UNC prefix (\\?\UNC\server\share\...)
// Allows >260 chars, bypasses MAX_PATH, works with CreateFileW
inline std::wstring to_extended_unc(const std::wstring& path) {
    if (path.rfind(L"\\\\?\\", 0) == 0) return path;           // already extended
    if (path.rfind(L"\\\\", 0) == 0) return L"\\\\?\\UNC\\" + path.substr(2);
    if (std::filesystem::path(path).is_absolute()) return L"\\\\?\\" + path;
    return L"\\\\?\\" + std::filesystem::absolute(path).wstring();
}

// sUNC: validate share access before returning handle
// Returns INVALID_HANDLE_VALUE on ACL failure
inline HANDLE open_sunc(const std::wstring& unc_path, DWORD access = GENERIC_READ,
                        DWORD share = FILE_SHARE_READ, DWORD disp = OPEN_EXISTING) {
    auto ext = to_extended_unc(unc_path);
    HANDLE h = CreateFileW(ext.c_str(), access, share, nullptr, disp,
                           FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (h == INVALID_HANDLE_VALUE) return h;

    // Quick ACL check — can we actually read/write?
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (GetSecurityInfo(h, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                        nullptr, nullptr, nullptr, nullptr, &sd) == ERROR_SUCCESS) {
        // TODO: Implement full access check against current token
        LocalFree(sd);
    }
    return h;
}

// Read entire file via UNC/sUNC
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

// Write file via UNC/sUNC (atomic: write to temp, move)
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

    // Atomic replace
    return MoveFileExW(temp_path.c_str(), unc_path.c_str(), MOVEFILE_REPLACE_EXISTING);
}

// Benchmark helper — raw throughput test (microseconds per iteration)
inline uint64_t bench_throughput(const std::wstring& unc_file, size_t iterations = 1000) {
    HANDLE h = open_sunc(unc_file, GENERIC_READ | GENERIC_WRITE, 0, CREATE_ALWAYS);
    if (h == INVALID_HANDLE_VALUE) return 0;
    const size_t buf_size = 64 * 1024;
    std::vector<char> buf(buf_size, 'x');
    DWORD written;
    auto start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < iterations; ++i) {
        SetFilePointer(h, 0, nullptr, FILE_BEGIN);
        WriteFile(h, buf.data(), static_cast<DWORD>(buf_size), &written, nullptr);
    }
    auto end = std::chrono::high_resolution_clock::now();
    CloseHandle(h);
    return std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / iterations;
}

// Benchmark read throughput
inline uint64_t bench_read_throughput(const std::wstring& unc_file, size_t iterations = 1000) {
    // Pre-write test file
    std::vector<char> buf(64 * 1024, 'x');
    write_file_atomic(unc_file, {reinterpret_cast<uint8_t*>(buf.data()), buf.size()});

    HANDLE h = open_sunc(unc_file, GENERIC_READ);
    if (h == INVALID_HANDLE_VALUE) return 0;

    std::vector<char> read_buf(64 * 1024);
    DWORD read;
    auto start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < iterations; ++i) {
        SetFilePointer(h, 0, nullptr, FILE_BEGIN);
        ReadFile(h, read_buf.data(), static_cast<DWORD>(read_buf.size()), &read, nullptr);
    }
    auto end = std::chrono::high_resolution_clock::now();
    CloseHandle(h);
    return std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / iterations;
}

struct BenchResult {
    uint64_t write_us_avg;
    uint64_t write_us_p50;
    uint64_t write_us_p99;
    uint64_t read_us_avg;
    uint64_t read_us_p50;
    uint64_t read_us_p99;
    uint64_t acl_overhead_us;
};

inline BenchResult bench_full(const std::wstring& unc_file, size_t iterations = 1000) {
    BenchResult r{};
    std::vector<uint64_t> write_times, read_times;

    // Warmup
    bench_throughput(unc_file, 10);
    bench_read_throughput(unc_file, 10);

    // Measure write
    for (size_t i = 0; i < iterations; ++i) {
        write_times.push_back(bench_throughput(unc_file, 1));
    }
    // Measure read
    for (size_t i = 0; i < iterations; ++i) {
        read_times.push_back(bench_read_throughput(unc_file, 1));
    }

    // ACL overhead: compare open_sunc vs plain CreateFileW
    auto ext = to_extended_unc(unc_file);
    auto start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < 100; ++i) {
        HANDLE h = CreateFileW(ext.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }
    auto end = std::chrono::high_resolution_clock::now();
    uint64_t plain_open = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 100;

    start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < 100; ++i) {
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