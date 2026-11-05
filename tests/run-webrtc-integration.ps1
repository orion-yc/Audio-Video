param(
    [string]$QtRoot = 'E:\App\Qt\6.11.1\msvc2022_64',
    [string]$VsDevCmd = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat',
    [string]$GatewayUrl = 'https://video.onnotify.top',
    [string]$LiveKitUrl = 'wss://video.onnotify.top'
)
$ErrorActionPreference = 'Stop'
$project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$sdk = Join-Path $project 'third_party\livekit-1.13.0\livekit-sdk-windows-x64-1.13.0'
if (-not (Test-Path (Join-Path $sdk 'lib\livekit.lib'))) { & (Join-Path $project 'packaging\install-livekit-sdk.ps1') }
$build = Join-Path $project 'build-webrtc-tests'
New-Item -ItemType Directory -Force $build | Out-Null
$qmake = Join-Path $QtRoot 'bin\qmake.exe'
$command = 'call "{0}" -arch=x64 -host_arch=x64 && cd /d "{1}" && "{2}" "{3}" -spec win32-msvc "CONFIG+=release" "LIVEKIT_SDK_ROOT={4}" && nmake /NOLOGO /f Makefile.Release' -f `
    $VsDevCmd, $build, $qmake, (Join-Path $PSScriptRoot 'webrtc_integration.pro'), $sdk
& cmd.exe /d /c $command
if ($LASTEXITCODE -ne 0) { throw 'Integration test build failed' }
$env:PATH = (Join-Path $QtRoot 'bin') + ';' + (Join-Path $sdk 'bin') + ';' + $env:PATH
& (Join-Path $build 'release\webrtc_integration.exe') $GatewayUrl $LiveKitUrl `
    (Join-Path $PSScriptRoot 'fixtures\webrtc-idr.h264') (Join-Path $build 'report.json')
if ($LASTEXITCODE -ne 0) { throw 'Integration test failed' }
Write-Output "Test creates temporary webrtc-smoke accounts. IDs for cleanup: $build\report.json"
