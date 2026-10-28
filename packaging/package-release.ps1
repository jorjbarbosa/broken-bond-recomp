[CmdletBinding()]
param(
    [string]$Version,
    [string]$OutputRootDir,
    [ValidateRange(1, 128)][int]$Jobs = 6
)

# Builds the Windows release: a portable ZIP of the native game, with its own
# setup screen and menus. No launcher, installer or game data is included.
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $Version) {
    $versionSource = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'version.iss') -Raw
    if ($versionSource -notmatch '#define MyAppVersion "([^"]+)"') { throw 'Missing release version.' }
    $Version = $Matches[1]
}
# major.minor.patch, with an optional pre-release suffix ("0.1.0-beta"); the
# update comparison reads the numeric part only.
if ($Version -notmatch '^\d+\.\d+\.\d+(-[0-9A-Za-z.]+)?$') { throw 'Use a major.minor.patch version, with an optional -suffix.' }
if (-not $OutputRootDir) { $OutputRootDir = Join-Path $projectRoot 'dist' }
$OutputRootDir = [IO.Path]::GetFullPath($OutputRootDir)
$buildRoot = Join-Path $projectRoot 'build/native'
$cachePath = Join-Path $buildRoot 'CMakeCache.txt'
if (-not (Test-Path -LiteralPath $cachePath)) { throw 'Run scripts/build-game-native.ps1 first.' }
$cache = Get-Content -LiteralPath $cachePath -Raw
if ($cache -notmatch '(?m)^CMAKE_BUILD_TYPE:STRING=Release\s*$') { throw 'Packaging requires a Release build.' }
# The ZIP has no launcher: the game's setup screen is the only way to choose the game.
if ($cache -notmatch '(?m)^BROKENBOND_NATIVE_RENDERER:BOOL=(ON|TRUE|YES|1)\s*$') {
    throw 'The ZIP requires the native game with its setup screen. Configure scripts/build-game-native.ps1; existing releases were not changed.'
}
# The release ships both graphics APIs; Direct3D 12 stays the default.
foreach ($option in @('BROKENBOND_NATIVE_D3D12', 'BROKENBOND_NATIVE_VULKAN')) {
    if ($cache -notmatch "(?m)^${option}:BOOL=(ON|TRUE|YES|1)\s*$") {
        throw "Packaging requires $option=ON. Configure scripts/build-game-native.ps1 -Backend both."
    }
}
if ($env:VSCMD_ARG_TGT_ARCH -ne 'x64') {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $visualStudio = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $visualStudio) { throw 'Visual Studio C++ Build Tools are required.' }
    & (Join-Path $visualStudio 'Common7/Tools/Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
}
& cmake --build $buildRoot --target broken_bond --parallel $Jobs
if ($LASTEXITCODE -ne 0) { throw 'Port build failed; previous packages were not changed.' }
# The Vulkan backend reads the SPIR-V package embedded in the game executable.
$spirvPackage = Join-Path $buildRoot 'shader_cache/shader_cache_spirv.bin'
if (-not (Test-Path -LiteralPath $spirvPackage -PathType Leaf) -or (Get-Item -LiteralPath $spirvPackage).Length -eq 0) {
    throw "Missing SPIR-V shader package: $spirvPackage"
}

$binaries = @('broken_bond.exe', 'rexruntime.dll')
if (Test-Path -LiteralPath (Join-Path $buildRoot 'amd_fidelityfx_dx12.dll')) { $binaries += 'amd_fidelityfx_dx12.dll' }
$inspect = (Get-Command llvm-readobj.exe -ErrorAction Stop).Source
foreach ($name in $binaries) {
    $path = Join-Path $buildRoot $name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -eq 0) {
        throw "Missing or empty binary: $path"
    }
    $imports = & $inspect --coff-imports $path
    if ($LASTEXITCODE -ne 0) { throw "Cannot inspect binary: $path" }
    if ($imports -match 'Name:\s+(?:(?:MSVCP|VCRUNTIME|CONCRT)\w*D\.dll|ucrtbased\.dll)') {
        throw "Debug CRT dependency in $path. Rebuild the port and SDK in Release."
    }
    if (($imports -match 'Name:\s+amd_fidelityfx_dx12\.dll') -and
        ($binaries -notcontains 'amd_fidelityfx_dx12.dll')) { throw 'Missing FidelityFX runtime DLL.' }
}
$releaseParts = @{
    FidelityFX = $binaries -contains 'amd_fidelityfx_dx12.dll'
    NativeVulkan = $true
}
$components = @((Import-PowerShellDataFile (Join-Path $PSScriptRoot 'third-party-licenses.psd1')).Components |
    Where-Object { -not $_.Condition -or $releaseParts[$_.Condition] })
