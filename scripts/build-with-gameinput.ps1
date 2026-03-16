#Requires -Version 5.1
<#
.SYNOPSIS
    Downloads the latest Microsoft.GameInput NuGet package, extracts it, and builds MultiPadTester with the GameInput backend enabled.
.DESCRIPTION
    Fetches the latest version from NuGet, extracts the SDK to .gameinput-sdk in the repo root,
    then configures and builds with USE_GAMEINPUT=ON and GAMEINPUT_ROOT set to the SDK's build/native path.
.EXAMPLE
    .\scripts\build-with-gameinput.ps1
.EXAMPLE
    .\scripts\build-with-gameinput.ps1 -BuildDir build-gi -Config Release
#>
[CmdletBinding()]
param(
    [Parameter()]
    [string]$BuildDir = "build",
    [Parameter()]
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Config = "Release",
    [Parameter()]
    [string]$SdkDir = ".gameinput-sdk"
)

$ErrorActionPreference = "Stop"
$NuGetIndexUrl = "https://api.nuget.org/v3-flatcontainer/microsoft.gameinput/index.json"
$NuGetBaseUrl = "https://api.nuget.org/v3-flatcontainer/microsoft.gameinput"

# Resolve repo root (directory that contains CMakeLists.txt)
$scriptDir = Split-Path -Parent $PSCommandPath
$repoRoot = Split-Path -Parent $scriptDir
if (-not (Test-Path (Join-Path $repoRoot "CMakeLists.txt"))) {
    throw "Repo root not found (no CMakeLists.txt in $repoRoot)"
}

$sdkRoot = Join-Path $repoRoot $SdkDir
$vcpkgToolchain = Join-Path $repoRoot "vcpkg\scripts\buildsystems\vcpkg.cmake"
if (-not (Test-Path $vcpkgToolchain)) {
    throw "vcpkg toolchain not found at $vcpkgToolchain. Run from a repo that has vcpkg."
}

Write-Host "Fetching latest Microsoft.GameInput version from NuGet..." -ForegroundColor Cyan
$indexJson = Invoke-RestMethod -Uri $NuGetIndexUrl -UseBasicParsing
$versions = $indexJson.versions
$version = $versions[-1]
Write-Host "Latest version: $version" -ForegroundColor Green

$nupkgUrl = "$NuGetBaseUrl/$version/microsoft.gameinput.$version.nupkg"
$nupkgPath = Join-Path $repoRoot "microsoft.gameinput.$version.nupkg"
$extractDir = Join-Path $sdkRoot $version

if (Test-Path (Join-Path $extractDir "build\native")) {
    Write-Host "GameInput SDK $version already extracted at $extractDir" -ForegroundColor Green
} else {
    Write-Host "Downloading $nupkgUrl ..." -ForegroundColor Cyan
    New-Item -ItemType Directory -Path $sdkRoot -Force | Out-Null
    Invoke-WebRequest -Uri $nupkgUrl -OutFile $nupkgPath -UseBasicParsing

    try {
        Write-Host "Extracting NuGet package..." -ForegroundColor Cyan
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        [System.IO.Compression.ZipFile]::ExtractToDirectory($nupkgPath, $extractDir)
    } finally {
        if (Test-Path $nupkgPath) { Remove-Item $nupkgPath -Force }
    }

    $nativePath = Join-Path $extractDir "build\native"
    if (-not (Test-Path $nativePath)) {
        # Some packages use package/id.version/ layout
        $packageDir = Get-ChildItem -Path $extractDir -Directory | Select-Object -First 1
        if ($packageDir) {
            $altNative = Join-Path $packageDir.FullName "build\native"
            if (Test-Path $altNative) {
                $extractDir = $packageDir.FullName
            }
        }
    }
}

$nativePath = Join-Path $extractDir "build\native"
if (-not (Test-Path $nativePath)) {
    throw "GameInput SDK build/native not found under $extractDir"
}

$gameInputRoot = (Resolve-Path $nativePath).Path
Write-Host "GAMEINPUT_ROOT = $gameInputRoot" -ForegroundColor Gray

$buildPath = Join-Path $repoRoot $BuildDir
Write-Host "Configuring CMake (GameInput enabled)..." -ForegroundColor Cyan
& cmake -B $buildPath `
    -DCMAKE_TOOLCHAIN_FILE="$vcpkgToolchain" `
    -DUSE_GAMEINPUT=ON `
    -DGAMEINPUT_ROOT="$gameInputRoot" `
    -DCMAKE_BUILD_TYPE=$Config

if ($LASTEXITCODE -ne 0) { throw "CMake configure failed." }

Write-Host "Building..." -ForegroundColor Cyan
& cmake --build $buildPath --config $Config
if ($LASTEXITCODE -ne 0) { throw "Build failed." }

Write-Host "Done. Build output: $buildPath" -ForegroundColor Green
