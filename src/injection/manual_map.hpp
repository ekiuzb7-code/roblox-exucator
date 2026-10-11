#pragma once
#include <windows.h>
#include <vector>
#include <string>
#include <optional>

namespace inject {

// PE parsing helpers
struct PEInfo {
    uintptr_t image_base = 0;
    SIZE_T image_size = 0;
    DWORD entry_point_rva = 0;
    std::vector<IMAGE_SECTION_HEADER> sections;
    std::vector<IMAGE_DATA_DIRECTORY> data_dirs;
    bool is_64bit = false;
};

inline std::optional<PEInfo> parse_pe(const std::vector<uint8_t>& dll_bytes) {
    if (dll_bytes.size() < sizeof(IMAGE_DOS_HEADER)) return std::nullopt;
    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(dll_bytes.data());
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return std::nullopt;
    if (dll_bytes.size() < dos->e_lfanew + sizeof(IMAGE_NT_HEADERS64)) return std::nullopt;

    auto* nt64 = reinterpret_cast<const IMAGE_NT_HEADERS64*>(dll_bytes.data() + dos->e_lfanew);
    if (nt64->Signature != IMAGE_NT_SIGNATURE) return std::nullopt;

    PEInfo info;
    info.is_64bit = (nt64->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC);
    info.image_base = nt64->OptionalHeader.ImageBase;
    info.image_size = nt64->OptionalHeader.SizeOfImage;
    info.entry_point_rva = nt64->OptionalHeader.AddressOfEntryPoint;

    // Sections
    auto* sec = IMAGE_FIRST_SECTION(nt64);
    for (WORD i = 0; i < nt64->FileHeader.NumberOfSections; ++i, ++sec) {
        info.sections.push_back(*sec);
    }

    // Data directories
    for (DWORD i = 0; i < nt64->OptionalHeader.NumberOfRvaAndSizes; ++i) {
        info.data_dirs.push_back(nt64->OptionalHeader.DataDirectory[i]);
    }

    return info;
}

// Manual map a DLL into target PID — no LoadLibrary, no disk writes
// Returns base address or 0 on failure
inline uintptr_t manual_map(DWORD pid, const std::vector<uint8_t>& dll_bytes) {
    auto pe = parse_pe(dll_bytes);
    if (!pe) return 0;

    HANDLE proc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!proc) return 0;