if (-not (Test-Path -LiteralPath (Join-Path $projectRoot 'LICENSE') -PathType Leaf)) { throw 'Missing project LICENSE.' }
foreach ($component in $components) {
    if (-not (Test-Path -LiteralPath (Join-Path $projectRoot $component.File) -PathType Leaf)) {
        throw "Missing license for $($component.Name): $($component.File)"
    }
}
# The LGPL libraries are statically linked into rexruntime.dll: name the exact
# source revisions it is built from, so it can be rebuilt with modified copies.
$sourceRevisions = foreach ($source in @(
        @{ Name = 'Naruto: The Broken Bond PC port (SDK patches: patches)'; Url = 'https://github.com/jorjbarbosa/broken-bond-recomp'; Path = '.' }
        @{ Name = 'ReXGlue SDK (rexruntime.dll)'; Url = 'https://github.com/rexglue/rexglue-sdk'; Path = 'sdk' }
        @{ Name = 'FFmpeg'; Url = 'https://github.com/wmarti/FFmpeg'; Path = 'sdk/thirdparty/FFmpeg' }
        @{ Name = 'libmspack'; Url = 'https://github.com/kyz/libmspack'; Path = 'sdk/thirdparty/libmspack' })) {
    $revision = & git -C (Join-Path $projectRoot $source.Path) rev-parse HEAD
    if ($LASTEXITCODE -ne 0 -or -not $revision) { throw "Cannot record source revision: $($source.Path)" }
    "  $($source.Name)`r`n    $($source.Url)/tree/$($revision.Trim())"
}

# Build into a fresh staging directory. A failed step leaves the previous
# release intact; only complete artifacts are published.
$stage = Join-Path $OutputRootDir ('.packaging-' + [guid]::NewGuid().ToString('N'))
$portableName = 'broken-bond-recomp-win-amd64'
$portable = Join-Path $stage $portableName
New-Item -ItemType Directory -Path $portable -Force | Out-Null
try {
    foreach ($name in $binaries) { Copy-Item -LiteralPath (Join-Path $buildRoot $name) -Destination $portable }
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'broken_bond.toml') -Destination $portable
    Set-Content -LiteralPath (Join-Path $portable 'broken_bond.version') -Value $Version -Encoding ascii
    $licenses = Join-Path $portable 'licenses'
    New-Item -ItemType Directory -Path $licenses | Out-Null
    Copy-Item -LiteralPath (Join-Path $projectRoot 'LICENSE') -Destination (Join-Path $licenses 'broken-bond-recomp.txt')
    foreach ($component in $components) {
        Copy-Item -LiteralPath (Join-Path $projectRoot $component.File) -Destination (Join-Path $licenses $component.Target)
    }
    $componentText = ($components | ForEach-Object {
        "$($_.Name)`r`n  License: $($_.License) ($($_.Target))`r`n  Source:  $($_.Url)`r`n  Used in: $($_.Usage)"
    }) -join "`r`n`r`n"
    $sourceText = $sourceRevisions -join "`r`n"
    Set-Content -LiteralPath (Join-Path $licenses 'README.txt') -Encoding utf8 -Value @"
THIRD-PARTY NOTICES - NARUTO: THE BROKEN BOND PC PORT ($Version)

This port's own code is licensed under GPL-3.0-only: broken-bond-recomp.txt.
It includes the components below, each under its own license; the license texts
are in this folder. Naruto: The Broken Bond, its content, artwork and trademarks
belong to their respective owners and are not covered by these licenses. Game
data is not distributed with this port.

$componentText

LGPL SOURCE CODE:
FFmpeg (LGPL-2.1-or-later) and libmspack (LGPL-2.1-only) are statically linked
into rexruntime.dll, which is built from the ReXGlue SDK (BSD-3-Clause). The
complete source code of these libraries and of rexruntime.dll, at the revisions
this release was built from:

$sourceText

You may modify these libraries, rebuild rexruntime.dll from these sources and
replace the rexruntime.dll of this package with it.
"@
    Set-Content -LiteralPath (Join-Path $portable 'README.txt') -Encoding utf8 -Value @"
