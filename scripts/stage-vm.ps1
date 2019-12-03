# Stage a Jello VM install tree via cmake --install (bin/, lib/, include/).
# Usage:
#   scripts/stage-vm.ps1 -BuildDir build -Prefix C:\Temp\jellovm-stage
param(
    [Parameter(Mandatory = $true)][string]$BuildDir,
    [Parameter(Mandatory = $true)][string]$Prefix
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)

function Resolve-BuildDir([string]$Dir) {
    if ([IO.Path]::IsPathRooted($Dir)) {
        if (Test-Path (Join-Path $Dir "CMakeCache.txt")) { return (Resolve-Path $Dir).Path }
        throw "error: CMake build not found at $Dir (missing CMakeCache.txt)"
    }
    $underRoot = Join-Path $Root $Dir
    if (Test-Path (Join-Path $underRoot "CMakeCache.txt")) { return (Resolve-Path $underRoot).Path }
    if (Test-Path (Join-Path $Dir "CMakeCache.txt")) { return (Resolve-Path $Dir).Path }
    throw "error: CMake build not found at $Dir (missing CMakeCache.txt)"
}

$BuildDir = Resolve-BuildDir $BuildDir
New-Item -ItemType Directory -Force -Path $Prefix | Out-Null
$Prefix = (Resolve-Path $Prefix).Path

cmake --install $BuildDir --prefix $Prefix

$jelloH = Join-Path $Prefix "include\jello.h"
$jdllH = Join-Path $Prefix "include\jello\jdll.h"
$jellovmExe = Join-Path $Prefix "bin\jellovm.exe"
if (-not (Test-Path $jellovmExe)) { $jellovmExe = Join-Path $Prefix "bin\jellovm" }

if (-not (Test-Path $jelloH)) { throw "error: jello.h not found under $Prefix\include after cmake --install" }
if (-not (Test-Path $jdllH)) { throw "error: jello/jdll.h not found under $Prefix\include after cmake --install" }
if (-not (Test-Path $jellovmExe)) { throw "error: jellovm not found under $Prefix\bin after cmake --install" }

$hasLib = $false
foreach ($name in @("libjellovm.a", "libjellovm.lib", "libjellovm.dll.a")) {
    if (Test-Path (Join-Path $Prefix "lib\$name")) { $hasLib = $true; break }
}
if (-not $hasLib) { throw "error: libjellovm not found under $Prefix\lib after cmake --install" }

Get-ChildItem (Join-Path $Prefix "lib") -Filter "*jellovm*.dll" -ErrorAction SilentlyContinue |
    ForEach-Object { Copy-Item $_.FullName (Join-Path $Prefix "bin\") -Force }

Write-Host "Staged VM install at $Prefix"
