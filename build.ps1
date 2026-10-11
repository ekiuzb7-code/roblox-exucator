<#>
.SYNOPSIS
    Build script for Roblox Executor

.DESCRIPTION
    Builds the executor, injector DLL, and benchmarks using CMake and vcpkg.
    Supports Debug, Release, and RelWithDebInfo configurations.

.PARAMETER BuildType
    Build configuration (Debug, Release, RelWithDebInfo). Default: Release

.PARAMETER Clean
    Clean build directories before building

.PARAMETER RunTests
    Run self-tests after build. Default: $true

.PARAMETER RunBenchmarks
    Run benchmarks after build. Default: $false

.EXAMPLE
    .\build.ps1 -BuildType Release

.EXAMPLE
    .\build.ps1 -BuildType RelWithDebInfo -Clean -RunBenchmarks
#>

param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$BuildType = 'Release',

    [switch]$Clean,

    [switch]$RunTests,

    [switch]$RunBenchmarks
)

$ErrorActionPreference = 'Stop'
$ProjectDir = $PSScriptRoot
$VcpkgDir = Join-Path $ProjectDir 'vcpkg'
$ThirdPartyDir = Join-Path $ProjectDir 'third_party'
$LuauDir = Join-Path $ThirdPartyDir 'luau'
$BuildDir = Join-Path $ProjectDir 'build'

function Write-Header($msg) {
    Write-Host "`n==========================================" -ForegroundColor Cyan
    Write-Host $msg -ForegroundColor Cyan
    Write-Host "==========================================" -ForegroundColor Cyan
}

function Write-Success($msg) { Write-Host "[OK] $msg" -ForegroundColor Green }
function Write-Error($msg) { Write-Host "[ERROR] $msg" -ForegroundColor Red }
function Write-Warning($msg) { Write-Host "[WARN] $msg" -ForegroundColor Yellow }
function Write-Info($msg) { Write-Host "[INFO] $msg" -ForegroundColor Gray }

Write-Header "Roblox Executor Build"
Write-Info "Build Type: $BuildType"
Write-Info "Project: $ProjectDir"

# Check/Install vcpkg
if (-not (Test-Path "$VcpkgDir\vcpkg.exe")) {
    Write-Header "Bootstrapping vcpkg"
    git clone https://github.com/microsoft/vcpkg $VcpkgDir
    & "$VcpkgDir\bootstrap-vcpkg.bat" -disableMetrics
    if ($LASTEXITCODE -ne 0) { Write-Error "vcpkg bootstrap failed"; exit $LASTEXITCODE }
    Write-Success "vcpkg ready"
}

# Install dependencies
Write-Header "Installing dependencies"
& "$VcpkgDir\vcpkg.exe" install fmt spdlog --triplet x64-windows-static
if ($LASTEXITCODE -ne 0) { Write-Warning "vcpkg install had issues, continuing..." }

# Build Luau submodule
Write-Header "Building Luau submodule"
if (-not (Test-Path "$LuauDir\CMakeLists.txt")) {
    Write-Info "Initializing Luau submodule..."
    git submodule update --init --recursive $LuauDir
    if ($LASTEXITCODE -ne 0) { Write-Error "Failed to init Luau submodule"; exit $LASTEXITCODE }
}

$LuauBuildDir = Join-Path $LuauDir 'build'
if ($Clean -and (Test-Path $LuauBuildDir)) {
    Write-Info "Cleaning Luau build..."
    Remove-Item $LuauBuildDir -Recurse -Force
}

$LuauCMakeArgs = @(
    "-B", $LuauBuildDir
    "-S", $LuauDir
    "-DCMAKE_BUILD_TYPE=$BuildType"
    "-DCMAKE_CXX_FLAGS=/MT"
    "-DBUILD_TESTS=OFF"
)
& cmake @LuauCMakeArgs
if ($LASTEXITCODE -ne 0) { Write-Error "Luau configure failed"; exit $LASTEXITCODE }

& cmake --build $LuauBuildDir --config $BuildType --parallel
if ($LASTEXITCODE -ne 0) { Write-Error "Luau build failed"; exit $LASTEXITCODE }
Write-Success "Luau built"

# Configure main project
Write-Header "Configuring main project"
$LuauIncludeDir = Join-Path $LuauDir 'include'
$LuauLibrary = Join-Path $LuauBuildDir $BuildType 'luau.lib'

if ($Clean -and (Test-Path $BuildDir)) {
    Write-Info "Cleaning main build..."
    Remove-Item $BuildDir -Recurse -Force
}

$CMakeArgs = @(
    "-B", $BuildDir
    "-S", $ProjectDir
    "-DCMAKE_BUILD_TYPE=$BuildType"
    "-DCMAKE_TOOLCHAIN_FILE=$VcpkgDir\scripts\buildsystems\vcpkg.cmake"
    "-DVCPKG_TARGET_TRIPLET=x64-windows-static"
    "-DLUAU_INCLUDE_DIR=$LuauIncludeDir"
    "-DLUAU_LIBRARY=$LuauLibrary"
)
& cmake @CMakeArgs
if ($LASTEXITCODE -ne 0) { Write-Error "Main configure failed"; exit $LASTEXITCODE }

# Build
Write-Header "Building main project"
& cmake --build $BuildDir --config $BuildType --parallel
if ($LASTEXITCODE -ne 0) { Write-Error "Main build failed"; exit $LASTEXITCODE }

$ExePath = Join-Path $BuildDir $BuildType 'executor.exe'
$DllPath = Join-Path $BuildDir $BuildType 'injector.dll'
$BenchPath = Join-Path $BuildDir $BuildType 'benchmark.exe'
$GuiPath = Join-Path $BuildDir $BuildType 'roblox_executor_gui.exe'

Write-Success "Build complete!"
Write-Info "Executor:  $ExePath"
Write-Info "Injector:  $DllPath"
Write-Info "Benchmark: $BenchPath"
Write-Info "GUI:       $GuiPath"

# Self-test
if ($RunTests -or -not $RunBenchmarks) {
    Write-Header "Running self-tests"
    & $ExePath --self-test
    if ($LASTEXITCODE -ne 0) { Write-Error "Self-tests failed"; exit $LASTEXITCODE }
    Write-Success "Self-tests passed"
}

# Benchmarks
if ($RunBenchmarks) {
    Write-Header "Running benchmarks"
    $TestFile = "\\localhost\C$\temp\executor_bench.bin"
    if (-not (Test-Path (Split-Path $TestFile))) {
        New-Item -ItemType Directory -Path (Split-Path $TestFile) -Force | Out-Null
    }

    & $BenchPath --unc --file $TestFile
    & $BenchPath --luau
    & $BenchPath --inject
    & $BenchPath --ipc
    Write-Success "Benchmarks complete"
}

Write-Header "All done!"