    // Allocate RWX in target at preferred base if possible
    void* preferred = reinterpret_cast<void*>(pe->image_base);
    void* base = VirtualAllocEx(proc, preferred, pe->image_size,
                                MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    bool at_preferred = (base == preferred);
    if (!base) {
        // Try anywhere
        base = VirtualAllocEx(proc, nullptr, pe->image_size,
                              MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!base) { CloseHandle(proc); return 0; }
    }

    // Write headers
    SIZE_T written;
    WriteProcessMemory(proc, base, dll_bytes.data(), pe->data_dirs[0].Size, &written); // SizeOfHeaders

    // Write sections
    for (const auto& sec : pe->sections) {
        if (sec.SizeOfRawData && sec.PointerToRawData) {
            WriteProcessMemory(proc,
                reinterpret_cast<BYTE*>(base) + sec.VirtualAddress,
                dll_bytes.data() + sec.PointerToRawData,
                sec.SizeOfRawData, &written);
        }
    }

    // Apply relocations if not at preferred base
    if (!at_preferred && pe->data_dirs[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size) {
        uintptr_t delta = reinterpret_cast<uintptr_t>(base) - pe->image_base;
        auto* reloc_dir = reinterpret_cast<const IMAGE_BASE_RELOCATION*>(
            dll_bytes.data() + pe->data_dirs[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress);
        SIZE_T reloc_size = pe->data_dirs[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size;

        while (reloc_size >= sizeof(IMAGE_BASE_RELOCATION)) {
            DWORD count = (reloc_dir->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);
            auto* entries = reinterpret_cast<const WORD*>(reloc_dir + 1);
            for (DWORD i = 0; i < count; ++i) {
                WORD entry = entries[i];
                WORD type = entry >> 12;
                WORD offset = entry & 0xFFF;
                if (type == IMAGE_REL_BASED_DIR64) {
                    uintptr_t* target = reinterpret_cast<uintptr_t*>(
                        reinterpret_cast<BYTE*>(base) + reloc_dir->VirtualAddress + offset);
                    uintptr_t val;
                    ReadProcessMemory(proc, target, &val, sizeof(val), &written);
                    val += delta;
                    WriteProcessMemory(proc, target, &val, sizeof(val), &written);
                } else if (type == IMAGE_REL_BASED_HIGHLOW) {
                    DWORD* target = reinterpret_cast<DWORD*>(
                        reinterpret_cast<BYTE*>(base) + reloc_dir->VirtualAddress + offset);
                    DWORD val;
                    ReadProcessMemory(proc, target, &val, sizeof(val), &written);
                    val += static_cast<DWORD>(delta);
                    WriteProcessMemory(proc, target, &val, sizeof(val), &written);
                }
            }
            reloc_size -= reloc_dir->SizeOfBlock;
            reloc_dir = reinterpret_cast<const IMAGE_BASE_RELOCATION*>(
                reinterpret_cast<const BYTE*>(reloc_dir) + reloc_dir->SizeOfBlock);
        }
    }

    // Resolve imports
    if (pe->data_dirs[IMAGE_DIRECTORY_ENTRY_IMPORT].Size) {
        auto* import_desc = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(
            dll_bytes.data() + pe->data_dirs[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);

        for (; import_desc->Name; ++import_desc) {
            const char* dll_name = reinterpret_cast<const char*>(
                dll_bytes.data() + import_desc->Name);
            HMODULE h_mod = LoadLibraryA(dll_name);
            if (!h_mod) continue; // Optional: fail here

            auto* thunk = reinterpret_cast<const IMAGE_THUNK_DATA64*>(
                dll_bytes.data() + import_desc->FirstThunk);
            auto* orig_thunk = import_desc->OriginalFirstThunk
                ? reinterpret_cast<const IMAGE_THUNK_DATA64*>(
                    dll_bytes.data() + import_desc->OriginalFirstThunk)
                : thunk;

            for (; orig_thunk->u1.AddressOfData; ++orig_thunk, ++thunk) {
                FARPROC proc_addr = nullptr;
                if (orig_thunk->u1.Ordinal & IMAGE_ORDINAL_FLAG64) {
                    proc_addr = GetProcAddress(h_mod,
                        reinterpret_cast<LPCSTR>(orig_thunk->u1.Ordinal & 0xFFFF));
                } else {
                    auto* import_name = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(
                        dll_bytes.data() + orig_thunk->u1.AddressOfData);
                    proc_addr = GetProcAddress(h_mod, import_name->Name);
                }
                if (proc_addr) {
                    uintptr_t target = reinterpret_cast<uintptr_t>(base) + thunk->u1.Function;
                    WriteProcessMemory(proc, reinterpret_cast<void*>(target),
                                       &proc_addr, sizeof(proc_addr), &written);
                }
            }
        }
    }

    // TLS callbacks
    if (pe->data_dirs[IMAGE_DIRECTORY_ENTRY_TLS].Size) {
        auto* tls = reinterpret_cast<const IMAGE_TLS_DIRECTORY64*>(
            dll_bytes.data() + pe->data_dirs[IMAGE_DIRECTORY_ENTRY_TLS].VirtualAddress);
        if (tls->AddressOfCallBacks) {
            auto* callbacks = reinterpret_cast<const PIMAGE_TLS_CALLBACK*>(
                dll_bytes.data() + tls->AddressOfCallBacks - pe->image_base);
            for (; *callbacks; ++callbacks) {
                uintptr_t cb_addr = reinterpret_cast<uintptr_t>(base) +
                    (reinterpret_cast<uintptr_t>(*callbacks) - pe->image_base);
                HANDLE thread = CreateRemoteThread(proc, nullptr, 0,
                    reinterpret_cast<LPTHREAD_START_ROUTINE>(cb_addr),
                    base, 0, nullptr);
                if (thread) {
                    WaitForSingleObject(thread, 5000);
                    CloseHandle(thread);
                }
            }
        }
    }

    // Set final memory protections
    for (const auto& sec : pe->sections) {
        DWORD protect = PAGE_READONLY;
        if (sec.Characteristics & IMAGE_SCN_MEM_WRITE) protect = PAGE_READWRITE;
        if (sec.Characteristics & IMAGE_SCN_MEM_EXECUTE) protect = PAGE_EXECUTE_READ;
        if ((sec.Characteristics & IMAGE_SCN_MEM_WRITE) &&
            (sec.Characteristics & IMAGE_SCN_MEM_EXECUTE)) protect = PAGE_EXECUTE_READWRITE;

        DWORD old;
        VirtualProtectEx(proc,
            reinterpret_cast<BYTE*>(base) + sec.VirtualAddress,
            sec.Misc.VirtualSize, protect, &old);
    }

    // Call entry point
    uintptr_t entry = reinterpret_cast<uintptr_t>(base) + pe->entry_point_rva;
    HANDLE thread = CreateRemoteThread(proc, nullptr, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(entry), base, 0, nullptr);
    if (thread) {
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
    }

    CloseHandle(proc);
    return reinterpret_cast<uintptr_t>(base);
}

// Find process by name
inline DWORD find_process(const std::wstring& name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    DWORD pid = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, name.c_str()) == 0) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

// Inject from file path
inline uintptr_t inject_from_file(DWORD pid, const std::wstring& dll_path) {
    auto data = unc::read_file(dll_path);
    if (data.empty()) return 0;
    return manual_map(pid, data);
}

} // namespace inject