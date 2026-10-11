#include <windows.h>
#include <fmt/core.h>
#include <spdlog/spdlog.h>

// Minimal DLL entry for manual mapping
// This gets injected into the target process

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hModule);
            // Initialize logger (output to debug string)
            spdlog::set_pattern("[injector] %v");
            spdlog::set_level(spdlog::level::debug);
            spdlog::info("Injected into process {}", GetCurrentProcessId());
            break;
        case DLL_PROCESS_DETACH:
            spdlog::info("Detaching from process");
            break;
    }
    return TRUE;
}

// Exported functions for the executor to call via GetProcAddress
extern "C" __declspec(dllexport) void executor_init() {
    spdlog::info("executor_init called");
}

extern "C" __declspec(dllexport) void executor_shutdown() {
    spdlog::info("executor_shutdown called");
}

extern "C" __declspec(dllexport) const char* executor_version() {
    return "1.0.0";
}