NARUTO: THE BROKEN BOND - PC PORT ($Version)

FIRST LAUNCH:
Extract the ZIP to a writable folder and run broken_bond.exe.
Choose your original Xbox 360 ISO or the default.xex of a complete extracted
copy. The ISO is read in place; nothing is extracted or installed. A complete
copy in a game folder beside broken_bond.exe is found by itself.
Supported Title ID: 55530825 (default.xex SHA-256
8f70e79443e36b38e44b6a105dad51fb8909fb615d982753cabaac9b15f0d576).
The startup menu opens before the game; it can be turned off under
Settings > Advanced. Hold Shift while opening, or run broken_bond.exe --setup,
to return to the setup screen.

IN GAME:
Press F1 or Back+Start to open Settings, DLC and Exit game. The game keeps
running while the menu is open, with game input blocked.
Settings include Game, Display, Quality, Controls and Advanced, in seven
interface languages. Graphics options that need a restart say so.
Configure controls, under Controls, sets up four players.
Create a desktop shortcut from Settings > Advanced.

UPDATES:
The game tells you about new releases and opens the release page.
Close the game, keep your broken_bond.toml, and replace the other files with
those of the new ZIP. Keep game, user (saves), DLC and mods.
A broken_bond_launcher.exe left by version 0.1.0-beta can be deleted.

TRANSLATIONS:
Translation packages in mods\<name> appear under Settings > Game > Game
language.

Requires Windows x64, a Direct3D 12 or Vulkan 1.2 GPU and the Microsoft Visual
C++ 2015-2022 Redistributable (x64). Direct3D 12 is the default; Vulkan can be
chosen under Settings > Display.
Settings: broken_bond.toml. Saves and cache: user, unless user_data_root is set.
Alt+F4 closes the game.

Game files are not included. License and third-party notices: licenses\README.txt.
"@
    $zipName = "$portableName.zip"
    Compress-Archive -Path (Join-Path $portable '*') -DestinationPath (Join-Path $stage $zipName)
    $hash = (Get-FileHash -LiteralPath (Join-Path $stage $zipName) -Algorithm SHA256).Hash.ToLowerInvariant()
    Set-Content -LiteralPath (Join-Path $stage 'SHA256SUMS.txt') -Value "$hash  $zipName" -Encoding ascii
    $artifacts = @($portableName, $zipName, 'SHA256SUMS.txt')

    # Keep existing artifacts as backups instead of deleting them.
    $backup = Join-Path $OutputRootDir ('previous-release-' + [guid]::NewGuid().ToString('N'))
    $published = @()
    $backedUp = @()
    try {
        foreach ($name in $artifacts) {
            $existing = Join-Path $OutputRootDir $name
            if (Test-Path -LiteralPath $existing) {
                New-Item -ItemType Directory -Path $backup -Force | Out-Null
                Move-Item -LiteralPath $existing -Destination (Join-Path $backup $name)
                $backedUp += $name
            }
            Move-Item -LiteralPath (Join-Path $stage $name) -Destination $existing
            $published += $name
        }
    } catch {
        # Restore the old artifacts, without deleting them, if a destination is locked.
        foreach ($name in $published) {
            Move-Item -LiteralPath (Join-Path $OutputRootDir $name) -Destination (Join-Path $stage $name)
        }
        foreach ($name in $backedUp) {
            Move-Item -LiteralPath (Join-Path $backup $name) -Destination (Join-Path $OutputRootDir $name)
        }
        throw
    }
    Write-Host "Release artifacts: $OutputRootDir"
    if (Test-Path -LiteralPath $backup) { Write-Host "Previous artifacts preserved in $backup" }
} finally {
    # The stage is a unique directory created by this run, never an existing release.
    if (Test-Path -LiteralPath $stage) {
        $resolvedStage = (Resolve-Path -LiteralPath $stage).Path
        $resolvedOutput = (Resolve-Path -LiteralPath $OutputRootDir).Path
        if ((Split-Path $resolvedStage -Parent) -ne $resolvedOutput -or
            (Split-Path $resolvedStage -Leaf) -notlike '.packaging-*') {
            throw "Refusing to remove a staging directory outside the release output: $resolvedStage"
        }
        Remove-Item -LiteralPath $resolvedStage -Recurse -Force -ErrorAction SilentlyContinue
    }
}
