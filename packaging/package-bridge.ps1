[CmdletBinding()]
param(
    [string]$PackageDir,
    [string]$OutputRootDir,
    [string]$IsccPath
)

# Builds BrokenBondInstaller.exe, the bridge that moves 0.1.0-beta installations
# to the game-only layout, from the folder that package-release.ps1 publishes.
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $OutputRootDir) { $OutputRootDir = Join-Path $projectRoot 'dist' }
$OutputRootDir = [IO.Path]::GetFullPath($OutputRootDir)
if (-not $PackageDir) { $PackageDir = Join-Path $OutputRootDir 'broken-bond-recomp-win-amd64' }
$PackageDir = (Resolve-Path -LiteralPath $PackageDir).Path
foreach ($name in @('broken_bond.exe', 'rexruntime.dll', 'defaults\broken_bond.toml', 'broken_bond.version', 'README.txt', 'licenses\README.txt')) {
    if (-not (Test-Path -LiteralPath (Join-Path $PackageDir $name) -PathType Leaf)) {
        throw "Missing package file: $name. Run package-release.ps1 first."
    }
}
foreach ($name in @('broken_bond_launcher.exe', 'broken_bond_setup_helper.exe', 'extract-xiso.exe', 'assets')) {
    if (Test-Path -LiteralPath (Join-Path $PackageDir $name)) { throw "Obsolete file in the bridge payload: $name" }
}
# The bridge carries exactly the version of its ZIP, -Version builds included.
$version = (Get-Content -LiteralPath (Join-Path $PackageDir 'broken_bond.version') -Raw).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+(-[0-9A-Za-z.]+)?$') { throw "Invalid package version: $version" }
if (-not $IsccPath) {
    $compiler = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($compiler) { $IsccPath = $compiler.Source }
    foreach ($base in @(${env:ProgramFiles(x86)}, $env:ProgramFiles, $env:LOCALAPPDATA)) {
        if (-not $base) { continue }
        foreach ($sub in @('Inno Setup 6/ISCC.exe', 'Programs/Inno Setup 6/ISCC.exe')) {
            $candidate = Join-Path $base $sub
            if (-not $IsccPath -and (Test-Path -LiteralPath $candidate -PathType Leaf)) { $IsccPath = $candidate }
        }
    }
}
if (-not $IsccPath -or -not (Test-Path -LiteralPath $IsccPath -PathType Leaf)) {
    throw 'Install Inno Setup 6.4+ or pass -IsccPath to build the bridge.'
}

$stage = Join-Path $OutputRootDir ('.bridge-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stage -Force | Out-Null
try {
    & $IsccPath /Qp "/O$stage" "/DPackageDir=$PackageDir" "/DMyAppVersion=$version" (Join-Path $PSScriptRoot 'installer.iss')
    if ($LASTEXITCODE -ne 0) { throw "Bridge compilation failed ($LASTEXITCODE); the previous bridge was not changed." }
    $bridgeName = 'BrokenBondInstaller.exe'
    $bridge = Join-Path $stage $bridgeName
    if (-not (Test-Path -LiteralPath $bridge -PathType Leaf) -or (Get-Item -LiteralPath $bridge).Length -eq 0) {
        throw 'Bridge output missing.'
    }
    $destination = Join-Path $OutputRootDir $bridgeName
    $backup = Join-Path $OutputRootDir ('previous-bridge-' + [guid]::NewGuid().ToString('N') + '.exe')
    $hadPrevious = Test-Path -LiteralPath $destination
    if ($hadPrevious) { Move-Item -LiteralPath $destination -Destination $backup }
    try {
        Move-Item -LiteralPath $bridge -Destination $destination
    } catch {
        if ($hadPrevious) { Move-Item -LiteralPath $backup -Destination $destination }
        throw
    }
    $hash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant()
    # The release's SHA256SUMS.txt (package-release.ps1) gains the bridge's line.
    $sums = Join-Path $OutputRootDir 'SHA256SUMS.txt'
    $lines = @()
    if (Test-Path -LiteralPath $sums) {
        $lines = @(Get-Content -LiteralPath $sums | Where-Object { $_ -and $_ -notmatch "\s$([regex]::Escape($bridgeName))$" })
    }
    Set-Content -LiteralPath $sums -Value ($lines + "$hash  $bridgeName") -Encoding ascii
    Write-Host "Bridge $version`: $destination"
    Write-Host "SHA-256: $hash  $bridgeName"
    if ($hadPrevious) { Write-Host "Previous bridge preserved as $backup" }
} finally {
    # The stage is a unique directory created by this run.
    if (Test-Path -LiteralPath $stage) {
        $resolvedStage = (Resolve-Path -LiteralPath $stage).Path
        if ((Split-Path $resolvedStage -Parent) -ne (Resolve-Path -LiteralPath $OutputRootDir).Path -or
            (Split-Path $resolvedStage -Leaf) -notlike '.bridge-*') {
            throw "Refusing to remove a staging directory outside the bridge output: $resolvedStage"
        }
        Remove-Item -LiteralPath $resolvedStage -Recurse -Force -ErrorAction SilentlyContinue
    }
}
