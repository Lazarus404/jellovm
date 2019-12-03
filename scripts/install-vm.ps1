# Install a Jello VM component (release zip, staged prefix, or build directory).
param(
    [string]$Zip,
    [string]$Dir,
    [string]$BuildDir,
    [string]$Version,
    [string]$Prefix,
    [switch]$Uninstall,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$Root = Split-Path -Parent $ScriptDir

function Write-Info([string]$Message) { Write-Host $Message }

function Invoke-VmAction([scriptblock]$Action, [string]$Description) {
    if ($DryRun) {
        Write-Info "[dry-run] $Description"
        return
    }
    & $Action
}

function Test-VmRoot([string]$VmRoot) {
    foreach ($sub in @("bin", "lib", "include")) {
        if (-not (Test-Path (Join-Path $VmRoot $sub))) {
            throw "error: '$VmRoot' is not a Jello VM tree (expected bin/, lib/, include/)"
        }
    }
    if (-not (Test-Path (Join-Path $VmRoot "include\jello.h"))) {
        throw "error: jello.h not found under $VmRoot\include"
    }
    if (-not (Test-Path (Join-Path $VmRoot "include\jello\jdll.h"))) {
        throw "error: jello/jdll.h not found under $VmRoot\include"
    }
    $jellovm = Join-Path $VmRoot "bin\jellovm.exe"
    if (-not (Test-Path $jellovm)) { $jellovm = Join-Path $VmRoot "bin\jellovm" }
    if (-not (Test-Path $jellovm)) { throw "error: jellovm not found under $VmRoot\bin" }
}

function Get-VersionFromName([string]$Name) {
    if ($Name -match "^jellovm-([0-9]+\.[0-9]+\.[0-9]+(-[^-]+)?)-") {
        return $Matches[1]
    }
    return $null
}

function Get-VmVersion([string]$VmRoot) {
    if ($Version) { return $Version }
    $fromName = Get-VersionFromName (Split-Path -Leaf $VmRoot)
    if ($fromName) { return $fromName }
    $readme = Join-Path $VmRoot "README.txt"
    if (Test-Path $readme) {
        $line = Get-Content $readme -TotalCount 1
        if ($line -match "Jello VM ([0-9]+\.[0-9]+\.[0-9]+(-[^\s]+)?)") {
            return $Matches[1]
        }
    }
    throw "error: could not detect VM version; pass -Version"
}

function Get-VmHome {
    if ($Prefix) { return $Prefix }
    $local = [Environment]::GetFolderPath("LocalApplicationData")
    return Join-Path $local "Jello\vm"
}

function Resolve-VmRoot {
    if ($Zip -and $Dir) { throw "error: use only one of -Zip or -Dir" }
    if ($BuildDir -and ($Zip -or $Dir)) {
        throw "error: -BuildDir cannot be combined with -Zip or -Dir"
    }

    if ($BuildDir) {
        $tmp = Join-Path $env:TEMP ("jellovm-stage-" + [guid]::NewGuid().ToString())
        if ($DryRun) {
            Write-Info "[dry-run] stage-vm.ps1 -BuildDir $BuildDir -Prefix $tmp"
            return Join-Path $tmp "dry-run"
        }
        & (Join-Path $Root "scripts\stage-vm.ps1") -BuildDir $BuildDir -Prefix $tmp
        Test-VmRoot $tmp
        return $tmp
    }

    if ($Zip) {
        if (-not (Test-Path $Zip)) { throw "error: zip not found: $Zip" }
        if ($DryRun) {
            return Join-Path $env:TEMP "jellovm-dry-run"
        }
        $tmp = Join-Path $env:TEMP ("jellovm-install-" + [guid]::NewGuid().ToString())
        Expand-Archive -Path $Zip -DestinationPath $tmp -Force
        $extracted = Get-ChildItem -Path $tmp -Directory | Select-Object -First 1
        if (-not $extracted) { throw "error: empty zip: $Zip" }
        $root = $extracted.FullName
        Test-VmRoot $root
        return $root
    }

    if ($Dir) {
        $root = (Resolve-Path $Dir).Path
        Test-VmRoot $root
        return $root
    }

    if (Test-Path (Join-Path $ScriptDir "bin\jellovm.exe")) {
        Test-VmRoot $ScriptDir
        return $ScriptDir
    }

    throw @"
error: pass -Zip, -Dir, -BuildDir, or run from an unpacked VM tree

usage: install-vm.ps1 [-Zip PATH] [-Dir PATH] [-BuildDir DIR] [-Version VER] [-Prefix PATH] [-Uninstall] [-DryRun]
"@
}

function Set-ScopedEnv([string]$Name, [string]$Value, [string]$Scope) {
    if ($DryRun) {
        Write-Info "[dry-run] set $Scope env $Name=$Value"
        return
    }
    [Environment]::SetEnvironmentVariable($Name, $Value, $Scope)
}

function Prepend-ScopedEnvPath([string]$Name, [string]$Value, [string]$Scope) {
    $existing = [Environment]::GetEnvironmentVariable($Name, $Scope)
    if ($existing -and ($existing -split ';' | Where-Object { $_ -eq $Value })) {
        return
    }
    $newVal = if ($existing) { "$Value;$existing" } else { $Value }
    Set-ScopedEnv $Name $newVal $Scope
}

function Add-ScopedPath([string]$BinDir, [string]$Scope) {
    $existing = [Environment]::GetEnvironmentVariable("Path", $Scope)
    if ($existing -and ($existing -split ';' | Where-Object { $_ -eq $BinDir })) {
        return
    }
    $newPath = if ($existing) { "$BinDir;$existing" } else { $BinDir }
    Set-ScopedEnv "Path" $newPath $Scope
}

function Install-Vm([string]$VmRoot, [string]$Ver) {
    $vmHome = Get-VmHome
    $target = Join-Path $vmHome ("sdk\" + $Ver)
    $current = Join-Path $vmHome "current"
    $scope = "User"

    Write-Info "Installing Jello VM $Ver -> $target"
    if (Test-Path $target) { throw "error: $target already exists (use -Uninstall first)" }

    Invoke-VmAction { New-Item -ItemType Directory -Force -Path (Split-Path $target) | Out-Null } "mkdir $(Split-Path $target)"
    Invoke-VmAction { Copy-Item -Path (Join-Path $VmRoot "*") -Destination $target -Recurse -Force } "copy VM to $target"

    if (Test-Path $current) {
        Invoke-VmAction { Remove-Item $current -Force -Recurse -ErrorAction SilentlyContinue } "remove old current"
    }
    Invoke-VmAction {
        New-Item -ItemType Junction -Path $current -Target $target | Out-Null
    } "junction $current -> $target"

    $includeDir = Join-Path $current "include"
    $libDir = Join-Path $current "lib"
    Set-ScopedEnv "JELLO_VM_ROOT" $current $scope
    Set-ScopedEnv "JELLO_INCLUDE" $includeDir $scope
    Set-ScopedEnv "JELLO_LIB" $libDir $scope
    Prepend-ScopedEnvPath "INCLUDE" $includeDir $scope
    Prepend-ScopedEnvPath "LIB" $libDir $scope
    Add-ScopedPath (Join-Path $current "bin") $scope

    Write-Info ""
    Write-Info "Installed Jello VM $Ver."
    Write-Info "  JELLO_VM_ROOT=$current"
    Write-Info "Open a new terminal, then run:"
    Write-Info "  jellovm --help"
}

function Uninstall-Vm {
    $vmHome = Get-VmHome
    $ver = $Version
    $current = Join-Path $vmHome "current"

    if (-not $ver) {
        if (-not (Test-Path $current)) {
            throw "error: pass -Version or ensure $current exists"
        }
        $ver = Split-Path -Leaf (Get-Item $current).Target
    }

    $target = Join-Path $vmHome ("sdk\" + $ver)
    Write-Info "Removing $target"
    Invoke-VmAction { Remove-Item $target -Recurse -Force -ErrorAction SilentlyContinue } "remove $target"

    if ((Test-Path $current) -and ((Get-Item $current).Target -eq $target)) {
        Invoke-VmAction { Remove-Item $current -Force -ErrorAction SilentlyContinue } "remove current link"
        $remaining = Get-ChildItem (Join-Path $vmHome "sdk") -Directory -ErrorAction SilentlyContinue |
            Sort-Object Name | Select-Object -Last 1
        if ($remaining) {
            Invoke-VmAction {
                New-Item -ItemType Junction -Path $current -Target $remaining.FullName | Out-Null
            } "switch current -> $($remaining.Name)"
        }
    }

    Write-Info "Uninstalled Jello VM $ver."
}

if ($Uninstall) {
    Uninstall-Vm
    exit 0
}

$vmRoot = Resolve-VmRoot
if (-not $DryRun) { Test-VmRoot $vmRoot }
$detectedVersion = Get-VmVersion $vmRoot
Install-Vm $vmRoot $detectedVersion
