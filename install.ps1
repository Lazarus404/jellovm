# Build jellovm, optionally package a VM zip, and install for the current user.
param(
    [switch]$SkipBuild,
    [switch]$PackageOnly,
    [string]$Version,
    [string]$BuildDir = "build",
    [string]$OutDir = "dist",
    [string]$Prefix,
    [switch]$DryRun,
    [switch]$Uninstall,
    [switch]$Help
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $Root

function Show-Usage {
    @"
usage: install.ps1 [options]

Build jellovm, optionally package a platform VM zip, and install locally.

Build / package:
  -SkipBuild         Skip cmake build (use existing build/)
  -PackageOnly       Build and create dist\*.zip only; do not install
  -Version VER       VM version label (default: CMake project version)
  -BuildDir DIR      CMake build directory (default: build)
  -OutDir DIR        Output directory for VM zip (default: dist)

Install (passed to scripts/install-vm.ps1):
  -Prefix PATH       Override install root
  -DryRun            Print install actions without changing the system
  -Uninstall         Remove an installed VM (pass -Version if needed)
  -Help              Show this help
"@
}

if ($Help) { Show-Usage; exit 0 }

$installArgs = @()
if ($Version) { $installArgs += @("-Version", $Version) }
if ($Prefix) { $installArgs += @("-Prefix", $Prefix) }
if ($DryRun) { $installArgs += "-DryRun" }
if ($Uninstall) { $installArgs += "-Uninstall" }

if ($Uninstall) {
    & (Join-Path $Root "scripts\install-vm.ps1") @installArgs
    exit $LASTEXITCODE
}

$OsName = "windows"
$Arch = switch -Regex ($env:PROCESSOR_ARCHITECTURE) {
    "AMD64" { "x64" }
    "ARM64" { "arm64" }
    default { throw "error: unsupported CPU architecture: $($env:PROCESSOR_ARCHITECTURE)" }
}

if (-not $SkipBuild) {
    Write-Host "Building jellovm (Release)..."
    if (-not $DryRun) {
        cmake -S . -B $BuildDir -DCMAKE_BUILD_TYPE=Release -DJELLOVM_BUILD_TESTS=OFF
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        cmake --build $BuildDir --config Release --target jellovm_cli
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    } else {
        Write-Host "[dry-run] cmake build jellovm_cli"
    }
} else {
    Write-Host "Skipping build (-SkipBuild)."
}

if (-not $Version) {
    $cache = Join-Path $BuildDir "CMakeCache.txt"
    if (Test-Path $cache) {
        $Version = (Select-String -Path $cache -Pattern '^CMAKE_PROJECT_VERSION:STATIC=(.+)$' |
            ForEach-Object { $_.Matches[0].Groups[1].Value } | Select-Object -First 1)
    }
}
if (-not $Version) { $Version = "0.1.0" }

$VmZip = Join-Path $OutDir "jellovm-$Version-$OsName-$Arch.zip"

Write-Host "Packaging VM $Version ($OsName-$Arch)..."
if ($DryRun) {
    Write-Host "[dry-run] scripts/package-vm.ps1 -Version $Version -Os $OsName -Arch $Arch -BuildDir $BuildDir -OutDir $OutDir"
} else {
    & (Join-Path $Root "scripts\package-vm.ps1") `
        -Version $Version -Os $OsName -Arch $Arch -BuildDir $BuildDir -OutDir $OutDir
}

if ($PackageOnly) {
    Write-Host "Created $VmZip"
    exit 0
}

Write-Host "Installing from build tree..."
if ($DryRun) {
    Write-Host "[dry-run] scripts/install-vm.ps1 -BuildDir $BuildDir"
    exit 0
}

& (Join-Path $Root "scripts\install-vm.ps1") -BuildDir $BuildDir @installArgs
