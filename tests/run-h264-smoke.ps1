param(
    [string]$QtRoot = 'E:\App\Qt\6.11.1\msvc2022_64',
    [string]$VsDevCmd = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat',
    [switch]$RequireNvenc
)
$ErrorActionPreference = 'Stop'
$project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$build = Join-Path $project 'build-h264-tests'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$command = 'call "{0}" -arch=x64 -host_arch=x64 && cd /d "{1}" && "{2}" "{3}" -spec win32-msvc "CONFIG+=release" && nmake /NOLOGO /f Makefile.Release' -f `
    $VsDevCmd, $build, (Join-Path $QtRoot 'bin\qmake.exe'), (Join-Path $PSScriptRoot 'h264codec_smoke.pro')
& cmd.exe /d /c $command
if ($LASTEXITCODE -ne 0) { throw 'H.264 smoke test compilation failed.' }
$env:PATH = (Join-Path $QtRoot 'bin') + ';' + (Join-Path $project 'third_party\ffmpeg-master-latest-win64-lgpl-shared\bin') + ';' + $env:PATH
$env:QT_QPA_PLATFORM = 'offscreen'
$env:AUDIO_VIDEO_REQUIRE_NVENC = [int][bool]$RequireNvenc
$exe = Join-Path $build 'release\h264codec_smoke.exe'
$outputLog = Join-Path $build 'results.txt'
$errorLog = Join-Path $build 'stderr.txt'
$process = New-Object System.Diagnostics.Process
$process.StartInfo.FileName = $exe
$process.StartInfo.WorkingDirectory = Split-Path $exe
$process.StartInfo.UseShellExecute = $false
$process.StartInfo.CreateNoWindow = $true
$process.StartInfo.RedirectStandardOutput = $true
$process.StartInfo.RedirectStandardError = $true
try {
    if (-not $process.Start()) { throw 'Unable to start H.264 smoke test.' }
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit(60000)) {
        $process.Kill()
        $process.WaitForExit()
        throw 'H.264 smoke test exceeded 60 seconds.'
    }
    $stdout.Result | Set-Content -LiteralPath $outputLog -Encoding UTF8
    $stderr.Result | Set-Content -LiteralPath $errorLog -Encoding UTF8
    Get-Content -LiteralPath $outputLog
    if ($process.ExitCode -ne 0) {
        Get-Content -LiteralPath $errorLog -Tail 25
        throw "H.264 smoke test failed: $($process.ExitCode)"
    }
} finally {
    $process.Dispose()
}
