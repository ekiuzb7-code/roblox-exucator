cmake_minimum_required(VERSION 3.25)
project(RobloxExecutor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build types
set(CMAKE_CONFIGURATION_TYPES "Debug;Release;RelWithDebInfo" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS_DEBUG "/MTd /Zi /Ob0 /Od /RTC1")
set(CMAKE_CXX_FLAGS_RELEASE "/MT /O2 /Ob2 /GL /DNDEBUG")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "/MT /O2 /Ob2 /GL /DNDEBUG /Zi")

# Link-time optimization
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)

# Windows version target
add_compile_definitions(_WIN32_WINNT=0x0A00 WINVER=0x0A00 NTDDI_VERSION=0x0A000006)

# Warning levels
if (MSVC)
    add_compile_options(/W4 /WX /permissive- /guard:cf /DYNAMICBASE /NXCOMPAT)
    add_link_options(/GUARD:CF /DYNAMICBASE /NXCOMPAT /LTCG)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

# Find dependencies via vcpkg
find_package(fmt REQUIRED)
find_package(spdlog REQUIRED)

# DirectX 11 for ImGui backend
find_package(DirectX REQUIRED)

# Luau - try to find via vcpkg, fallback to submodule
find_path(LUAU_INCLUDE_DIR luau.h
    PATHS ${CMAKE_SOURCE_DIR}/third_party/luau/include
          ${VCPKG_INSTALL_PREFIX}/include
)
find_library(LUAU_LIBRARY luau
    PATHS ${CMAKE_SOURCE_DIR}/third_party/luau/build
          ${VCPKG_INSTALL_PREFIX}/lib
)

if (NOT LUAU_INCLUDE_DIR OR NOT LUAU_LIBRARY)
    message(WARNING "Luau not found via vcpkg, expecting submodule at third_party/luau")
    # We'll handle this in the build script
endif()

# Option to build GUI
option(BUILD_GUI "Build GUI executable" ON)

# Main executor executable (CLI + IPC server)
add_executable(executor
    src/entry.cpp
    src/injection/manual_map.hpp
    src/luau/vm_wrapper.hpp
    src/luau/vm_wrapper.cpp
    src/unc/unc_path.hpp
    src/ipc/pipe_server.hpp
    src/ipc/pipe_server.cpp
)

target_include_directories(executor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${LUAU_INCLUDE_DIR}
)

target_link_libraries(executor PRIVATE
    fmt::fmt
    spdlog::spdlog
    ${LUAU_LIBRARY}
    kernel32.lib
    user32.lib
    advapi32.lib
    shell32.lib
    ole32.lib
    uuid.lib
)

# Injector DLL (for manual map)
add_library(injector SHARED
    src/injection/dll_main.cpp
)
target_include_directories(injector PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(injector PRIVATE fmt::fmt spdlog::spdlog)
set_target_properties(injector PROPERTIES
    PREFIX ""          # No "lib" prefix
    SUFFIX ".dll"      # Force .dll extension
    OUTPUT_NAME "injector"
)

# Benchmark executable
add_executable(benchmark
    bench/benchmark.cpp
)
target_include_directories(benchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(benchmark PRIVATE executor)

# GUI executable (tabbed editor with ImGui + DX11)
if (BUILD_GUI)
    add_executable(roblox_executor_gui
        src/gui/gui_main.cpp
        src/gui/editor_tabs.cpp
        src/gui/editor_tabs.hpp
        src/gui/imgui/imgui_impl.h
        src/ipc/pipe_client.hpp
        src/ipc/pipe_server.cpp  # For json helpers
    )

    target_include_directories(roblox_executor_gui PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/gui
        ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/imgui
        ${LUAU_INCLUDE_DIR}
    )

    target_link_libraries(roblox_executor_gui PRIVATE
        fmt::fmt
        spdlog::spdlog
        d3d11.lib
        dxgi.lib
        kernel32.lib
        user32.lib
        advapi32.lib
        shell32.lib
        ole32.lib
        uuid.lib
        comdlg32.lib
    )

    # GUI is a windowed app, not console
    if (MSVC)
        set_target_properties(roblox_executor_gui PROPERTIES
            LINK_FLAGS "/SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup"
        )
    endif()

    install(TARGETS roblox_executor_gui
        RUNTIME DESTINATION bin
    )
endif()

# Install targets
install(TARGETS executor
    RUNTIME DESTINATION bin
)
install(TARGETS injector
    LIBRARY DESTINATION bin
)

# CPack for packaging
set(CPACK_PACKAGE_NAME "RobloxExecutor")
set(CPACK_PACKAGE_VERSION "1.0.0")
set(CPACK_PACKAGE_DESCRIPTION "Roblox Luau Executor with UNC/sUNC support")
set(CPACK_GENERATOR "ZIP")
include(CPack)