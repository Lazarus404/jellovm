# Package a platform VM zip from a CMake build tree (via cmake --install staging).
param(
    [Parameter(Mandatory = $true)][string]$Version,
    [Parameter(Mandatory = $true)][string]$Os,
    [Parameter(Mandatory = $true)][string]$Arch,
    [Parameter(Mandatory = $true)][string]$BuildDir,
    [Parameter(Mandatory = $true)][string]$OutDir
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$BuildDir = (Resolve-Path (Join-Path $Root $BuildDir)).Path
$OutDir = Join-Path $Root $OutDir
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path

$PkgName = "jellovm-$Version-$Os-$Arch"
$Stage = Join-Path $env:TEMP ("jellovm-pkg-" + [guid]::NewGuid().ToString())
$PkgRoot = Join-Path $Stage $PkgName

try {
    & (Join-Path $Root "scripts\stage-vm.ps1") -BuildDir $BuildDir -Prefix $PkgRoot

    Copy-Item (Join-Path $Root "scripts\install-vm.sh") (Join-Path $PkgRoot "install.sh")
    Copy-Item (Join-Path $Root "scripts\install-vm.ps1") (Join-Path $PkgRoot "install.ps1")

    @"
Jello VM $Version ($Os-$Arch)
=====================================

bin/jellovm.exe   - Jello virtual machine
lib/              - libjellovm (static or import library)
include/          - jello.h (embed API), jello/jdll.h (plugin API)

Install (recommended):

  Windows:        .\install.ps1

This installs to %LOCALAPPDATA%\Jello\vm\, adds jellovm to PATH, and sets JELLO_INCLUDE and JELLO_LIB.
"@ | Set-Content -Path (Join-Path $PkgRoot "README.txt") -Encoding UTF8

    $ZipPath = Join-Path $OutDir "$PkgName.zip"
    if (Test-Path $ZipPath) { Remove-Item $ZipPath -Force }
    Compress-Archive -Path $PkgRoot -DestinationPath $ZipPath -Force
    Write-Host "Created $ZipPath"
}
finally {
    if (Test-Path $Stage) { Remove-Item $Stage -Recurse -Force -ErrorAction SilentlyContinue }
}
