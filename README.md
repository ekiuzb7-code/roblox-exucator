# Roblox Executor

A high-performance Roblox Luau executor with UNC/sUNC path support, manual DLL mapping, and IPC communication. Built for benchmarking and research.

## Features

- **UNC/sUNC Path Handling**: Extended-length paths (`\\?\UNC\...`), ACL validation, atomic writes
- **Manual DLL Mapping**: No `LoadLibrary`, no disk writes, full relocation/import resolution
- **Luau VM Integration**: Sandboxed execution, bytecode compilation, benchmarking
- **IPC Server/Client**: Named pipes, JSON-RPC, async communication
- **GitHub Actions CI**: Multi-config builds, benchmarks, static analysis

## Architecture

```
executor.exe          # Main CLI entry point
injector.dll          # Injected DLL (manual map target)
benchmark.exe         # Performance benchmarks
```

## Building

### Prerequisites

- Windows 10/11 SDK (10.0.19041+)
- Visual Studio 2022 (MSVC 14.30+)
- CMake 3.25+
- vcpkg (auto-bootstrapped by CI)

### Local Build

```cmd
# Clone with submodules
git clone --recursive https://github.com/yourusername/roblox-executor.git
cd roblox-executor

# Build Luau submodule (if not using vcpkg)
git submodule update --init --recursive third_party/luau
cmake -B third_party/luau/build -S third_party/luau -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="/MT"
cmake --build third_party/luau/build --config Release

# Configure and build
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release ^
  -DLUAU_INCLUDE_DIR="%CD%\third_party\luau\include" ^
  -DLUAU_LIBRARY="%CD%\third_party\luau\build\Release\luau.lib"
cmake --build build --config Release --parallel
```

### Using vcpkg (Recommended)

```cmd
# Install vcpkg
git clone https://github.com/microsoft/vcpkg
.\vcpkg\bootstrap-vcpkg.bat

# Install dependencies
.\vcpkg\vcpkg install fmt spdlog luau --triplet x64-windows-static

# Build
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_TOOLCHAIN_FILE=.\vcpkg\scripts\buildsystems\vcpkg.cmake ^
  -DVCPKG_TARGET_TRIPLET=x64-windows-static
cmake --build build --config Release
```

## Usage

### CLI Commands

```cmd
# Inject DLL into process
executor.exe --inject <pid|process_name> <dll_path>

# Execute Luau script
executor.exe --execute script.lua

# Start IPC server
executor.exe --server

# Run benchmarks
executor.exe --bench [unc|luau|inject|ipc] [path]

# Self-test
executor.exe --self-test

# Version
executor.exe --version
```

### IPC API (Named Pipe: `\\.\pipe\roblox_executor`)

```json
// Execute script
{"id": 1, "method": "execute", "payload": "print('hello')"}

// Inject DLL
{"id": 2, "method": "inject", "payload": {"pid": 1234, "dll": "C:\\path\\injector.dll"}}

// Benchmark UNC
{"id": 3, "method": "bench", "payload": {"type": "unc", "path": "\\\\server\\share\\file"}}

// Version
{"id": 4, "method": "version", "payload": ""}
```

### UNC/sUNC API

```cpp
#include "unc/unc_path.hpp"

// Normalize to extended-length UNC
std::wstring ext = unc::to_extended_unc(L"\\\\server\\share\\file");
// Result: L"\\\\?\\UNC\\server\\share\\file"

// sUNC - validated open with ACL check
HANDLE h = unc::open_sunc(L"\\\\server\\share\\file", GENERIC_READ);

// Atomic write (temp file + move)
unc::write_file_atomic(L"\\\\server\\share\\file", data);

// Benchmark
auto result = unc::bench_full(L"\\\\server\\share\\file", 1000);
```

## Benchmarks

Target performance metrics:

| Metric | Target |
|--------|--------|
| UNC Write (64KB, LAN) | <500 μs |
| UNC Read (64KB, LAN) | <200 μs |
| Injection (manual map) | <15 ms |
| Luau Compile+Exec (1KB) | <50 μs |
| Cold Start | <200 ms |
| Memory Footprint | <15 MB |

Run benchmarks:
```cmd
benchmark.exe --unc --file "\\localhost\C$\temp\test.bin"
benchmark.exe --luau
benchmark.exe --inject
benchmark.exe --ipc
```

## UNC vs sUNC

| Feature | UNC | sUNC |
|---------|-----|------|
| Path Format | `\\server\share\path` | Same |
| Extended Prefix | Manual | Auto via `to_extended_unc` |
| MAX_PATH Limit | 260 chars | Bypassed (`\\?\`) |
| ACL Validation | None | `open_sunc` checks DACL |
| Atomic Write | Manual | `write_file_atomic` |
| Benchmark Overhead | Baseline | +10-20 μs (ACL check) |

## CI/CD

GitHub Actions workflow (`.github/workflows/build.yml`):
- Multi-config: Release, RelWithDebInfo
- Multi-OS: Windows 2022, Windows 2025
- Static analysis: cppcheck, MSVC `/analyze`
- Benchmarks on every build
- Artifact upload + release packaging

## License

MIT