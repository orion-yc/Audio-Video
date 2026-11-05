param([string]$Destination = '')
$ErrorActionPreference = 'Stop'
$version = '1.13.0'
$sha256 = 'c97fe4d0efda5d6db76e1906fa6d61845af51abaf22744c162d5bde3a89d1d38'
if ([string]::IsNullOrWhiteSpace($Destination)) {
    $Destination = Join-Path $PSScriptRoot '..\third_party\livekit-1.13.0'
}
$archiveName = "livekit-sdk-windows-x64-$version.zip"
$archive = Join-Path ([IO.Path]::GetTempPath()) ([Guid]::NewGuid().ToString() + '.zip')
try {
    Invoke-WebRequest -Uri "https://github.com/livekit/client-sdk-cpp/releases/download/v$version/$archiveName" -OutFile $archive
    if ((Get-FileHash -Algorithm SHA256 -LiteralPath $archive).Hash.ToLowerInvariant() -ne $sha256) {
        throw 'LiveKit SDK SHA-256 did not match the pinned official release.'
    }
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    Expand-Archive -LiteralPath $archive -DestinationPath $Destination -Force
    $root = Join-Path $Destination "livekit-sdk-windows-x64-$version"
    foreach ($file in @('include\livekit\encoded_video_source.h', 'lib\livekit.lib', 'bin\livekit.dll', 'bin\livekit_ffi.dll')) {
        if (-not (Test-Path -LiteralPath (Join-Path $root $file))) { throw "SDK file missing: $file" }
    }
    Write-Output "Verified LiveKit SDK installed: $root"
}
finally {
    if (Test-Path -LiteralPath $archive) { Remove-Item -LiteralPath $archive -Force }
}
