# Rejects a build without the native game (no setup screen) or without both
# graphics APIs before anything is built or staged. The build tool is a stub.
$ErrorActionPreference = 'Stop'
$fixture = Join-Path ([IO.Path]::GetTempPath()) ('broken-bond-release-gate-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path "$fixture/packaging", "$fixture/build/native", "$fixture/release" -Force | Out-Null
try {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot '../package-release.ps1') -Destination "$fixture/packaging/package-release.ps1"
    Set-Content -LiteralPath "$fixture/packaging/version.iss" -Value '#define MyAppVersion "0.1.0-beta"'
    $previous = [byte[]](10, 20, 30, 40)
    [IO.File]::WriteAllBytes("$fixture/release/broken-bond-recomp-win-amd64.zip", $previous)
    $buildCalls = [Collections.Generic.List[string]]::new()
    function cmake { $buildCalls.Add("$args"); throw 'The rejected package attempted a build.' }
    $cases = @(
        @{ Cache = "CMAKE_BUILD_TYPE:STRING=Release`nBROKENBOND_NATIVE_RENDERER:BOOL=OFF"
           Message = '*requires the native game with its setup screen*' }
        @{ Cache = "CMAKE_BUILD_TYPE:STRING=Release`nBROKENBOND_NATIVE_RENDERER:BOOL=ON`nBROKENBOND_NATIVE_D3D12:BOOL=ON`nBROKENBOND_NATIVE_VULKAN:BOOL=OFF"
           Message = '*BROKENBOND_NATIVE_VULKAN=ON*' }
    )
    foreach ($case in $cases) {
        Set-Content -LiteralPath "$fixture/build/native/CMakeCache.txt" -Value $case.Cache
        $rejected = $false
        try {
            & "$fixture/packaging/package-release.ps1" -OutputRootDir "$fixture/release"
        } catch {
            if ($_.Exception.Message -notlike $case.Message) { throw }
            $rejected = $true
        }
        if (-not $rejected -or $buildCalls.Count -ne 0) { throw "A build was accepted or started: $($case.Cache)" }
        if ([Convert]::ToBase64String([IO.File]::ReadAllBytes("$fixture/release/broken-bond-recomp-win-amd64.zip")) -ne
            [Convert]::ToBase64String($previous)) { throw 'An existing release changed.' }
        if (@(Get-ChildItem -LiteralPath "$fixture/release" -Force).Count -ne 1) { throw 'The rejected package staged artifacts.' }
    }
    Write-Output 'PASS: native OFF and Vulkan OFF rejected before build and staging; previous release preserved'
} finally {
    if (Test-Path -LiteralPath $fixture) {
        $resolved = (Resolve-Path -LiteralPath $fixture).Path
        $tempRoot = (Resolve-Path -LiteralPath ([IO.Path]::GetTempPath())).Path.TrimEnd('\', '/')
        if ((Split-Path $resolved -Parent).TrimEnd('\', '/') -ne $tempRoot -or
            (Split-Path $resolved -Leaf) -notlike 'broken-bond-release-gate-*') { throw "Unsafe fixture path: $resolved" }
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
