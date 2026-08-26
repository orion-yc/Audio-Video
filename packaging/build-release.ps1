param(
    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string]$Version = '1.0.9',
    [string]$QtRoot = 'E:\App\Qt\6.11.1\mingw_64',
    [string]$MinGwRoot = 'E:\App\Qt\Tools\mingw1310_64',
    [string]$OpenCvRoot = 'E:\perject\OpenCV-4.13.0-MinGW64\install',
    [string]$SpeexRoot = 'E:\perject\Speex-1.2.1-MinGW64\install',
    [string]$FFmpegRoot = ''
)

$ErrorActionPreference = 'Stop'

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$buildDir = Join-Path $projectRoot 'build-qt6'
$releaseDir = Join-Path $buildDir 'release'
$distDir = Join-Path $projectRoot 'dist'
$stageDir = Join-Path $distDir "Audio-Video-$Version"
$portableZip = Join-Path $distDir "Audio-Video-Portable-$Version.zip"
$ffmpegDefaultRoot = Join-Path $projectRoot 'third_party\ffmpeg-master-latest-win64-lgpl-shared'
if ([string]::IsNullOrWhiteSpace($FFmpegRoot)) {
    $FFmpegRoot = $ffmpegDefaultRoot
}
$ffmpegBin = Join-Path $FFmpegRoot 'bin'
$ffmpegLicense = Join-Path $FFmpegRoot 'LICENSE.txt'
$openCvBin = Join-Path $OpenCvRoot 'x64\mingw\bin'
$cascadeDir = Join-Path $OpenCvRoot 'etc\haarcascades'
$speexDll = Join-Path $SpeexRoot 'bin\libspeex.dll'
$eyeCascade = Join-Path $cascadeDir 'haarcascade_eye.xml'
$faceCascade = Join-Path $cascadeDir 'haarcascade_frontalface_default.xml'
$qmake = Join-Path $QtRoot 'bin\qmake.exe'
$windeployqt = Join-Path $QtRoot 'bin\windeployqt.exe'
$make = Join-Path $MinGwRoot 'bin\mingw32-make.exe'
$isccCandidates = @(
    (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe'),
    'C:\Program Files (x86)\Inno Setup 6\ISCC.exe',
    'C:\Program Files\Inno Setup 6\ISCC.exe'
)
$iscc = $isccCandidates | Where-Object { Test-Path -LiteralPath $_ } |
    Select-Object -First 1

function Assert-Tool([string]$Path, [string]$Name) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Name was not found at $Path"
    }
}

function Assert-WithinProject([string]$Path) {
    $resolved = [IO.Path]::GetFullPath($Path)
    $rootPrefix = $projectRoot.TrimEnd('\') + '\'
    if (-not $resolved.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to modify a path outside the project: $resolved"
    }
}

Assert-Tool $qmake 'qmake'
Assert-Tool $windeployqt 'windeployqt'
Assert-Tool $make 'mingw32-make'
Assert-Tool $ffmpegLicense 'FFmpeg LGPL license'
Assert-Tool $speexDll 'Speex runtime'
Assert-Tool $eyeCascade 'OpenCV eye cascade'
Assert-Tool $faceCascade 'OpenCV face cascade'
if (-not (Test-Path -LiteralPath $ffmpegBin -PathType Container)) {
    throw "FFmpeg runtime directory was not found at $ffmpegBin"
}
if (-not (Test-Path -LiteralPath $openCvBin -PathType Container)) {
    throw "OpenCV runtime directory was not found at $openCvBin"
}
if (-not $iscc) {
    throw 'Inno Setup 6 was not found. Install JRSoftware.InnoSetup first.'
}

$sourceExe = Join-Path $releaseDir 'Audio-Video.exe'
$stageExe = Join-Path $stageDir 'Audio-Video.exe'
$blockingPaths = @($sourceExe, $stageExe)
$running = Get-Process -Name 'Meeting02', 'Meeting', 'Audio-Video' -ErrorAction SilentlyContinue |
    Where-Object {
        $_.Path -and ($blockingPaths -contains [IO.Path]::GetFullPath($_.Path))
    }
if ($running) {
    throw "Close the running Audio-Video client for version $Version before packaging."
}

New-Item -ItemType Directory -Force -Path $buildDir, $distDir | Out-Null
$env:PATH = "$($MinGwRoot)\bin;$($QtRoot)\bin;$env:PATH"

Push-Location $buildDir
try {
    & $qmake (Join-Path $projectRoot 'Meeting02.pro') -spec win32-g++ 'CONFIG+=release'
    if ($LASTEXITCODE -ne 0) { throw 'qmake failed.' }
    & $make -j4 release
    if ($LASTEXITCODE -ne 0) { throw 'Release build failed.' }
}
finally {
    Pop-Location
}

Assert-WithinProject $stageDir
if (Test-Path -LiteralPath $stageDir) {
    Remove-Item -LiteralPath $stageDir -Recurse -Force
}
New-Item -ItemType Directory -Path $stageDir | Out-Null

Assert-Tool $sourceExe 'Audio-Video executable'
Copy-Item -LiteralPath $sourceExe -Destination (Join-Path $stageDir 'Audio-Video.exe')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'config.ini') -Destination $stageDir
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'README.txt') -Destination $stageDir
Copy-Item -LiteralPath $eyeCascade -Destination $stageDir
Copy-Item -LiteralPath $faceCascade -Destination $stageDir
Copy-Item -LiteralPath $speexDll -Destination $stageDir
$openCvDlls = Get-ChildItem -LiteralPath $openCvBin -Filter 'libopencv_*.dll' -File
if ($openCvDlls.Count -eq 0) {
    throw "OpenCV runtime directory does not contain libopencv DLLs: $openCvBin"
}
$openCvDlls | Copy-Item -Destination $stageDir

