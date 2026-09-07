param(
    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string]$Version = '1.1.2',
    [string]$QtRoot = 'E:\App\Qt\6.11.1\msvc2022_64',
    [string]$VsDevCmd = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat',
    [string]$VcpkgRoot = '',
    [string]$FFmpegRoot = ''
)

$ErrorActionPreference = 'Stop'

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($VcpkgRoot)) {
    $VcpkgRoot = Join-Path $projectRoot 'third_party\vcpkg'
}
if ([string]::IsNullOrWhiteSpace($FFmpegRoot)) {
    $FFmpegRoot = Join-Path $projectRoot 'third_party\ffmpeg-master-latest-win64-lgpl-shared'
}

$buildDir = Join-Path $projectRoot 'build-msvc'
$releaseDir = Join-Path $buildDir 'release'
$distRoot = Join-Path $projectRoot 'dist'
$distDir = Join-Path $distRoot 'MSVC'
$artifactName = 'Audio-Video-MSVC'
$stageDir = Join-Path $distDir "$artifactName-$Version"
$portableZip = Join-Path $distDir "$artifactName-Portable-$Version.zip"
$installRoot = Join-Path $VcpkgRoot 'installed'
$vcpkg = Join-Path $VcpkgRoot 'vcpkg.exe'
$vcpkgInstalled = Join-Path $installRoot 'x64-windows'
$ffmpegBin = Join-Path $FFmpegRoot 'bin'
$ffmpegLicense = Join-Path $FFmpegRoot 'LICENSE.txt'
$cascadeDir = Join-Path $PSScriptRoot 'cascades'
$speexDll = Join-Path $vcpkgInstalled 'bin\speex-1.dll'
$qmake = Join-Path $QtRoot 'bin\qmake.exe'
$windeployqt = Join-Path $QtRoot 'bin\windeployqt.exe'
$isccCandidates = @(
    (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe'),
    'C:\Program Files (x86)\Inno Setup 6\ISCC.exe',
    'C:\Program Files\Inno Setup 6\ISCC.exe'
)
$iscc = $isccCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1

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

Assert-Tool $qmake 'MSVC qmake'
Assert-Tool $windeployqt 'MSVC windeployqt'
Assert-Tool $VsDevCmd 'Visual Studio developer command script'
Assert-Tool $ffmpegLicense 'FFmpeg LGPL license'
Assert-Tool $vcpkg 'vcpkg'
if (-not $iscc) {
    throw 'Inno Setup 6 was not found. Install JRSoftware.InnoSetup first.'
}

$env:VCPKG_DOWNLOADS = Join-Path $VcpkgRoot 'downloads'
$env:VCPKG_DEFAULT_BINARY_CACHE = Join-Path $VcpkgRoot 'archives'
New-Item -ItemType Directory -Force -Path $env:VCPKG_DOWNLOADS, $env:VCPKG_DEFAULT_BINARY_CACHE | Out-Null
$vcpkgArgs = @(
    'install', '--triplet', 'x64-windows', '--x-manifest-root', $projectRoot,
    '--x-install-root', $installRoot, '--downloads-root', $env:VCPKG_DOWNLOADS
)
& $vcpkg @vcpkgArgs
if ($LASTEXITCODE -ne 0) { throw 'vcpkg dependency installation failed.' }

$sourceExe = Join-Path $releaseDir 'Audio-Video.exe'
$stageExe = Join-Path $stageDir 'Audio-Video.exe'
$blockingPaths = @($sourceExe, $stageExe)
$running = Get-Process -Name 'Meeting02', 'Meeting', 'Audio-Video' -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -and ($blockingPaths -contains [IO.Path]::GetFullPath($_.Path)) }
if ($running) {
    throw "Close the running Audio-Video client for version $Version before packaging."
}

New-Item -ItemType Directory -Force -Path $buildDir, $distDir | Out-Null
$buildCommand = 'call "{0}" -arch=x64 -host_arch=x64 && "{1}" "{2}" -spec win32-msvc "CONFIG+=release" && nmake /NOLOGO /f Makefile.Release' -f `
    $VsDevCmd, $qmake, (Join-Path $projectRoot 'Meeting02.pro')
Push-Location $buildDir
try {
    & cmd.exe /d /c $buildCommand
    if ($LASTEXITCODE -ne 0) { throw 'MSVC release build failed.' }
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
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'README-MSVC.txt') -Destination (Join-Path $stageDir 'README.txt')

$eyeCascade = Join-Path $cascadeDir 'haarcascade_eye.xml'
$faceCascade = Join-Path $cascadeDir 'haarcascade_frontalface_default.xml'
Assert-Tool $eyeCascade 'OpenCV eye cascade'
Assert-Tool $faceCascade 'OpenCV face cascade'
Copy-Item -LiteralPath $eyeCascade, $faceCascade -Destination $stageDir

$vcpkgRuntimeDir = Join-Path $vcpkgInstalled 'bin'
$vcpkgRuntimeDlls = Get-ChildItem -LiteralPath $vcpkgRuntimeDir -Filter '*.dll' -File
if ($vcpkgRuntimeDlls.Count -eq 0) { throw 'vcpkg runtime DLLs were not found.' }
$vcpkgRuntimeDlls | Copy-Item -Destination $stageDir

$deployCommand = 'call "{0}" -arch=x64 -host_arch=x64 && "{1}" --release --compiler-runtime --no-translations "{2}"' -f `
    $VsDevCmd, $windeployqt, (Join-Path $stageDir 'Audio-Video.exe')
