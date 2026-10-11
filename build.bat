@echo off
REM Roblox Executor - Local Build Script
REM Usage: build.bat [Debug|Release|RelWithDebInfo] [clean]

setlocal enabledelayedexpansion

set BUILD_TYPE=%1
if "%BUILD_TYPE%"=="" set BUILD_TYPE=Release

set CLEAN=%2
set PROJECT_DIR=%CD%
set VCPKG_DIR=%PROJECT_DIR%\vcpkg
set THIRD_PARTY_DIR=%PROJECT_DIR%\third_party

echo ==========================================
echo Roblox Executor Build
echo Build Type: %BUILD_TYPE%
echo ==========================================

REM Check for vcpkg
if not exist "%VCPKG_DIR%\vcpkg.exe" (
    echo Bootstrapping vcpkg...
    git clone https://github.com/microsoft/vcpkg "%VCPKG_DIR%"
    if errorlevel 1 (
        echo Failed to clone vcpkg
        exit /b 1
    )
    "%VCPKG_DIR%\bootstrap-vcpkg.bat" -disableMetrics
    if errorlevel 1 (
        echo Failed to bootstrap vcpkg
        exit /b 1
    )
)

REM Install dependencies
echo Installing dependencies via vcpkg...
"%VCPKG_DIR%\vcpkg" install fmt spdlog --triplet x64-windows-static
if errorlevel 1 (
    echo Warning: static triplet failed, trying dynamic...
    "%VCPKG_DIR%\vcpkg" install fmt spdlog --triplet x64-windows
    if errorlevel 1 (
        echo Warning: vcpkg install failed, continuing...
    )
)

REM Build Luau from source (not submodule)
echo Building Luau...
if not exist "%THIRD_PARTY_DIR%\luau\CMakeLists.txt" (
    echo Cloning Luau...
    mkdir "%THIRD_PARTY_DIR%" 2>nul
    cd /d "%THIRD_PARTY_DIR%"
    git clone --depth 1 --branch 0.6.3 https://github.com/luau-lang/luau.git
    cd /d "%PROJECT_DIR%"
    if errorlevel 1 (
        echo Failed to clone Luau
        exit /b 1
    )
)

if "%CLEAN%"=="clean" (
    echo Cleaning Luau build...
    rmdir /s /q "%THIRD_PARTY_DIR%\luau\build" 2>nul
)

cmake -B "%THIRD_PARTY_DIR%\luau\build" -S "%THIRD_PARTY_DIR%\luau" ^
  -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
  -DCMAKE_CXX_FLAGS="/MT" ^
  -DBUILD_TESTS=OFF
if errorlevel 1 (
    echo Failed to configure Luau
    exit /b 1
)

cmake --build "%THIRD_PARTY_DIR%\luau\build" --config %BUILD_TYPE% --parallel
if errorlevel 1 (
    echo Failed to build Luau
    exit /b 1
)

REM Configure main project
echo Configuring main project...
set LUAU_INCLUDE_DIR=%THIRD_PARTY_DIR%\luau\include
set LUAU_LIBRARY=%THIRD_PARTY_DIR%\luau\build\%BUILD_TYPE%\luau.lib

if "%CLEAN%"=="clean" (
    echo Cleaning main build...
    rmdir /s /q "%PROJECT_DIR%\build" 2>nul
)

cmake -B "%PROJECT_DIR%\build" -S "%PROJECT_DIR%" ^
  -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
  -DCMAKE_TOOLCHAIN_FILE="%VCPKG_DIR%\scripts\buildsystems\vcpkg.cmake" ^
  -DVCPKG_TARGET_TRIPLET=x64-windows-static ^
  -DLUAU_INCLUDE_DIR="%LUAU_INCLUDE_DIR%" ^
  -DLUAU_LIBRARY="%LUAU_LIBRARY%"
if errorlevel 1 (
    echo Failed to configure main project
    exit /b 1
)

REM Build
echo Building main project...
cmake --build "%PROJECT_DIR%\build" --config %BUILD_TYPE% --parallel
if errorlevel 1 (
    echo Build failed
    exit /b 1
)

echo ==========================================
echo Build Successful!
echo Executable: %PROJECT_DIR%\build\%BUILD_TYPE%\executor.exe
echo Injector:   %PROJECT_DIR%\build\%BUILD_TYPE%\injector.dll
echo Benchmark:  %PROJECT_DIR%\build\%BUILD_TYPE%\benchmark.exe
echo GUI:        %PROJECT_DIR%\build\%BUILD_TYPE%\roblox_executor_gui.exe
echo ==========================================

REM Run self-test
echo Running self-tests...
"%PROJECT_DIR%\build\%BUILD_TYPE%\executor.exe" --self-test
if errorlevel 1 (
    echo Self-tests failed
    exit /b 1
)

echo All done!