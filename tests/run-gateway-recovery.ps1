param(
    [string]$QtRoot = 'E:\App\Qt\6.11.1\msvc2022_64',
    [string]$VsDevCmd = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat'
)
$ErrorActionPreference = 'Stop'
$project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$build = Join-Path $project 'build-gateway-tests'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$command = 'call "{0}" -arch=x64 -host_arch=x64 && cd /d "{1}" && "{2}" "{3}" -spec win32-msvc "CONFIG+=release" && nmake /NOLOGO /f Makefile.Release' -f `
    $VsDevCmd, $build, (Join-Path $QtRoot 'bin\qmake.exe'), (Join-Path $PSScriptRoot 'gateway_recovery.pro')
& cmd.exe /d /c $command
if ($LASTEXITCODE -ne 0) { throw 'Gateway recovery test compilation failed.' }
$env:PATH = (Join-Path $QtRoot 'bin') + ';' + $env:PATH
$exe = Join-Path $build 'release\gateway_recovery.exe'
$process = Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden -PassThru
if (-not $process.WaitForExit(45000)) {
    $process.Kill()
    throw 'Gateway recovery test exceeded 45 seconds.'
}
Get-Content -LiteralPath (Join-Path (Split-Path $exe) 'results.txt')
if ($process.ExitCode -ne 0) { throw "Gateway recovery test failed: $($process.ExitCode)" }
