@echo off
setlocal

cd /d "%~dp0"
echo [1/3] Fetching origin/dev...
git -c http.version=HTTP/1.1 fetch origin dev
if errorlevel 1 git -c http.proxy= -c http.version=HTTP/1.1 fetch origin dev
if errorlevel 1 (
    echo Failed to fetch origin/dev. Check network or Git proxy settings.
    pause
    exit /b 1
)

git merge-base --is-ancestor HEAD origin/dev
if errorlevel 1 (
    echo Local dev branch has diverged from origin/dev. Resolve it manually first.
    pause
    exit /b 1
)

git merge --ff-only origin/dev
if errorlevel 1 (
    echo Failed to fast-forward the local dev branch.
    pause
    exit /b 1
)

echo [2/3] Building and packaging Audio-Video 2.0.1...
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0packaging\build-release-msvc.ps1" -Version 2.0.1
if errorlevel 1 (
    echo Build or packaging failed.
    pause
    exit /b 1
)

echo [3/3] Completed successfully.
echo Installer: %~dp0dist\MSVC\Audio-Video-MSVC-Setup-2.0.1.exe
echo Portable : %~dp0dist\MSVC\Audio-Video-MSVC-Portable-2.0.1.zip
pause
exit /b 0
