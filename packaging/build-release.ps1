param(
    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string]$Version = '1.1.13',
    [string]$QtRoot = 'E:\App\Qt\6.11.1\msvc2022_64',
    [string]$VsDevCmd = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat',
    [string]$FFmpegRoot = ''
)

$ErrorActionPreference = 'Stop'

# Audio-Video uses one ABI: MSVC 2022 x64. Keep the former generic entry
# point so existing build instructions also produce the MSVC release.
$builder = Join-Path $PSScriptRoot 'build-release-msvc.ps1'
& $builder -Version $Version -QtRoot $QtRoot -VsDevCmd $VsDevCmd -FFmpegRoot $FFmpegRoot
exit $LASTEXITCODE
