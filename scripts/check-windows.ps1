param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",

    [string]$BuildDir = "build/windows",

    [string]$Triplet = $(if ($env:VCPKG_TARGET_TRIPLET) { $env:VCPKG_TARGET_TRIPLET } else { "x64-windows" }),

    [string]$VcpkgRoot = $env:VCPKG_ROOT,

    [switch]$Clean,
    [switch]$Run
)

$ErrorActionPreference = "Stop"

function Invoke-Native {
    param(
        [Parameter(Mandatory = $true)]
        [string]$FilePath,

        [Parameter(ValueFromRemainingArguments = $true)]
        [string[]]$Arguments
    )

    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$FilePath failed with exit code $LASTEXITCODE"
    }
}

if (-not $VcpkgRoot) {
    $candidate = Join-Path $PSScriptRoot "..\vcpkg"
    if (Test-Path $candidate) {
        $VcpkgRoot = (Resolve-Path $candidate).Path
    }
}

if (-not $VcpkgRoot) {
    throw "VCPKG_ROOT is not set. Set it to your vcpkg checkout, or place vcpkg next to this repository."
}

$ToolchainFile = Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"
if (-not (Test-Path $ToolchainFile)) {
    throw "vcpkg toolchain file not found: $ToolchainFile"
}

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$BuildPath = Join-Path $RepoRoot $BuildDir

if ($Clean -and (Test-Path $BuildPath)) {
    Remove-Item -Recurse -Force $BuildPath
}

Invoke-Native cmake -S $RepoRoot -B $BuildPath `
    -G "Visual Studio 17 2022" `
    -A x64 `
    -DCMAKE_TOOLCHAIN_FILE="$ToolchainFile" `
    -DVCPKG_TARGET_TRIPLET="$Triplet" `
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

Invoke-Native cmake --build $BuildPath --config $Configuration --parallel

if ($Run) {
    $ExePath = Join-Path $BuildPath "$Configuration\game01P.exe"
    if (-not (Test-Path $ExePath)) {
        throw "Executable not found after build: $ExePath"
    }

    & $ExePath
}
