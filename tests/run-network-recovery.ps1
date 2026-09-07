# Run after the MinGW release build. Uses its actual production object files.
$ErrorActionPreference = 'Stop'
$project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$build = Join-Path $project 'build-qt6'
$output = Join-Path $build 'network-test'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$objects = Get-Content -Raw (Join-Path $build 'release/object_script.Audio-Video.Release')
$objects.Replace('release/main.o', 'network-test/network_recovery.o') |
    Set-Content -Encoding ascii (Join-Path $output 'objects')
@'
include Makefile.Release
.PHONY: network-test
network-test:
	$(CXX) -c $(CXXFLAGS) $(INCPATH) ../tests/network_recovery.cpp -o network-test/network_recovery.o
	$(LINKER) $(LFLAGS) -o network-test/network_recovery.exe @network-test/objects $(LIBS)
'@ | Set-Content -Encoding ascii (Join-Path $output 'test.mk')
$env:PATH = "E:/App/Qt/Tools/mingw1310_64/bin;E:/App/Qt/6.11.1/mingw_64/bin;$project/dist/MinGW/Audio-Video-1.1.2;$env:PATH"
$env:QT_QPA_PLATFORM = 'offscreen'
$env:QT_PLUGIN_PATH = 'E:/App/Qt/6.11.1/mingw_64/plugins'
Push-Location $build
try {
    & 'E:/App/Qt/Tools/mingw1310_64/bin/mingw32-make.exe' -f network-test/test.mk network-test
    if ($LASTEXITCODE -ne 0) { throw 'Network harness compilation failed' }
    $process = Start-Process -FilePath (Join-Path $output 'network_recovery.exe') -WorkingDirectory $output -WindowStyle Hidden -PassThru
    if (-not $process.WaitForExit(30000)) {
        $process.Kill()
        throw 'Network harness exceeded 30 seconds'
    }
    Get-Content (Join-Path $output 'results.txt')
    if ($process.ExitCode -ne 0) { throw "Network harness failed: $($process.ExitCode)" }
} finally { Pop-Location }