& cmd.exe /d /c $deployCommand
if ($LASTEXITCODE -ne 0) { throw 'windeployqt failed.' }

$vsInstallRoot = Split-Path (Split-Path (Split-Path $VsDevCmd -Parent) -Parent) -Parent
$vcRedistRoot = Join-Path $vsInstallRoot 'VC\Redist\MSVC'
$vcRuntimeDirectory = Get-ChildItem -LiteralPath $vcRedistRoot -Directory -ErrorAction SilentlyContinue |
    Sort-Object Name -Descending |
    ForEach-Object {
        Get-ChildItem -LiteralPath (Join-Path $_.FullName 'x64') -Directory -Filter 'Microsoft.VC*.CRT' `
            -ErrorAction SilentlyContinue
    } |
    Select-Object -First 1
if (-not $vcRuntimeDirectory) {
    throw 'MSVC x64 runtime directory was not found.'
}
$vcRuntimeDlls = Get-ChildItem -LiteralPath $vcRuntimeDirectory.FullName -Filter '*.dll' -File
if ($vcRuntimeDlls.Count -eq 0) {
    throw 'MSVC x64 runtime DLLs were not found.'
}
$vcRuntimeDlls | Copy-Item -Destination $stageDir

if (-not (Test-Path -LiteralPath $ffmpegBin -PathType Container)) {
    throw "FFmpeg runtime directory was not found at $ffmpegBin"
}
$ffmpegDlls = Get-ChildItem -LiteralPath $ffmpegBin -Filter '*.dll' -File
if ($ffmpegDlls.Count -lt 3) { throw 'FFmpeg runtime directory does not contain enough DLLs.' }
$ffmpegDlls | Copy-Item -Destination $stageDir
$licenseDir = Join-Path $stageDir 'THIRD_PARTY_LICENSES'
New-Item -ItemType Directory -Force -Path $licenseDir | Out-Null
Copy-Item -LiteralPath $ffmpegLicense -Destination (Join-Path $licenseDir 'FFmpeg-LGPL-3.0.txt')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'licenses\OpenCV-4.12.0.txt') -Destination (Join-Path $licenseDir 'OpenCV-4.12.0.txt')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'THIRD_PARTY_NOTICES.txt') -Destination $stageDir

$requiredFiles = @(
    'Audio-Video.exe', 'config.ini', 'README.txt', 'Qt6Core.dll', 'Qt6Gui.dll',
    'Qt6Widgets.dll', 'Qt6Network.dll', 'Qt6Multimedia.dll', 'msvcp140.dll', 'speex-1.dll', 'jpeg62.dll',
    'libpng16.dll', 'libwebp.dll', 'libwebpdecoder.dll', 'libwebpdemux.dll', 'libwebpmux.dll', 'liblzma.dll',
    'tiff.dll', 'turbojpeg.dll', 'z.dll', 'avcodec-63.dll',
    'vcruntime140.dll', 'vcruntime140_1.dll',
    'avutil-61.dll', 'swscale-10.dll', 'THIRD_PARTY_NOTICES.txt',
    'THIRD_PARTY_LICENSES\FFmpeg-LGPL-3.0.txt', 'THIRD_PARTY_LICENSES\OpenCV-4.12.0.txt', 'haarcascade_eye.xml',
    'haarcascade_frontalface_default.xml', 'platforms\qwindows.dll'
)
foreach ($relativePath in $requiredFiles) {
    if (-not (Test-Path -LiteralPath (Join-Path $stageDir $relativePath) -PathType Leaf)) {
        throw "Required deployment file is missing: $relativePath"
    }
}

Assert-WithinProject $portableZip
if (Test-Path -LiteralPath $portableZip) { Remove-Item -LiteralPath $portableZip -Force }
Compress-Archive -Path (Join-Path $stageDir '*') -DestinationPath $portableZip -CompressionLevel Optimal

& $iscc "/DAppVersion=$Version" '/DAppVariant=-MSVC' '/DAppDistribution=MSVC' `
    (Join-Path $PSScriptRoot 'Meeting.iss')
if ($LASTEXITCODE -ne 0) { throw 'Inno Setup compilation failed.' }

$installer = Join-Path $distDir "$artifactName-Setup-$Version.exe"
Assert-Tool $installer 'Audio-Video MSVC installer'
(Get-Item -LiteralPath $installer, $portableZip) | Select-Object FullName, Length, LastWriteTime
