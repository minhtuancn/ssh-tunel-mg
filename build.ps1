[CmdletBinding()]
param(
    [ValidateSet('Configure', 'Build', 'Test', 'Clean')]
    [string]$Action = 'Build',
    [ValidateSet('x64')]
    [string]$Arch = 'x64'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$build = Join-Path $root 'build'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'

function Find-VsInstall {
    if (-not (Test-Path $vswhere)) { throw 'Visual Studio Installer (vswhere.exe) not found.' }
    $path = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or -not $path) { throw 'MSVC x64 tools missing. Install Visual Studio 2022 C++ Build Tools workload.' }
    return $path.Trim()
}

switch ($Action) {
    'Clean' {
        if (Test-Path $build) { Remove-Item -LiteralPath $build -Recurse -Force }
        exit 0
    }
    'Configure' {
        $vs = Find-VsInstall
        $cmake = Get-Command cmake -ErrorAction Stop
        & $cmake.Source -S $root -B $build -G 'Visual Studio 17 2022' -A x64
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }
    'Build' {
        if (-not (Test-Path (Join-Path $build 'CMakeCache.txt'))) { & $PSCommandPath -Action Configure; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE } }
        & cmake --build $build --config Release --parallel
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }
    'Test' {
        if (-not (Test-Path (Join-Path $build 'CMakeCache.txt'))) { & $PSCommandPath -Action Configure; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE } }
        & cmake --build $build --config Release --parallel
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        & ctest --test-dir $build -C Release --output-on-failure
        exit $LASTEXITCODE
    }
}