& $windeployqt --release --compiler-runtime --no-translations (Join-Path $stageDir 'Audio-Video.exe')
if ($LASTEXITCODE -ne 0) { throw 'windeployqt failed.' }

$ffmpegDlls = Get-ChildItem -LiteralPath $ffmpegBin -Filter '*.dll' -File
if ($ffmpegDlls.Count -lt 3) {
    throw "FFmpeg runtime directory does not contain enough DLLs: $ffmpegBin"
}
$ffmpegDlls | Copy-Item -Destination $stageDir
$licenseDir = Join-Path $stageDir 'THIRD_PARTY_LICENSES'
New-Item -ItemType Directory -Force -Path $licenseDir | Out-Null
Copy-Item -LiteralPath $ffmpegLicense -Destination (Join-Path $licenseDir 'FFmpeg-LGPL-3.0.txt')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'THIRD_PARTY_NOTICES.txt') -Destination $stageDir

$requiredFiles = @(
    'Audio-Video.exe',
    'config.ini',
    'README.txt',
    'Qt6Core.dll',
    'Qt6Gui.dll',
    'Qt6Widgets.dll',
    'Qt6Network.dll',
    'Qt6Multimedia.dll',
    'libspeex.dll',
    'avcodec-63.dll',
    'avutil-61.dll',
    'swscale-10.dll',
    'THIRD_PARTY_NOTICES.txt',
    'THIRD_PARTY_LICENSES\FFmpeg-LGPL-3.0.txt',
    'libopencv_core4130.dll',
    'haarcascade_eye.xml',
    'haarcascade_frontalface_default.xml',
    'platforms\qwindows.dll'
)
foreach ($relativePath in $requiredFiles) {
    $path = Join-Path $stageDir $relativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required deployment file is missing: $relativePath"
    }
}

$unexpected = Get-ChildItem -LiteralPath $stageDir -Recurse -File |
    Where-Object Extension -In @('.o', '.obj', '.cpp', '.h')
if ($unexpected) {
    throw "Build artifacts were copied into the release directory: $($unexpected.FullName -join ', ')"
}

Assert-WithinProject $portableZip
if (Test-Path -LiteralPath $portableZip) {
    Remove-Item -LiteralPath $portableZip -Force
}
Compress-Archive -Path (Join-Path $stageDir '*') -DestinationPath $portableZip -CompressionLevel Optimal

& $iscc "/DAppVersion=$Version" (Join-Path $PSScriptRoot 'Meeting.iss')
if ($LASTEXITCODE -ne 0) { throw 'Inno Setup compilation failed.' }

$installer = Join-Path $distDir "Audio-Video-Setup-$Version.exe"
Assert-Tool $installer 'Audio-Video installer'

$outputs = Get-Item -LiteralPath $installer, $portableZip
$outputs | Select-Object FullName, Length, LastWriteTime
