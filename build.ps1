<#>
.SYNOPSIS
    Build script for Roblox Executor

.DESCRIPTION
    Builds the executor, injector DLL, and benchmarks using CMake and vcpkg.
    Uses pre-installed vcpkg at C:\vcpkg (GitHub runners) or falls back to local install.
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
$VcpkgRoot = "C:\vcpkg"
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
Write-Info "Using vcpkg: $VcpkgRoot"

# Check for pre-installed vcpkg
if (-not (Test-Path "$VcpkgRoot\vcpkg.exe")) {
    Write-Warning "vcpkg not found at $VcpkgRoot, installing locally..."
    $VcpkgDir = Join-Path $ProjectDir 'vcpkg'
    if (-not (Test-Path "$VcpkgDir\vcpkg.exe")) {
        Write-Header "Bootstrapping vcpkg"
        git clone https://github.com/microsoft/vcpkg $VcpkgDir
        & "$VcpkgDir\bootstrap-vcpkg.bat" -disableMetrics
        if ($LASTEXITCODE -ne 0) { Write-Error "vcpkg bootstrap failed"; exit $LASTEXITCODE }
    }
    $VcpkgRoot = $VcpkgDir
    Write-Success "vcpkg ready at $VcpkgRoot"
} else {
    Write-Success "Using pre-installed vcpkg at $VcpkgRoot"
}

# Install dependencies (dynamic triplet - pre-built binaries)
Write-Header "Installing dependencies"
& "$VcpkgRoot\vcpkg.exe" install fmt spdlog --triplet x64-windows
if ($LASTEXITCODE -ne 0) { Write-Error "Failed to install dependencies"; exit $LASTEXITCODE }
Write-Success "Dependencies installed"

# Build Luau from source
Write-Header "Building Luau"
if (-not (Test-Path "$LuauDir\CMakeLists.txt")) {
    Write-Info "Cloning Luau..."
    New-Item -ItemType Directory -Path $ThirdPartyDir -Force | Out-Null
    Set-Location $ThirdPartyDir
    git clone --depth 1 --branch 0.6.3 https://github.com/luau-lang/luau.git
    Set-Location $ProjectDir
    if ($LASTEXITCODE -ne 0) { Write-Error "Failed to clone Luau"; exit $LASTEXITCODE }
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
    "-DCMAKE_TOOLCHAIN_FILE=$VcpkgRoot\scripts\buildsystems\vcpkg.cmake"
    "-DVCPKG_TARGET_TRIPLET=x64-windows"
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