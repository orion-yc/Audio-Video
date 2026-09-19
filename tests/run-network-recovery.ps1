param(
    [string]$QtRoot = 'E:\App\Qt\6.11.1\msvc2022_64',
    [string]$VsDevCmd = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat'
)

$ErrorActionPreference = 'Stop'
$project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$build = Join-Path $project 'build-msvc-tests'
$qmake = Join-Path $QtRoot 'bin\qmake.exe'
$testProject = Join-Path $PSScriptRoot 'network_recovery.pro'
$testExecutable = Join-Path $build 'release\network_recovery.exe'

foreach ($path in @($qmake, $VsDevCmd, $testProject)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required test tool was not found: $path"
    }
}

New-Item -ItemType Directory -Force -Path $build | Out-Null
$buildCommand = 'call "{0}" -arch=x64 -host_arch=x64 && cd /d "{1}" && "{2}" "{3}" -spec win32-msvc "CONFIG+=release" && nmake /NOLOGO /f Makefile.Release' -f `
    $VsDevCmd, $build, $qmake, $testProject
& cmd.exe /d /c $buildCommand
if ($LASTEXITCODE -ne 0) { throw 'Network recovery test compilation failed.' }

$runtimePaths = @(
    (Join-Path $QtRoot 'bin'),
    (Join-Path $project 'third_party\vcpkg\installed\x64-windows\bin'),
    (Join-Path $project 'third_party\ffmpeg-master-latest-win64-lgpl-shared\bin')
)
$env:PATH = ($runtimePaths -join ';') + ';' + $env:PATH
$env:QT_QPA_PLATFORM = 'offscreen'

$process = Start-Process -FilePath $testExecutable -WorkingDirectory (Split-Path $testExecutable) `
    -WindowStyle Hidden -PassThru
if (-not $process.WaitForExit(30000)) {
    $process.Kill()
    throw 'Network recovery test exceeded 30 seconds.'
}

$results = Join-Path (Split-Path $testExecutable) 'results.txt'
Get-Content -LiteralPath $results
if ($process.ExitCode -ne 0) {
    throw "Network recovery test failed: $($process.ExitCode)"
}
